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

#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>
#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <cutils/native_handle.h>
#include <log/log.h>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>
#include <system/camera_metadata.h>

#include <android/fdsan.h>

#undef LOG_TAG
#define LOG_TAG "ExynosCameraShimHook"

#include <time.h>

typedef int (*orig_configureStreams_t)(void *this_ptr, camera3_stream_configuration_t *stream_list);
static orig_configureStreams_t g_orig_configureStreams = nullptr;

typedef int (*orig_processCaptureRequest_t)(void *this_ptr, camera3_capture_request_t *request);
static orig_processCaptureRequest_t g_orig_processCaptureRequest = nullptr;

typedef int (*orig_releaseDevice_t)(void *this_ptr);
static orig_releaseDevice_t g_orig_releaseDevice = nullptr;

typedef int (*orig_constructStaticInfo_t)(int cameraId, camera_metadata_t **info);
static orig_constructStaticInfo_t g_orig_constructStaticInfo = nullptr;

typedef void *(*fn_getActivityControl_t)(void *params);
typedef void *(*fn_getFlashMgr_t)(void *actCtrl);
typedef void (*fn_setFlashStep_t)(void *flashMgr, int step);
typedef void (*fn_setFlashStatus_t)(void *flashMgr, int status);

static fn_getActivityControl_t fn_getActivityControl = nullptr;
static fn_getFlashMgr_t fn_getFlashMgr = nullptr;
static fn_setFlashStep_t fn_setFlashStep = nullptr;
static fn_setFlashStatus_t fn_setFlashStatus = nullptr;

static bool s_cam0_had_still_capture = false;
static camera_metadata_t *s_cam0_last_preview_meta = nullptr;

static void reset_cam0_flash(void *this_ptr) {
    if (!this_ptr) return;
    void *params = *(void **)((char *)this_ptr + 12);
    if (!params) return;
    if (!fn_getActivityControl) {
        void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
        if (h) {
            fn_getActivityControl = (fn_getActivityControl_t)dlsym(h, "_ZN7android23ExynosCamera3Parameters18getActivityControlEv");
            fn_getFlashMgr = (fn_getFlashMgr_t)dlsym(h, "_ZN7android27ExynosCameraActivityControl11getFlashMgrEv");
            fn_setFlashStep = (fn_setFlashStep_t)dlsym(h, "_ZN7android25ExynosCameraActivityFlash12setFlashStepENS0_10FLASH_STEPE");
            fn_setFlashStatus = (fn_setFlashStatus_t)dlsym(h, "_ZN7android25ExynosCameraActivityFlash14setFlashStatusENS0_12FLASH_STATUSE");
        }
    }
    if (fn_getActivityControl && fn_getFlashMgr && fn_setFlashStep && fn_setFlashStatus) {
        void *actCtrl = fn_getActivityControl(params);
        if (actCtrl) {
            void *flashMgr = fn_getFlashMgr(actCtrl);
            if (flashMgr) {
                fn_setFlashStep(flashMgr, 0 /* FLASH_STEP_OFF */);
                fn_setFlashStatus(flashMgr, 0 /* FLASH_STATUS_OFF */);
                ALOGI("reset_cam0_flash: successfully reset flashStep and flashStatus to OFF");
            }
        }
    }
}

static int s_cam1_flash_mode = 0;
static int s_cam1_ae_mode = 1;
static bool s_cam1_precapture_active = false;
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
    for (int i = 0; i < 20; i++) {
        usleep(100000); // 100ms
        if (s_cam1_flash_firing != 2) break;
        int64_t elapsed = get_time_ms() - s_cam1_capture_time_ms;
        if (!s_cam1_precapture_active && elapsed >= 300) {
            ALOGI("front_flash_watchdog: capture elapsed %lld ms, turning OFF front flash", (long long)elapsed);
            set_front_flash(0);
            s_cam1_flash_firing = 0;
            break;
        }
        if (elapsed > 2000) {
            ALOGW("front_flash_watchdog: max timeout (2.0s) reached, turning OFF front flash");
            set_front_flash(0);
            s_cam1_flash_firing = 0;
            s_cam1_precapture_active = false;
            break;
        }
    }
    return nullptr;
}

static void patch_front_camera_metadata(camera_metadata_t **info) {
    if (!info || !*info) return;
    camera_metadata_t *old_meta = *info;

    size_t entry_count = get_camera_metadata_entry_count(old_meta);
    size_t data_count = get_camera_metadata_data_count(old_meta);

    camera_metadata_t *new_meta = allocate_camera_metadata(entry_count + 16, data_count + 256);
    if (!new_meta) {
        ALOGE("patch_front_camera_metadata: allocate_camera_metadata failed");
        return;
    }

    if (append_camera_metadata(new_meta, old_meta) != 0) {
        ALOGE("patch_front_camera_metadata: append_camera_metadata failed");
        free_camera_metadata(new_meta);
        return;
    }

    // 1. ANDROID_FLASH_INFO_AVAILABLE = TRUE (1)
    uint8_t flash_avail = ANDROID_FLASH_INFO_AVAILABLE_TRUE;
    camera_metadata_entry_t entry;
    if (find_camera_metadata_entry(new_meta, ANDROID_FLASH_INFO_AVAILABLE, &entry) == 0) {
        update_camera_metadata_entry(new_meta, entry.index, &flash_avail, 1, nullptr);
    } else {
        add_camera_metadata_entry(new_meta, ANDROID_FLASH_INFO_AVAILABLE, &flash_avail, 1);
    }

    // 2. ANDROID_CONTROL_AE_AVAILABLE_MODES
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

    // 3. ANDROID_FLASH_INFO_CHARGE_DURATION
    int64_t charge_duration = 0;
    if (find_camera_metadata_entry(new_meta, ANDROID_FLASH_INFO_CHARGE_DURATION, &entry) == 0) {
        update_camera_metadata_entry(new_meta, entry.index, &charge_duration, 1, nullptr);
    } else {
        add_camera_metadata_entry(new_meta, ANDROID_FLASH_INFO_CHARGE_DURATION, &charge_duration, 1);
    }

    free_camera_metadata(old_meta);
    *info = new_meta;
    ALOGI("patch_front_camera_metadata: successfully injected flash info to front camera metadata!");
}

