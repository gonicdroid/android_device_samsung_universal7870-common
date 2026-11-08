/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "FrontFlashHook.h"

#include <log/log.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>
#include <time.h>
#include <system/camera_metadata.h>

#undef LOG_TAG
#define LOG_TAG "ExynosCameraFrontFlash"

typedef int (*orig_constructStaticInfo_t)(int cameraId, camera_metadata_t **info);
static orig_constructStaticInfo_t g_orig_constructStaticInfo = nullptr;

typedef int (*orig_initilizeDevice_t)(void *this_ptr, const camera3_callback_ops_t *callback_ops);
static orig_initilizeDevice_t g_orig_initilizeDevice = nullptr;

typedef int (*orig_processCaptureRequest_t)(void *this_ptr, camera3_capture_request_t *request);
static orig_processCaptureRequest_t g_orig_processCaptureRequest = nullptr;

typedef int (*orig_releaseDevice_t)(void *this_ptr);
static orig_releaseDevice_t g_orig_releaseDevice = nullptr;

static void (*g_framework_process_capture_result)(const struct camera3_callback_ops *,
        const camera3_capture_result_t *) = nullptr;

static int s_cam1_flash_mode = 0;
static int s_cam1_ae_mode = 1;
static bool s_cam1_torch_active = false;
static bool s_cam1_precapture_active = false;
static uint32_t s_cam1_precapture_trigger_frame = 0;
static int s_cam1_precapture_frames_seen = 0;
static bool s_cam1_still_capture_in_flight = false;
static uint32_t s_cam1_still_capture_frame = 0;
static int s_cam1_flash_firing = 0; // 0=off, 1=torch, 2=capture/pre-flash
static int64_t s_cam1_capture_time_ms = 0;
static camera3_stream_t *s_cam1_picture_stream = nullptr;
static camera3_stream_t *s_cam1_preview_stream = nullptr;

static inline int64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void set_front_flash(int val) {
    static int s_cur_val = -1;
    if (s_cur_val == val) return;
    s_cur_val = val;

    int fd = open("/sys/class/camera/flash/front_torch_flash", O_WRONLY);
    if (fd >= 0) {
        char buf[8];
        int len = snprintf(buf, sizeof(buf), "%d\n", val);
        write(fd, buf, len);
        close(fd);
        ALOGI("set_front_flash: wrote %d to /sys/class/camera/flash/front_torch_flash", val);
    } else {
        ALOGW("set_front_flash: open /sys/class/camera/flash/front_torch_flash failed: %s", strerror(errno));
    }
}

static void *front_flash_watchdog(void *arg) {
    (void)arg;
    for (int i = 0; i < 25; i++) {
        usleep(100000); // 100ms
        if (s_cam1_flash_firing != 2) break;
        int64_t elapsed = get_time_ms() - s_cam1_capture_time_ms;
        if (elapsed > 2500) {
            ALOGW("front_flash_watchdog: max timeout (2.5s) reached, turning OFF front flash");
            set_front_flash(0);
            s_cam1_flash_firing = 0;
            s_cam1_precapture_active = false;
            s_cam1_still_capture_in_flight = false;
            break;
        }
    }
    return nullptr;
}

