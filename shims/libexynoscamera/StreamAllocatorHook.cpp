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
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <android/fdsan.h>

#undef LOG_TAG
#define LOG_TAG "ExynosCameraShimHook"

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

    // In ExynosCameraStreamAllocator:
    // offset 0: vptr
    // offset 4: camera3_stream_t *m_allocator;
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

    if (format == HAL_PIXEL_FORMAT_BLOB || planeCount == 1) {
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
    // Check if instructions match: mov r3, r0 (0x4603), cbz r3, ... (0xb183)
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
}

static pthread_once_t hook_once = PTHREAD_ONCE_INIT;

static void install_hooks_once() {
    hook_library("libexynoscamera3.so");
    hook_library("libexynoscamera.so");
}

extern "C" void ensure_camera_hooks_installed() {
    pthread_once(&hook_once, install_hooks_once);
}

__attribute__((constructor))
static void init_stream_allocator_hook() {
    ALOGI("libexynoscamera_shim: constructor running...");
    android_fdsan_set_error_level(ANDROID_FDSAN_ERROR_LEVEL_DISABLED);
    ensure_camera_hooks_installed();
}