static int my_constructStaticInfo(int cameraId, camera_metadata_t **info) {
    ALOGI("my_constructStaticInfo: cameraId=%d", cameraId);
    int ret = 0;
    if (g_orig_constructStaticInfo) {
        ret = g_orig_constructStaticInfo(cameraId, info);
    } else {
        void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
        if (h) {
            orig_constructStaticInfo_t fn = (orig_constructStaticInfo_t)dlsym(h, "_ZN7android30ExynosCamera3MetadataConverter19constructStaticInfoEiPP15camera_metadata");
            if (fn) ret = fn(cameraId, info);
        }
    }

    if (ret == 0 && cameraId == 1) {
        patch_front_camera_metadata(info);
    }
    return ret;
}

static int my_processCaptureRequest(void *this_ptr, camera3_capture_request_t *request) {
    if (this_ptr && request) {
        int cam_id = *(int *)((char *)this_ptr + 136);
        if (cam_id == 0) {
            bool is_still_capture = false;
            if (request->settings) {
                camera_metadata_ro_entry_t entry;
                if (find_camera_metadata_ro_entry(request->settings, ANDROID_CONTROL_CAPTURE_INTENT, &entry) == 0) {
                    if (entry.data.u8[0] == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) {
                        is_still_capture = true;
                    }
                }
            }
            if (!is_still_capture) {
                for (uint32_t i = 0; i < request->num_output_buffers; i++) {
                    camera3_stream_t *s = request->output_buffers[i].stream;
                    if (s && s->format == HAL_PIXEL_FORMAT_BLOB) {
                        is_still_capture = true;
                        break;
                    }
                }
            }

            if (is_still_capture) {
                s_cam0_had_still_capture = true;
                ALOGI("my_processCaptureRequest(cam0): still capture frame %u detected", request->frame_number);
            } else {
                if (request->settings) {
                    if (s_cam0_last_preview_meta) {
                        free_camera_metadata(s_cam0_last_preview_meta);
                    }
                    s_cam0_last_preview_meta = clone_camera_metadata(request->settings);
                }

                if (s_cam0_had_still_capture) {
                    ALOGI("my_processCaptureRequest(cam0): first preview frame %u after still capture. Resetting flash and cleaning metadata.",
                          request->frame_number);
                    reset_cam0_flash(this_ptr);

                    bool injected_meta = false;
                    camera_metadata_t *clean_meta = nullptr;
                    if (request->settings == nullptr) {
                        if (s_cam0_last_preview_meta) {
                            request->settings = s_cam0_last_preview_meta;
                            injected_meta = true;
                        } else {
                            clean_meta = allocate_camera_metadata(2, 8);
                            uint8_t intent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
                            add_camera_metadata_entry(clean_meta, ANDROID_CONTROL_CAPTURE_INTENT, &intent, 1);
                            request->settings = clean_meta;
                            injected_meta = true;
                        }
                    }

                    int ret = 0;
                    if (!g_orig_processCaptureRequest) {
                        void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
                        if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
                        if (h) {
                            g_orig_processCaptureRequest = (orig_processCaptureRequest_t)dlsym(h, "_ZN7android13ExynosCamera321processCaptureRequestEP23camera3_capture_request");
                        }
                    }
                    if (g_orig_processCaptureRequest) {
                        ret = g_orig_processCaptureRequest(this_ptr, request);
                    }

                    if (injected_meta) {
                        if (clean_meta) {
                            free_camera_metadata(clean_meta);
                        }
                        request->settings = nullptr;
                    }

                    s_cam0_had_still_capture = false;
                    return ret;
                }
            }
        } else if (cam_id == 1) {
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
                    if (precapture_trigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START) {
                        s_cam1_precapture_active = true;
                    } else if (precapture_trigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_CANCEL) {
                        s_cam1_precapture_active = false;
                    }
                }
                if (find_camera_metadata_ro_entry(request->settings, ANDROID_CONTROL_CAPTURE_INTENT, &entry) == 0) {
                    capture_intent = entry.data.u8[0];
                }

                ALOGI("my_processCaptureRequest(cam1): req=%u, num_out=%u, flash_mode=%d, ae_mode=%d, precapture=%d, intent=%d",
                      request->frame_number, request->num_output_buffers, s_cam1_flash_mode, s_cam1_ae_mode, precapture_trigger, capture_intent);
            }

            // Check if this request is a still capture
            bool is_capture = false;
            for (uint32_t i = 0; i < request->num_output_buffers; i++) {
                camera3_stream_t *s = request->output_buffers[i].stream;
                if (!s) continue;
                if (s == s_cam1_picture_stream ||
                    s->format == HAL_PIXEL_FORMAT_BLOB ||
                    (s_cam1_preview_stream && s != s_cam1_preview_stream)) {
                    is_capture = true;
                    break;
                }
            }
            if (!is_capture && request->num_output_buffers > 1) {
                is_capture = true;
            }

            if (is_capture) {
                s_cam1_precapture_active = false;
            }

            int64_t now_ms = get_time_ms();

            // 1. TORCH mode (e.g. video or explicit torch)
            if (s_cam1_flash_mode == ANDROID_FLASH_MODE_TORCH) {
                set_front_flash(1);
                s_cam1_flash_firing = 1;
            } else if (s_cam1_flash_firing == 1 && s_cam1_flash_mode == ANDROID_FLASH_MODE_OFF) {
                set_front_flash(0);
                s_cam1_flash_firing = 0;
            }

            // 2. STILL CAPTURE / PRECAPTURE with FLASH
            bool flash_allowed = (s_cam1_ae_mode == ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH ||
                                  s_cam1_ae_mode == ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH ||
                                  s_cam1_flash_mode == ANDROID_FLASH_MODE_SINGLE);

            if (s_cam1_flash_firing != 1 && flash_allowed) {
                if (is_capture) {
                    ALOGI("my_processCaptureRequest(cam1): still capture request! Firing front flash");
                    set_front_flash(1);
                    s_cam1_flash_firing = 2;
                    s_cam1_capture_time_ms = now_ms;
                    s_cam1_precapture_active = false;

                    pthread_t tid;
                    pthread_create(&tid, nullptr, front_flash_watchdog, nullptr);
                    pthread_detach(tid);
                } else if (s_cam1_precapture_active) {
                    if (s_cam1_flash_firing != 2) {
                        ALOGI("my_processCaptureRequest(cam1): precapture trigger active! Turning ON front flash");
                        set_front_flash(1);
                        s_cam1_flash_firing = 2;
                        s_cam1_capture_time_ms = now_ms;

                        pthread_t tid;
                        pthread_create(&tid, nullptr, front_flash_watchdog, nullptr);
                        pthread_detach(tid);
                    }
                }
            }

            // 3. Turn off capture flash after capture duration has passed
            if (s_cam1_flash_firing == 2) {
                if (!s_cam1_precapture_active && !is_capture) {
                    // Preview frame arriving after capture
                    if (now_ms - s_cam1_capture_time_ms >= 250) {
                        ALOGI("my_processCaptureRequest(cam1): capture complete (%lld ms elapsed), turning OFF front flash",
                              (long long)(now_ms - s_cam1_capture_time_ms));
                        set_front_flash(0);
                        s_cam1_flash_firing = 0;
                    }
                }
            }
        }
    }

    if (!g_orig_processCaptureRequest) {
        void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
        if (h) {
            g_orig_processCaptureRequest = (orig_processCaptureRequest_t)dlsym(h, "_ZN7android13ExynosCamera321processCaptureRequestEP23camera3_capture_request");
        }
    }

    if (g_orig_processCaptureRequest) {
        return g_orig_processCaptureRequest(this_ptr, request);
    }
    return -ENOSYS;
}