static void patch_camera_metadata(int cameraId, camera_metadata_t **info) {
    if (!info || !*info) return;
    camera_metadata_t *old_meta = *info;

    size_t entry_count = get_camera_metadata_entry_count(old_meta);
    size_t data_count = get_camera_metadata_data_count(old_meta);

    camera_metadata_t *new_meta = allocate_camera_metadata(entry_count + 16, data_count + 256);
    if (!new_meta) {
        ALOGE("patch_camera_metadata(cam%d): allocate_camera_metadata failed", cameraId);
        return;
    }

    if (append_camera_metadata(new_meta, old_meta) != 0) {
        ALOGE("patch_camera_metadata(cam%d): append_camera_metadata failed", cameraId);
        free_camera_metadata(new_meta);
        return;
    }

    // 1. ALL CAMERAS: ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE = REALTIME (1)
    // Samsung Exynos 7870 camera kernel driver produces timestamps using ktime_get_boot_ns() (BOOTTIME).
    // The legacy Samsung Oreo HAL statically declared UNKNOWN (0), causing Camera3Device not to calculate
    // mTimestampOffset, leading to a 13-16s frozen first video frame in Stagefright/MPEG4Writer.
    uint8_t timestamp_source = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
    camera_metadata_entry_t entry;
    if (find_camera_metadata_entry(new_meta, ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &entry) == 0) {
        update_camera_metadata_entry(new_meta, entry.index, &timestamp_source, 1, nullptr);
    } else {
        add_camera_metadata_entry(new_meta, ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &timestamp_source, 1);
    }

    // 2. FRONT CAMERA ONLY (cam 1): Flash configuration
    if (cameraId == 1) {
        // ANDROID_FLASH_INFO_AVAILABLE = TRUE (1)
        uint8_t flash_avail = ANDROID_FLASH_INFO_AVAILABLE_TRUE;
        if (find_camera_metadata_entry(new_meta, ANDROID_FLASH_INFO_AVAILABLE, &entry) == 0) {
            update_camera_metadata_entry(new_meta, entry.index, &flash_avail, 1, nullptr);
        } else {
            add_camera_metadata_entry(new_meta, ANDROID_FLASH_INFO_AVAILABLE, &flash_avail, 1);
        }

        // ANDROID_CONTROL_AE_AVAILABLE_MODES
        uint8_t ae_modes[] = {
            ANDROID_CONTROL_AE_MODE_OFF,
            ANDROID_CONTROL_AE_MODE_ON,
            ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH,
            ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH
        };
        if (find_camera_metadata_entry(new_meta, ANDROID_CONTROL_AE_AVAILABLE_MODES, &entry) == 0) {
            update_camera_metadata_entry(new_meta, entry.index, ae_modes, sizeof(ae_modes), nullptr);
        } else {
            add_camera_metadata_entry(new_meta, ANDROID_CONTROL_AE_AVAILABLE_MODES, ae_modes, sizeof(ae_modes));
        }

        // ANDROID_FLASH_INFO_CHARGE_DURATION
        int64_t charge_duration = 0;
        if (find_camera_metadata_entry(new_meta, ANDROID_FLASH_INFO_CHARGE_DURATION, &entry) == 0) {
            update_camera_metadata_entry(new_meta, entry.index, &charge_duration, 1, nullptr);
        } else {
            add_camera_metadata_entry(new_meta, ANDROID_FLASH_INFO_CHARGE_DURATION, &charge_duration, 1);
        }
    }

    free_camera_metadata(old_meta);
    *info = new_meta;
    ALOGI("patch_camera_metadata(cam%d): successfully patched metadata (timestampSource=REALTIME%s)",
          cameraId, (cameraId == 1) ? ", flashInfo=AVAILABLE" : "");
}

static int my_constructStaticInfo(int cameraId, camera_metadata_t **info) {
    int ret = 0;
    if (g_orig_constructStaticInfo) {
        ret = g_orig_constructStaticInfo(cameraId, info);
    } else {
        void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
        if (h) {
            orig_constructStaticInfo_t fn = (orig_constructStaticInfo_t)dlsym(
                h, "_ZN7android30ExynosCamera3MetadataConverter19constructStaticInfoEiPP15camera_metadata");
            if (fn) ret = fn(cameraId, info);
        }
    }

    if (ret == 0) {
        patch_camera_metadata(cameraId, info);
    }
    return ret;
}

