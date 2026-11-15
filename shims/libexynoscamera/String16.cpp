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

#include <utils/String16.h>
#include <utils/Errors.h>
#include <string>

namespace android {

status_t String16_remove(String16* thiz, size_t len, size_t begin) {
    const size_t myLen = thiz->size();
    if (begin >= myLen) return OK;
    if ((begin + len) > myLen) len = myLen - begin;
    if (len == 0) return OK;
    if (begin == 0 && len == myLen) {
        return thiz->setTo(u"", 0);
    }
    std::u16string temp;
    temp.reserve(myLen - len);
    temp.append(thiz->string(), begin);
    temp.append(thiz->string() + begin + len, myLen - begin - len);
    return thiz->setTo(temp.data(), temp.size());
}

} // namespace android

extern "C" android::status_t _ZN7android8String166removeEjj(android::String16* thiz, size_t len, size_t begin) {
    return android::String16_remove(thiz, len, begin);
}