static int my_releaseDevice(void *this_ptr) {
    ALOGI("my_releaseDevice: cleaning up device and front/rear flash");
    set_front_flash(0);
    s_cam1_flash_firing = 0;
    s_cam1_precapture_active = false;
    s_cam0_had_still_capture = false;
    if (s_cam0_last_preview_meta) {
        free_camera_metadata(s_cam0_last_preview_meta);
        s_cam0_last_preview_meta = nullptr;
    }
    if (!g_orig_releaseDevice) {
        void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
        if (h) {
            g_orig_releaseDevice = (orig_releaseDevice_t)dlsym(h, "_ZN7android13ExynosCamera313releaseDeviceEv");
        }
    }
    if (g_orig_releaseDevice) {
        return g_orig_releaseDevice(this_ptr);
    }
    return 0;
}

static int (*fn_checkPreviewSize)(void *params, int w, int h) = nullptr;
static int (*fn_checkPictureSize)(void *params, int w, int h) = nullptr;
static int (*fn_checkVideoSize)(void *params, int w, int h) = nullptr;
static void (*fn_getHwSensorSize)(void *params, int *w, int *h) = nullptr;

static int (*fn_calcPreviewBayerCropSize)(void *this_ptr, void *srcRect, void *dstRect) = nullptr;
static int (*fn_calcPictureBayerCropSize)(void *this_ptr, void *srcRect, void *dstRect) = nullptr;

static inline int get_ratio_id(int w, int h) {
    if (w <= 0 || h <= 0) return 1; // default to 4:3
    float r = (float)w / (float)h;
    if (r < 1.0f) r = 1.0f / r;

    if (r >= 1.65f) {
        return 0; // 16:9
    } else if (r >= 1.42f) {
        return 3; // 3:2
    } else if (r >= 1.15f) {
        return 1; // 4:3
    } else {
        return 2; // 1:1
    }
}

static inline void get_hw_preview_size(int ratio_id, int *hwW, int *hwH) {
    switch (ratio_id) {
    case 0: // 16:9
        *hwW = 1920; *hwH = 1080;
        break;
    case 3: // 3:2
        *hwW = 1056; *hwH = 704;
        break;
    case 1: // 4:3
        *hwW = 1440; *hwH = 1080;
        break;
    case 2: // 1:1
        *hwW = 1080; *hwH = 1080;
        break;
    default:
        *hwW = 1440; *hwH = 1080;
        break;
    }
}