static void my_process_capture_result(const struct camera3_callback_ops *ops,
        const camera3_capture_result_t *result)
{
    if (result) {
        uint32_t frame_num = result->frame_number;

        // 1. Handle AE precapture state machine
        if (result->result && s_cam1_precapture_active) {
            // Only update AE state for frames starting from the precapture trigger frame
            if (frame_num >= s_cam1_precapture_trigger_frame) {
                camera_metadata_entry_t entry;
                camera_metadata_t *meta = const_cast<camera_metadata_t *>(result->result);
                if (find_camera_metadata_entry(meta, ANDROID_CONTROL_AE_STATE, &entry) == 0) {
                    s_cam1_precapture_frames_seen++;
                    if (s_cam1_precapture_frames_seen < 3) {
                        uint8_t state = ANDROID_CONTROL_AE_STATE_PRECAPTURE;
                        update_camera_metadata_entry(meta, entry.index, &state, 1, nullptr);
                        ALOGI("my_process_capture_result(cam1): frame %u (count %d) -> AE_STATE_PRECAPTURE",
                              frame_num, s_cam1_precapture_frames_seen);
                    } else {
                        uint8_t state = ANDROID_CONTROL_AE_STATE_CONVERGED;
                        update_camera_metadata_entry(meta, entry.index, &state, 1, nullptr);
                        ALOGI("my_process_capture_result(cam1): frame %u (count %d) -> AE_STATE_CONVERGED!",
                              frame_num, s_cam1_precapture_frames_seen);
                        s_cam1_precapture_active = false;
                    }
                }
            }
        }

        // 2. Handle Still Capture completion
        if (s_cam1_still_capture_in_flight) {
            bool capture_done = false;

            // Check if the picture output buffer was delivered in this result
            for (uint32_t i = 0; i < result->num_output_buffers; i++) {
                camera3_stream_t *s = result->output_buffers[i].stream;
                if (s && (s == s_cam1_picture_stream || s->format == HAL_PIXEL_FORMAT_BLOB)) {
                    capture_done = true;
                    ALOGI("my_process_capture_result(cam1): picture buffer delivered in frame %u!", frame_num);
                    break;
                }
            }

            // Or if normal preview frames resume after the still capture frame
            if (!capture_done && frame_num > s_cam1_still_capture_frame) {
                int64_t elapsed = get_time_ms() - s_cam1_capture_time_ms;
                if (elapsed >= 300) {
                    capture_done = true;
                    ALOGI("my_process_capture_result(cam1): frame %u > still capture %u (%lld ms elapsed), capture done",
                          frame_num, s_cam1_still_capture_frame, (long long)elapsed);
                }
            }

            if (capture_done) {
                s_cam1_still_capture_in_flight = false;
                if (!s_cam1_torch_active) {
                    ALOGI("my_process_capture_result(cam1): still capture complete, turning OFF front flash");
                    set_front_flash(0);
                    s_cam1_flash_firing = 0;
                }
            }
        }
    }

    if (g_framework_process_capture_result) {
        g_framework_process_capture_result(ops, result);
    }
}

static int my_initilizeDevice(void *this_ptr, const camera3_callback_ops_t *callback_ops) {
    ALOGI("my_initilizeDevice: this=%p, callback_ops=%p", this_ptr, callback_ops);
    if (callback_ops) {
        if (!g_framework_process_capture_result &&
            callback_ops->process_capture_result != my_process_capture_result) {
            g_framework_process_capture_result = callback_ops->process_capture_result;
        }

        // In HIDL CameraDeviceSession, camera3_callback_ops is embedded directly
        // inside the class, and callbacks downcast callback_ops to CameraDeviceSession*
        // by calculating ((char*)ops - 4). We must NOT pass a separate wrapper struct;
        // instead, we hook process_capture_result in place in the existing ops struct.
        auto mutable_ops = const_cast<camera3_callback_ops_t *>(callback_ops);
        long page_size = sysconf(_SC_PAGESIZE);
        uintptr_t page_start = ((uintptr_t)&mutable_ops->process_capture_result) & ~(page_size - 1);
        mprotect((void *)page_start, page_size, PROT_READ | PROT_WRITE);
        mutable_ops->process_capture_result = my_process_capture_result;
    }
    if (g_orig_initilizeDevice) {
        return g_orig_initilizeDevice(this_ptr, callback_ops);
    }
    return 0;
}

