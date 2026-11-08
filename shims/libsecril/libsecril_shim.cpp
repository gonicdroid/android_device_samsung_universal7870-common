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

#include <stdint.h>
#include <stdlib.h>
#include <uchar.h>
#include <limits.h>

#define UTF16_REPLACEMENT_CHAR 0xfffd
#define UTF8_SEQ_LENGTH(ch) (((0xe5000000 >> (((ch) >> 3) & 0x1e)) & 3) + 1)
#define UTF8_SHIFT_AND_MASK(unicode, byte) (unicode) <<= 6; (unicode) |= (0x3f & (byte));
#define UNICODE_UPPER_LIMIT 0x10fffd

extern "C" {

static inline uint32_t getUtf32FromUtf8(const char** pUtf8Ptr) {
    uint32_t ret;
    int seq_len;
    int i;
    static const unsigned char leaderMask[4] = {0xff, 0x1f, 0x0f, 0x07};

    if (((**pUtf8Ptr) & 0xc0) == 0x80) {
        (*pUtf8Ptr)++;
        return UTF16_REPLACEMENT_CHAR;
    }

    seq_len = UTF8_SEQ_LENGTH(**pUtf8Ptr);
    ret = (**pUtf8Ptr) & leaderMask[seq_len - 1];

    if (**pUtf8Ptr == '\0') return ret;

    (*pUtf8Ptr)++;
    for (i = 1; i < seq_len; i++, (*pUtf8Ptr)++) {
        if ((**pUtf8Ptr) == '\0') return UTF16_REPLACEMENT_CHAR;
        if (((**pUtf8Ptr) & 0xc0) != 0x80) return UTF16_REPLACEMENT_CHAR;
        UTF8_SHIFT_AND_MASK(ret, **pUtf8Ptr);
    }
    return ret;
}

size_t strlen8to16(const char* utf8Str) {
    size_t len = 0;
    int ic;
    int expected = 0;

    while ((ic = *utf8Str++) != '\0') {
        if ((ic & 0xc0) == 0x80) {
            expected--;
            if (expected < 0) {
                len++;
            }
        } else {
            len++;
            expected = UTF8_SEQ_LENGTH(ic) - 1;
            if (expected == 3) {
                len++;
            }
        }
    }
    return len;
}

char16_t* strcpy8to16(char16_t* utf16Str, const char* utf8Str, size_t* out_len) {
    char16_t* dest = utf16Str;

    while (*utf8Str != '\0') {
        uint32_t ret = getUtf32FromUtf8(&utf8Str);
        if (ret <= 0xffff) {
            *dest++ = (char16_t)ret;
        } else if (ret <= UNICODE_UPPER_LIMIT) {
            *dest++ = 0xd800 | ((ret - 0x10000) >> 10);
            *dest++ = 0xdc00 | ((ret - 0x10000) & 0x3ff);
        } else {
            *dest++ = UTF16_REPLACEMENT_CHAR;
        }
    }

    *out_len = dest - utf16Str;
    return utf16Str;
}

char16_t* strdup8to16(const char* s, size_t* out_len) {
    char16_t* ret;
    size_t len;

    if (s == NULL) return NULL;

    len = strlen8to16(s);
    if (len && SIZE_MAX / len < sizeof(char16_t))
        return NULL;

    ret = (char16_t*)malloc(sizeof(char16_t) * len);
    return strcpy8to16(ret, s, out_len);
}

char16_t* strcpylen8to16(char16_t* utf16Str, const char* utf8Str, int length, size_t* out_len) {
    char16_t* dest = utf16Str;
    const char* end = utf8Str + length;

    while (utf8Str < end) {
        uint32_t ret = getUtf32FromUtf8(&utf8Str);
        if (ret <= 0xffff) {
            *dest++ = (char16_t)ret;
        } else if (ret <= UNICODE_UPPER_LIMIT) {
            *dest++ = 0xd800 | ((ret - 0x10000) >> 10);
            *dest++ = 0xdc00 | ((ret - 0x10000) & 0x3ff);
        } else {
            *dest++ = UTF16_REPLACEMENT_CHAR;
        }
    }

    *out_len = dest - utf16Str;
    return utf16Str;
}

} // extern "C"