extern "C" int my_getPreviewBayerCropSize(void *this_ptr, void *srcRect, void *dstRect) {
    if (!this_ptr) return -EINVAL;

    int previewW = *(int *)((char *)this_ptr + 32528);
    int previewH = *(int *)((char *)this_ptr + 32532);
    int hwPreviewW = *(int *)((char *)this_ptr + 32648);
    int hwPreviewH = *(int *)((char *)this_ptr + 32652);
    int initRatioId = *(int *)((char *)this_ptr + 32656);

    int checkW = (previewW > 0) ? previewW : hwPreviewW;
    int checkH = (previewH > 0) ? previewH : hwPreviewH;

    ALOGI("my_getPreviewBayerCropSize: enter: preview=%dx%d, hwPreview=%dx%d, initRatioId=%d",
          previewW, previewH, hwPreviewW, hwPreviewH, initRatioId);

    if (checkW > 0 && checkH > 0) {
        int rId = get_ratio_id(checkW, checkH);
        int expW = 0, expH = 0;
        get_hw_preview_size(rId, &expW, &expH);

        *(int *)((char *)this_ptr + 32656) = rId;
        *(int *)((char *)this_ptr + 32648) = expW;
        *(int *)((char *)this_ptr + 32652) = expH;
        *(int *)((char *)this_ptr + 32528) = expW;
        *(int *)((char *)this_ptr + 32532) = expH;

        ALOGI("my_getPreviewBayerCropSize: check=%dx%d -> hwPreview=%dx%d, ratioId=%d",
              checkW, checkH, expW, expH, rId);
    }

    int ret = 0;
    if (fn_calcPreviewBayerCropSize) {
        ret = fn_calcPreviewBayerCropSize(this_ptr, srcRect, dstRect);
    } else {
        ALOGE("my_getPreviewBayerCropSize: fn_calcPreviewBayerCropSize is NULL!");
        return -ENOSYS;
    }

    if (dstRect) {
        int *d = (int *)dstRect;
        ALOGI("my_getPreviewBayerCropSize: result dstRect=(%d, %d, %d, %d, full %dx%d)",
              d[0], d[1], d[2], d[3], d[4], d[5]);
    }

    return ret;
}

extern "C" int my_getPictureBayerCropSize(void *this_ptr, void *srcRect, void *dstRect) {
    if (!this_ptr) return -EINVAL;

    int picW = *(int *)((char *)this_ptr + 32544);
    int picH = *(int *)((char *)this_ptr + 32548);
    int hwPicW = *(int *)((char *)this_ptr + 32660);
    int hwPicH = *(int *)((char *)this_ptr + 32664);

    int checkW = (picW > 0) ? picW : hwPicW;
    int checkH = (picH > 0) ? picH : hwPicH;

    if (checkW > 0 && checkH > 0) {
        int prId = get_ratio_id(checkW, checkH);
        *(int *)((char *)this_ptr + 32668) = prId;
        ALOGI("my_getPictureBayerCropSize: check=%dx%d -> ratioId=%d", checkW, checkH, prId);
    }

    int ret = 0;
    if (fn_calcPictureBayerCropSize) {
        ret = fn_calcPictureBayerCropSize(this_ptr, srcRect, dstRect);
    } else {
        ALOGE("my_getPictureBayerCropSize: fn_calcPictureBayerCropSize is NULL!");
        return -ENOSYS;
    }

    if (dstRect) {
        int *d = (int *)dstRect;
        ALOGI("my_getPictureBayerCropSize: result dstRect=(%d, %d, %d, %d, full %dx%d)",
              d[0], d[1], d[2], d[3], d[4], d[5]);
    }

    return ret;
}

static void init_parameter_funcs() {
    static bool initialized = false;
    if (initialized) return;

    void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
    if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
    if (h) {
        fn_checkPreviewSize = (int (*)(void *, int, int))dlsym(h, "_ZN7android23ExynosCamera3Parameters16checkPreviewSizeEii");
        fn_checkPictureSize = (int (*)(void *, int, int))dlsym(h, "_ZN7android23ExynosCamera3Parameters16checkPictureSizeEii");
        fn_checkVideoSize = (int (*)(void *, int, int))dlsym(h, "_ZN7android23ExynosCamera3Parameters14checkVideoSizeEii");
        fn_getHwSensorSize = (void (*)(void *, int *, int *))dlsym(h, "_ZN7android23ExynosCamera3Parameters15getHwSensorSizeEPiS1_");
        initialized = true;
        ALOGI("%s: resolved funcs: preview=%p, picture=%p, video=%p, sensorSize=%p",
              __FUNCTION__, fn_checkPreviewSize, fn_checkPictureSize, fn_checkVideoSize, fn_getHwSensorSize);
    } else {
        ALOGE("%s: failed to dlopen libexynoscamera3.so: %s", __FUNCTION__, dlerror());
    }
}