static int my_processCaptureRequest(void *this_ptr, camera3_capture_request_t *request) {
    if (this_ptr && request) {
        int cam_id = *(int *)((char *)this_ptr + 136);
        if (cam_id == 1) {
            int precapture_trigger = -1;
            int capture_intent = -1;

            if (request->settings) {
                camera_metadata_ro_entry_t entry;
                if (find_camera_metadata_ro_entry(request->settings, ANDROID_FLASH_MODE, &entry) == 0) {
                    s_cam1_flash_mode = entry.data.u8[0];
                }
                if (find_camera_metadata_ro_entry(request->settings, ANDROID_CONTROL_AE_MODE, &entry) == 0) {
                    s_cam1_ae_mode = entry.data.u8[0];
                }
                if (find_camera_metadata_ro_entry(request->settings, ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &entry) == 0) {
                    precapture_trigger = entry.data.u8[0];
                }
                if (find_camera_metadata_ro_entry(request->settings, ANDROID_CONTROL_CAPTURE_INTENT, &entry) == 0) {
                    capture_intent = entry.data.u8[0];
                }

                ALOGI("my_processCaptureRequest(cam1): req=%u, num_out=%u, flash_mode=%d, ae_mode=%d, precapture=%d, intent=%d",
                      request->frame_number, request->num_output_buffers, s_cam1_flash_mode, s_cam1_ae_mode, precapture_trigger, capture_intent);
            }

            // Check if this request is a still capture
            bool is_capture = false;
            if (capture_intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) {
                is_capture = true;
            } else if (capture_intent != ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD &&
                       capture_intent != ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW) {
                for (uint32_t i = 0; i < request->num_output_buffers; i++) {
                    camera3_stream_t *s = request->output_buffers[i].stream;
                    if (!s) continue;
                    if (s == s_cam1_picture_stream || s->format == HAL_PIXEL_FORMAT_BLOB) {
                        is_capture = true;
                        break;
                    }
                }
            }

            int64_t now_ms = get_time_ms();

            bool flash_allowed = (s_cam1_ae_mode == ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH ||
                                  s_cam1_ae_mode == ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH ||
                                  s_cam1_flash_mode == ANDROID_FLASH_MODE_SINGLE);

            // 1. TORCH mode (e.g. video or explicit torch)
            if (s_cam1_flash_mode == ANDROID_FLASH_MODE_TORCH) {
                s_cam1_torch_active = true;
                set_front_flash(1);
                s_cam1_flash_firing = 1;
            } else if (s_cam1_torch_active && s_cam1_flash_mode == ANDROID_FLASH_MODE_OFF) {
                s_cam1_torch_active = false;
                if (!s_cam1_precapture_active && !s_cam1_still_capture_in_flight) {
                    set_front_flash(0);
                    s_cam1_flash_firing = 0;
                }
            }

            // 2. PRECAPTURE TRIGGER
            if (precapture_trigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START && flash_allowed) {
                s_cam1_precapture_active = true;
                s_cam1_precapture_trigger_frame = request->frame_number;
                s_cam1_precapture_frames_seen = 0;
                s_cam1_capture_time_ms = now_ms;
                s_cam1_flash_firing = 2;
                set_front_flash(1);
                ALOGI("my_processCaptureRequest(cam1): precapture trigger at frame %u! Turning front flash ON",
                      request->frame_number);

                pthread_t tid;
                pthread_create(&tid, nullptr, front_flash_watchdog, nullptr);
                pthread_detach(tid);
            } else if (precapture_trigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_CANCEL) {
                s_cam1_precapture_active = false;
                if (!s_cam1_torch_active && !s_cam1_still_capture_in_flight) {
                    set_front_flash(0);
                    s_cam1_flash_firing = 0;
                    ALOGI("my_processCaptureRequest(cam1): precapture cancelled, front flash OFF");
                }
            }

            // 3. STILL CAPTURE
            if (is_capture) {
                s_cam1_still_capture_in_flight = true;
                s_cam1_still_capture_frame = request->frame_number;
                s_cam1_precapture_active = false; // capture has been submitted

                if (flash_allowed || s_cam1_flash_firing == 2) {
                    set_front_flash(1);
                    s_cam1_flash_firing = 2;
                    s_cam1_capture_time_ms = now_ms;
                    ALOGI("my_processCaptureRequest(cam1): STILL CAPTURE at frame %u! Firing front flash",
                          request->frame_number);

                    pthread_t tid;
                    pthread_create(&tid, nullptr, front_flash_watchdog, nullptr);
                    pthread_detach(tid);
                }
            }

            // 4. SANITIZE SETTINGS FOR SAMSUNG HAL (cam 1 only)
            // Samsung's proprietary HAL tries to execute ExynosCameraActivityFlash for camera 1
            // if it receives PRECAPTURE_TRIGGER_START or flash modes. Because camera 1 is fixed-focus
            // (afState is always 0), the Samsung HAL's flash state machine gets permanently stuck in
            // FLASH_STATUS_PRE_AE_DONE, overwriting AE_STATE with PRECAPTURE on every subsequent frame.
            // By masking out precapture trigger and mapping flash modes to ON/OFF for camera 1,
            // Samsung HAL operates in clean auto-exposure mode while our shim handles the front LED torch.
            if (request->settings) {
                camera_metadata_t *req_meta = const_cast<camera_metadata_t *>(request->settings);
                camera_metadata_entry_t entry;

                // Clear PRECAPTURE_TRIGGER so Samsung HAL does not start FLASH_STEP_PRE_START
                if (find_camera_metadata_entry(req_meta, ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &entry) == 0) {
                    if (entry.data.u8[0] == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START) {
                        uint8_t idle = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
                        update_camera_metadata_entry(req_meta, entry.index, &idle, 1, nullptr);
                    }
                }

                // Map flash AE modes to normal AE ON so Samsung HAL does not trigger flash management
                if (find_camera_metadata_entry(req_meta, ANDROID_CONTROL_AE_MODE, &entry) == 0) {
                    if (entry.data.u8[0] == ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH ||
                        entry.data.u8[0] == ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH) {
                        uint8_t on = ANDROID_CONTROL_AE_MODE_ON;
                        update_camera_metadata_entry(req_meta, entry.index, &on, 1, nullptr);
                    }
                }

                // Clear TORCH/SINGLE flash modes so Samsung HAL does not attempt ISP rear flash
                if (find_camera_metadata_entry(req_meta, ANDROID_FLASH_MODE, &entry) == 0) {
                    if (entry.data.u8[0] == ANDROID_FLASH_MODE_TORCH ||
                        entry.data.u8[0] == ANDROID_FLASH_MODE_SINGLE) {
                        uint8_t off = ANDROID_FLASH_MODE_OFF;
                        update_camera_metadata_entry(req_meta, entry.index, &off, 1, nullptr);
                    }
                }
            }
        }
    }

    if (!g_orig_processCaptureRequest) {
        void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
        if (h) {
            g_orig_processCaptureRequest = (orig_processCaptureRequest_t)dlsym(
                h, "_ZN7android13ExynosCamera321processCaptureRequestEP23camera3_capture_request");
        }
    }

    if (g_orig_processCaptureRequest) {
        return g_orig_processCaptureRequest(this_ptr, request);
    }
    return -ENOSYS;
}

