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

#undef LOG_TAG
#define LOG_TAG "ExynosCameraShimHook"

typedef int (*orig_configureStreams_t)(void *this_ptr, camera3_stream_configuration_t *stream_list);
static orig_configureStreams_t g_orig_configureStreams = nullptr;

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
    ensure_camera_hooks_installed();
}