extern "C" int my_configureStreams(void *this_ptr, camera3_stream_configuration_t *stream_list) {
    if (!this_ptr || !stream_list) {
        if (g_orig_configureStreams) {
            return g_orig_configureStreams(this_ptr, stream_list);
        }
        return -EINVAL;
    }

    void *m_parameters = *(void **)((char *)this_ptr + 12);
    int m_cameraId = *(int *)((char *)this_ptr + 136);

    ALOGI("my_configureStreams: cam_id=%d, this=%p, params=%p, num_streams=%u",
          m_cameraId, this_ptr, m_parameters, stream_list->num_streams);

    if (m_cameraId == 1) {
        set_front_flash(0);
        s_cam1_flash_firing = 0;
        s_cam1_precapture_active = false;
    } else if (m_cameraId == 0) {
        s_cam0_had_still_capture = false;
        reset_cam0_flash(this_ptr);
        if (s_cam0_last_preview_meta) {
            free_camera_metadata(s_cam0_last_preview_meta);
            s_cam0_last_preview_meta = nullptr;
        }
    }

    camera3_stream_t *preview_stream = nullptr;
    camera3_stream_t *picture_stream = nullptr;
    camera3_stream_t *video_stream = nullptr;

    for (uint32_t i = 0; i < stream_list->num_streams; i++) {
        camera3_stream_t *s = stream_list->streams[i];
        if (!s) continue;

        ALOGI("my_configureStreams: stream[%u]: %ux%u fmt=0x%x usage=0x%x type=%d",
              i, s->width, s->height, s->format, s->usage, s->stream_type);

        if (s->usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
            video_stream = s;
        }

        if (s->format == HAL_PIXEL_FORMAT_BLOB) {
            picture_stream = s;
        } else if (s->format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
            if (!picture_stream || (s->width * s->height > picture_stream->width * picture_stream->height)) {
                picture_stream = s;
            }
        } else if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
            if (!preview_stream || (s->usage & (GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE))) {
                preview_stream = s;
            }
        }
    }

    if (!preview_stream) {
        for (uint32_t i = 0; i < stream_list->num_streams; i++) {
            camera3_stream_t *s = stream_list->streams[i];
            if (s && s != picture_stream) {
                preview_stream = s;
                break;
            }
        }
    }

    if (!preview_stream && stream_list->num_streams == 1 &&
        stream_list->streams[0]->format != HAL_PIXEL_FORMAT_BLOB) {
        preview_stream = stream_list->streams[0];
        picture_stream = nullptr;
    }

    if (m_cameraId == 1) {
        s_cam1_picture_stream = picture_stream;
        s_cam1_preview_stream = preview_stream;
        ALOGI("my_configureStreams(cam1): picture_stream=%p, preview_stream=%p",
              s_cam1_picture_stream, s_cam1_preview_stream);
    }

    if (m_parameters) {
        init_parameter_funcs();

        if (preview_stream) {
            int ratioId = get_ratio_id(preview_stream->width, preview_stream->height);
            int hwW = 0, hwH = 0;
            get_hw_preview_size(ratioId, &hwW, &hwH);

            *(int *)((char *)m_parameters + 32528) = hwW;
            *(int *)((char *)m_parameters + 32532) = hwH;
            *(int *)((char *)m_parameters + 32648) = hwW;
            *(int *)((char *)m_parameters + 32652) = hwH;
            *(int *)((char *)m_parameters + 32656) = ratioId;

            if (fn_checkPreviewSize) {
                ALOGI("my_configureStreams: applying checkPreviewSize(%u, %u)",
                      preview_stream->width, preview_stream->height);
                int ret = fn_checkPreviewSize(m_parameters, preview_stream->width, preview_stream->height);
                ALOGI("my_configureStreams: checkPreviewSize returned %d", ret);
            }

            // Restore calibrated hw sizes after checkPreviewSize (which resets them to 1920x1080)
            *(int *)((char *)m_parameters + 32528) = hwW;
            *(int *)((char *)m_parameters + 32532) = hwH;
            *(int *)((char *)m_parameters + 32648) = hwW;
            *(int *)((char *)m_parameters + 32652) = hwH;
            *(int *)((char *)m_parameters + 32656) = ratioId;
        }

        if (picture_stream) {
            if (fn_checkPictureSize) {
                ALOGI("my_configureStreams: applying checkPictureSize(%u, %u)",
                      picture_stream->width, picture_stream->height);
                int ret = fn_checkPictureSize(m_parameters, picture_stream->width, picture_stream->height);
                ALOGI("my_configureStreams: checkPictureSize returned %d", ret);
            }

            int pratioId = get_ratio_id(picture_stream->width, picture_stream->height);
            *(int *)((char *)m_parameters + 32544) = picture_stream->width;
            *(int *)((char *)m_parameters + 32548) = picture_stream->height;
            *(int *)((char *)m_parameters + 32660) = picture_stream->width;
            *(int *)((char *)m_parameters + 32664) = picture_stream->height;
            *(int *)((char *)m_parameters + 32668) = pratioId;
        } else if (fn_getHwSensorSize && fn_checkPictureSize) {
            int sw = 0, sh = 0;
            fn_getHwSensorSize(m_parameters, &sw, &sh);
            if (sw > 0 && sh > 0) {
                ALOGI("my_configureStreams: no picture stream, setting sensor size (%d, %d)", sw, sh);
                fn_checkPictureSize(m_parameters, sw, sh);
            }
        }

        if (video_stream && fn_checkVideoSize) {
            ALOGI("my_configureStreams: applying checkVideoSize(%u, %u)",
                  video_stream->width, video_stream->height);
            fn_checkVideoSize(m_parameters, video_stream->width, video_stream->height);
        }
    }

    if (!g_orig_configureStreams) {
        void *h = dlopen("libexynoscamera3.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libexynoscamera3.so", RTLD_NOW);
        if (h) {
            g_orig_configureStreams = (orig_configureStreams_t)dlsym(h, "_ZN7android13ExynosCamera316configureStreamsEP28camera3_stream_configuration");
        }
    }

    if (g_orig_configureStreams) {
        return g_orig_configureStreams(this_ptr, stream_list);
    }

    ALOGE("my_configureStreams: g_orig_configureStreams is NULL!");
    return -ENOSYS;
}

static inline uintptr_t resolve_ptr(Elf32_Addr base, Elf32_Addr ptr) {
    if (ptr < base) {
        return base + ptr;
    }
    return ptr;
}