static int my_releaseDevice(void *this_ptr) {
    if (this_ptr) {
        int cam_id = *(int *)((char *)this_ptr + 136);
        if (cam_id == 1) {
            ALOGI("my_releaseDevice: cleaning up front flash");
            set_front_flash(0);
            s_cam1_flash_firing = 0;
            s_cam1_torch_active = false;
            s_cam1_precapture_active = false;
            s_cam1_precapture_frames_seen = 0;
            s_cam1_still_capture_in_flight = false;
        }
        stream_allocator_on_release_device(cam_id);
    }
    if (!g_orig_releaseDevice) {
        void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
        if (h) {
            g_orig_releaseDevice = (orig_releaseDevice_t)dlsym(
                h, "_ZN7android13ExynosCamera313releaseDeviceEv");
        }
    }
    if (g_orig_releaseDevice) {
        return g_orig_releaseDevice(this_ptr);
    }
    return 0;
}

void front_flash_on_configure_streams(int cameraId, camera3_stream_configuration_t *stream_list) {
    if (cameraId != 1) return;

    set_front_flash(0);
    s_cam1_flash_firing = 0;
    s_cam1_torch_active = false;
    s_cam1_precapture_active = false;
    s_cam1_precapture_frames_seen = 0;
    s_cam1_still_capture_in_flight = false;

    s_cam1_picture_stream = nullptr;
    s_cam1_preview_stream = nullptr;

    if (!stream_list) return;

    for (uint32_t i = 0; i < stream_list->num_streams; i++) {
        camera3_stream_t *s = stream_list->streams[i];
        if (!s) continue;

        if (s->format == HAL_PIXEL_FORMAT_BLOB) {
            s_cam1_picture_stream = s;
        } else if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
            if (!s_cam1_preview_stream || (s->usage & (GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE))) {
                s_cam1_preview_stream = s;
            }
        }
    }

    if (!s_cam1_preview_stream) {
        for (uint32_t i = 0; i < stream_list->num_streams; i++) {
            camera3_stream_t *s = stream_list->streams[i];
            if (s && s != s_cam1_picture_stream) {
                s_cam1_preview_stream = s;
                break;
            }
        }
    }

    ALOGI("front_flash_on_configure_streams: picture_stream=%p, preview_stream=%p",
          s_cam1_picture_stream, s_cam1_preview_stream);
}

static bool patch_got_entry(void **got_entry, void *new_func, void **orig_func) {
    if (*got_entry != new_func) {
        if (orig_func && !*orig_func) {
            *orig_func = *got_entry;
        }
        long page_size = sysconf(_SC_PAGESIZE);
        uintptr_t page_start = ((uintptr_t)got_entry) & ~(page_size - 1);
        if (mprotect((void *)page_start, page_size, PROT_READ | PROT_WRITE) == 0) {
            *got_entry = new_func;
            mprotect((void *)page_start, page_size, PROT_READ);
            return true;
        } else {
            ALOGE("patch_got_entry: Failed to mprotect GOT entry at %p: %s", got_entry, strerror(errno));
            return false;
        }
    }
    return true;
}

void front_flash_hook_got(const char *name, void **got_entry) {
    if (strcmp(name, "_ZN7android30ExynosCamera3MetadataConverter19constructStaticInfoEiPP15camera_metadata") == 0) {
        if (patch_got_entry(got_entry, (void *)my_constructStaticInfo, (void **)&g_orig_constructStaticInfo)) {
            ALOGI("Successfully hooked constructStaticInfo to %p (orig=%p)",
                  my_constructStaticInfo, g_orig_constructStaticInfo);
        }
    } else if (strcmp(name, "_ZN7android13ExynosCamera315initilizeDeviceEPK20camera3_callback_ops") == 0) {
        if (patch_got_entry(got_entry, (void *)my_initilizeDevice, (void **)&g_orig_initilizeDevice)) {
            ALOGI("Successfully hooked initilizeDevice to %p (orig=%p)",
                  my_initilizeDevice, g_orig_initilizeDevice);
        }
    } else if (strcmp(name, "_ZN7android13ExynosCamera321processCaptureRequestEP23camera3_capture_request") == 0) {
        if (patch_got_entry(got_entry, (void *)my_processCaptureRequest, (void **)&g_orig_processCaptureRequest)) {
            ALOGI("Successfully hooked processCaptureRequest to %p (orig=%p)",
                  my_processCaptureRequest, g_orig_processCaptureRequest);
        }
    } else if (strcmp(name, "_ZN7android13ExynosCamera313releaseDeviceEv") == 0) {
        if (patch_got_entry(got_entry, (void *)my_releaseDevice, (void **)&g_orig_releaseDevice)) {
            ALOGI("Successfully hooked releaseDevice to %p (orig=%p)",
                  my_releaseDevice, g_orig_releaseDevice);
        }
    }
}