static bool hook_got_for_camera_lib(struct dl_phdr_info *info) {
    if (!info->dlpi_name) return false;
    if (!strstr(info->dlpi_name, "camera.") && !strstr(info->dlpi_name, "camera/")) {
        return false;
    }

    Elf32_Addr base = info->dlpi_addr;
    const Elf32_Phdr *phdr = info->dlpi_phdr;
    Elf32_Half phnum = info->dlpi_phnum;

    const Elf32_Dyn *dyn = nullptr;
    for (int i = 0; i < phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn = (const Elf32_Dyn *)resolve_ptr(base, phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return false;

    const Elf32_Rel *rel = nullptr;
    size_t relsz = 0;
    const Elf32_Sym *symtab = nullptr;
    const char *strtab = nullptr;

    for (const Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        if (d->d_tag == DT_JMPREL) {
            rel = (const Elf32_Rel *)resolve_ptr(base, d->d_un.d_ptr);
        } else if (d->d_tag == DT_PLTRELSZ) {
            relsz = d->d_un.d_val;
        } else if (d->d_tag == DT_SYMTAB) {
            symtab = (const Elf32_Sym *)resolve_ptr(base, d->d_un.d_ptr);
        } else if (d->d_tag == DT_STRTAB) {
            strtab = (const char *)resolve_ptr(base, d->d_un.d_ptr);
        }
    }

    if (!rel || !symtab || !strtab) return false;

    bool hooked = false;
    size_t rel_count = relsz / sizeof(Elf32_Rel);
    for (size_t i = 0; i < rel_count; i++) {
        uint32_t sym_idx = ELF32_R_SYM(rel[i].r_info);
        const char *name = strtab + symtab[sym_idx].st_name;
        if (strcmp(name, "_ZN7android13ExynosCamera316configureStreamsEP28camera3_stream_configuration") == 0) {
            void **got_entry = (void **)(base + rel[i].r_offset);
            ALOGI("Found configureStreams GOT entry in %s at %p (value=%p)",
                  info->dlpi_name, got_entry, *got_entry);

            if (*got_entry != (void *)my_configureStreams) {
                if (!g_orig_configureStreams) {
                    g_orig_configureStreams = (orig_configureStreams_t)*got_entry;
                }
                long page_size = sysconf(_SC_PAGESIZE);
                uintptr_t page_start = ((uintptr_t)got_entry) & ~(page_size - 1);
                if (mprotect((void *)page_start, page_size, PROT_READ | PROT_WRITE) == 0) {
                    *got_entry = (void *)my_configureStreams;
                    mprotect((void *)page_start, page_size, PROT_READ);
                    ALOGI("Successfully hooked configureStreams in %s to %p (orig=%p)",
                      info->dlpi_name, my_configureStreams, g_orig_configureStreams);
                    hooked = true;
                } else {
                    ALOGE("Failed to mprotect GOT entry at %p: %s", got_entry, strerror(errno));
                }
            } else {
                hooked = true;
            }
        } else if (strcmp(name, "_ZN7android30ExynosCamera3MetadataConverter19constructStaticInfoEiPP15camera_metadata") == 0) {
            void **got_entry = (void **)(base + rel[i].r_offset);
            ALOGI("Found constructStaticInfo GOT entry in %s at %p (value=%p)",
                  info->dlpi_name, got_entry, *got_entry);

            if (*got_entry != (void *)my_constructStaticInfo) {
                if (!g_orig_constructStaticInfo) {
                    g_orig_constructStaticInfo = (orig_constructStaticInfo_t)*got_entry;
                }
                long page_size = sysconf(_SC_PAGESIZE);
                uintptr_t page_start = ((uintptr_t)got_entry) & ~(page_size - 1);
                if (mprotect((void *)page_start, page_size, PROT_READ | PROT_WRITE) == 0) {
                    *got_entry = (void *)my_constructStaticInfo;
                    mprotect((void *)page_start, page_size, PROT_READ);
                    ALOGI("Successfully hooked constructStaticInfo in %s to %p (orig=%p)",
                          info->dlpi_name, my_constructStaticInfo, g_orig_constructStaticInfo);
                    hooked = true;
                } else {
                    ALOGE("Failed to mprotect GOT entry at %p: %s", got_entry, strerror(errno));
                }
            } else {
                hooked = true;
            }
        } else if (strcmp(name, "_ZN7android13ExynosCamera321processCaptureRequestEP23camera3_capture_request") == 0) {
            void **got_entry = (void **)(base + rel[i].r_offset);
            ALOGI("Found processCaptureRequest GOT entry in %s at %p (value=%p)",
                  info->dlpi_name, got_entry, *got_entry);

            if (*got_entry != (void *)my_processCaptureRequest) {
                if (!g_orig_processCaptureRequest) {
                    g_orig_processCaptureRequest = (orig_processCaptureRequest_t)*got_entry;
                }
                long page_size = sysconf(_SC_PAGESIZE);
                uintptr_t page_start = ((uintptr_t)got_entry) & ~(page_size - 1);
                if (mprotect((void *)page_start, page_size, PROT_READ | PROT_WRITE) == 0) {
                    *got_entry = (void *)my_processCaptureRequest;
                    mprotect((void *)page_start, page_size, PROT_READ);
                    ALOGI("Successfully hooked processCaptureRequest in %s to %p (orig=%p)",
                          info->dlpi_name, my_processCaptureRequest, g_orig_processCaptureRequest);
                    hooked = true;
                } else {
                    ALOGE("Failed to mprotect GOT entry at %p: %s", got_entry, strerror(errno));
                }
            } else {
                hooked = true;
            }
        } else if (strcmp(name, "_ZN7android13ExynosCamera313releaseDeviceEv") == 0) {
            void **got_entry = (void **)(base + rel[i].r_offset);
            ALOGI("Found releaseDevice GOT entry in %s at %p (value=%p)",
                  info->dlpi_name, got_entry, *got_entry);

            if (*got_entry != (void *)my_releaseDevice) {
                if (!g_orig_releaseDevice) {
                    g_orig_releaseDevice = (orig_releaseDevice_t)*got_entry;
                }
                long page_size = sysconf(_SC_PAGESIZE);
                uintptr_t page_start = ((uintptr_t)got_entry) & ~(page_size - 1);
                if (mprotect((void *)page_start, page_size, PROT_READ | PROT_WRITE) == 0) {
                    *got_entry = (void *)my_releaseDevice;
                    mprotect((void *)page_start, page_size, PROT_READ);
                    ALOGI("Successfully hooked releaseDevice in %s to %p (orig=%p)",
                          info->dlpi_name, my_releaseDevice, g_orig_releaseDevice);
                    hooked = true;
                } else {
                    ALOGE("Failed to mprotect GOT entry at %p: %s", got_entry, strerror(errno));
                }
            } else {
                hooked = true;
            }
        }
    }
    return hooked;
}

static int phdr_callback(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    (void)data;
    hook_got_for_camera_lib(info);
    return 0;
}

extern "C" int my_stream_allocator_lock(
        void *this_ptr,
        buffer_handle_t **bufHandle,
        int fd[],
        char *addr[],
        bool *isLocked,
        int planeCount)
{
    if (!this_ptr || !bufHandle || !*bufHandle || !**bufHandle) {
        ALOGE("%s: invalid argument(s): this_ptr=%p, bufHandle=%p",
              __FUNCTION__, this_ptr, bufHandle);
        return -EINVAL;
    }

    buffer_handle_t handle = **bufHandle;

    camera3_stream_t *stream = *(camera3_stream_t **)((char *)this_ptr + 4);

    uint32_t width = stream ? stream->width : 0;
    uint32_t height = stream ? stream->height : 0;
    int format = stream ? stream->format : 0;
    uint32_t usage = stream ? stream->usage : 0;

    ALOGI("%s: handle=%p, w=%u, h=%u, fmt=0x%x, usage=0x%x, planes=%d, numFds=%d",
          __FUNCTION__, handle, width, height, format, usage, planeCount, handle->numFds);

    int numFds = handle->numFds;
    for (int i = 0; i < planeCount; i++) {
        if (i < numFds) {
            fd[i] = handle->data[i];
        } else if (numFds > 0) {
            fd[i] = handle->data[0];
        } else {
            fd[i] = -1;
        }
    }

    for (int i = 0; i < planeCount; i++) {
        addr[i] = nullptr;
    }

    android::Rect bounds(width, height);
    uint32_t lockUsage = usage | GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;
    android::status_t lockErr = android::NO_ERROR;

    if (format != HAL_PIXEL_FORMAT_YCbCr_420_888 && (format == HAL_PIXEL_FORMAT_BLOB || planeCount == 1)) {
        void *vaddr = nullptr;
        lockErr = android::GraphicBufferMapper::get().lock(handle, lockUsage, bounds, &vaddr);
        if (lockErr != android::NO_ERROR) {
            lockErr = android::GraphicBufferMapper::get().lock(handle, usage, bounds, &vaddr);
        }
        if (lockErr == android::NO_ERROR) {
            addr[0] = static_cast<char *>(vaddr);
        } else {
            ALOGW("%s: GraphicBufferMapper::lock failed: %d", __FUNCTION__, lockErr);
        }
    } else {
        android_ycbcr ycbcr = {};
        lockErr = android::GraphicBufferMapper::get().lockYCbCr(handle, lockUsage, bounds, &ycbcr);
        if (lockErr != android::NO_ERROR) {
            lockErr = android::GraphicBufferMapper::get().lockYCbCr(handle, usage, bounds, &ycbcr);
        }
        if (lockErr == android::NO_ERROR) {
            addr[0] = static_cast<char *>(ycbcr.y);
            if (planeCount > 1) addr[1] = static_cast<char *>(ycbcr.cb);
            if (planeCount > 2) addr[2] = static_cast<char *>(ycbcr.cr);
        } else {
            ALOGW("%s: GraphicBufferMapper::lockYCbCr failed (%d), fallback to lock()",
                  __FUNCTION__, lockErr);
            void *vaddr = nullptr;
            lockErr = android::GraphicBufferMapper::get().lock(handle, lockUsage, bounds, &vaddr);
            if (lockErr == android::NO_ERROR) {
                addr[0] = static_cast<char *>(vaddr);
            } else {
                ALOGW("%s: fallback lock failed: %d", __FUNCTION__, lockErr);
            }
        }
    }

    if (isLocked) {
        *isLocked = true;
    }

    ALOGD("%s: success. fd[0]=%d, addr[0]=%p", __FUNCTION__, fd[0], addr[0]);
    return 0;
}

extern "C" int my_stream_allocator_unlock(
        void *this_ptr,
        buffer_handle_t *bufHandle)
{
    (void)this_ptr;
    if (!bufHandle || !*bufHandle) {
        return 0;
    }

    android::status_t err = android::GraphicBufferMapper::get().unlock(*bufHandle);
    if (err != android::NO_ERROR) {
        ALOGW("%s: GraphicBufferMapper::unlock failed: %d", __FUNCTION__, err);
    }

    return 0;
}

static bool hook_thumb_function(void *func_ptr, void *hook_dest) {
    if (!func_ptr || !hook_dest) return false;

    uintptr_t addr = ((uintptr_t)func_ptr) & ~1;
    uintptr_t dest = (uintptr_t)hook_dest;

    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page_start = addr & ~(page_size - 1);
    size_t len = (addr + 10) - page_start;

    if (mprotect((void *)page_start, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        ALOGE("%s: mprotect failed at %p: %s", __FUNCTION__, (void *)page_start, strerror(errno));
        return false;
    }

    uint16_t imm16_low = dest & 0xffff;
    uint16_t imm16_high = (dest >> 16) & 0xffff;

    uint16_t movw_hw1 = 0xf240 | (((imm16_low >> 11) & 0x1) << 10) | ((imm16_low >> 12) & 0xf);
    uint16_t movw_hw2 = (((imm16_low >> 8) & 0x7) << 12) | 0x0c00 | (imm16_low & 0xff);

    uint16_t movt_hw1 = 0xf2c0 | (((imm16_high >> 11) & 0x1) << 10) | ((imm16_high >> 12) & 0xf);
    uint16_t movt_hw2 = (((imm16_high >> 8) & 0x7) << 12) | 0x0c00 | (imm16_high & 0xff);

    uint16_t bx_r12 = 0x4760;

    uint16_t *p = (uint16_t *)addr;
    p[0] = movw_hw1;
    p[1] = movw_hw2;
    p[2] = movt_hw1;
    p[3] = movt_hw2;
    p[4] = bx_r12;

    __builtin___clear_cache((char *)addr, (char *)(addr + 10));
    mprotect((void *)page_start, len, PROT_READ | PROT_EXEC);
    return true;
}

static void patch_validate_at(void *unlock_func) {
    if (!unlock_func) return;

    uintptr_t unlock_addr = ((uintptr_t)unlock_func) & ~1;
    uintptr_t validate_addr = unlock_addr + 0x64;

    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page_start = validate_addr & ~(page_size - 1);
    size_t len = (validate_addr + 16) - page_start;

    uint16_t *v = (uint16_t *)validate_addr;
    if (v[0] == 0x4603 && v[1] == 0xb183) {
        if (mprotect((void *)page_start, len, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            v[0] = 0x2000; // movs r0, #0
            v[1] = 0x4770; // bx lr
            v[2] = 0xbf00; // nop
            v[3] = 0xbf00; // nop

            __builtin___clear_cache((char *)validate_addr, (char *)(validate_addr + 16));
            mprotect((void *)page_start, len, PROT_READ | PROT_EXEC);
            ALOGI("Successfully patched private_handle_t::validate at %p", (void *)validate_addr);
        } else {
            ALOGE("mprotect failed for validate at %p: %s", (void *)page_start, strerror(errno));
        }
    } else {
        ALOGW("validate pattern mismatch at %p: 0x%04x 0x%04x", (void *)validate_addr, v[0], v[1]);
    }
}

static void hook_library(const char *lib_name) {
    void *handle = dlopen(lib_name, RTLD_NOLOAD);
    if (!handle) {
        handle = dlopen(lib_name, RTLD_NOW);
    }
    if (!handle) {
        ALOGD("hook_library: %s not loaded yet (%s)", lib_name, dlerror());
        return;
    }

    const char *lock_sym_name = "_ZN7android27ExynosCameraStreamAllocator4lockEPPPK13native_handlePiPPcPbi";
    const char *unlock_sym_name = "_ZN7android27ExynosCameraStreamAllocator6unlockEPPK13native_handle";

    void *lock_fn = dlsym(handle, lock_sym_name);
    void *unlock_fn = dlsym(handle, unlock_sym_name);

    if (lock_fn) {
        if (hook_thumb_function(lock_fn, (void *)my_stream_allocator_lock)) {
            ALOGI("Successfully hooked %s:lock at %p", lib_name, lock_fn);
        }
    } else {
        ALOGE("Failed to find %s in %s", lock_sym_name, lib_name);
    }

    if (unlock_fn) {
        patch_validate_at(unlock_fn);

        if (hook_thumb_function(unlock_fn, (void *)my_stream_allocator_unlock)) {
            ALOGI("Successfully hooked %s:unlock at %p", lib_name, unlock_fn);
        }
    } else {
        ALOGE("Failed to find %s in %s", unlock_sym_name, lib_name);
    }

    if (strstr(lib_name, "camera3")) {
        const char *get_prev_name = "_ZN7android23ExynosCamera3Parameters23getPreviewBayerCropSizeEP10ExynosRectS2_";
        const char *calc_prev_name = "_ZN7android23ExynosCamera3Parameters24calcPreviewBayerCropSizeEP10ExynosRectS2_";
        const char *get_pic_name = "_ZN7android23ExynosCamera3Parameters23getPictureBayerCropSizeEP10ExynosRectS2_";
        const char *calc_pic_name = "_ZN7android23ExynosCamera3Parameters24calcPictureBayerCropSizeEP10ExynosRectS2_";

        void *get_prev_fn = dlsym(handle, get_prev_name);
        void *calc_prev_fn = dlsym(handle, calc_prev_name);
        void *get_pic_fn = dlsym(handle, get_pic_name);
        void *calc_pic_fn = dlsym(handle, calc_pic_name);

        if (calc_prev_fn) {
            fn_calcPreviewBayerCropSize = (int (*)(void *, void *, void *))calc_prev_fn;
        }
        if (calc_pic_fn) {
            fn_calcPictureBayerCropSize = (int (*)(void *, void *, void *))calc_pic_fn;
        }

        if (get_prev_fn && calc_prev_fn) {
            if (hook_thumb_function(get_prev_fn, (void *)my_getPreviewBayerCropSize)) {
                ALOGI("Successfully hooked %s:getPreviewBayerCropSize at %p", lib_name, get_prev_fn);
            }
        }
        if (get_pic_fn && calc_pic_fn) {
            if (hook_thumb_function(get_pic_fn, (void *)my_getPictureBayerCropSize)) {
                ALOGI("Successfully hooked %s:getPictureBayerCropSize at %p", lib_name, get_pic_fn);
            }
        }
    }
}

static pthread_once_t hook_once = PTHREAD_ONCE_INIT;

static void install_hooks_once() {
    hook_library("libexynoscamera3.so");
    hook_library("libexynoscamera.so");
    dl_iterate_phdr(phdr_callback, nullptr);
}

extern "C" void ensure_camera_hooks_installed() {
    pthread_once(&hook_once, install_hooks_once);
    dl_iterate_phdr(phdr_callback, nullptr);
}

__attribute__((constructor))
static void init_stream_allocator_hook() {
    ALOGI("libexynoscamera_shim: constructor running...");
    android_fdsan_set_error_level(ANDROID_FDSAN_ERROR_LEVEL_DISABLED);
    ensure_camera_hooks_installed();
}
