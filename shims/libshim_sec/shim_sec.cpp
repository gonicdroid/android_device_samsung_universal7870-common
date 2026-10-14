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

#include <string>
#include <cstdint>

namespace android {
namespace hardware {
namespace radio {
namespace V1_0 {

enum class PinState : int32_t;
enum class CardState : int32_t;
struct OperatorInfo;
struct SignalStrength;
struct CallForwardInfo;
struct DataRegStateResult;
struct VoiceRegStateResult;
struct Call;
struct Dial;
struct AppStatus;

std::string toString(PinState) { return ""; }
std::string toString(CardState) { return ""; }
std::string toString(const OperatorInfo&) { return ""; }
std::string toString(const SignalStrength&) { return ""; }
std::string toString(const CallForwardInfo&) { return ""; }
std::string toString(const DataRegStateResult&) { return ""; }
std::string toString(const VoiceRegStateResult&) { return ""; }
std::string toString(const Call&) { return ""; }
std::string toString(const Dial&) { return ""; }
std::string toString(const AppStatus&) { return ""; }

bool operator!=(const OperatorInfo& a, const OperatorInfo& b) { return &a != &b; }
bool operator!=(const SignalStrength& a, const SignalStrength& b) { return &a != &b; }
bool operator!=(const CallForwardInfo& a, const CallForwardInfo& b) { return &a != &b; }
bool operator!=(const DataRegStateResult& a, const DataRegStateResult& b) { return &a != &b; }
bool operator!=(const VoiceRegStateResult& a, const VoiceRegStateResult& b) { return &a != &b; }
bool operator!=(const Call& a, const Call& b) { return &a != &b; }
bool operator!=(const Dial& a, const Dial& b) { return &a != &b; }
bool operator!=(const AppStatus& a, const AppStatus& b) { return &a != &b; }

} // namespace V1_0
} // namespace radio

namespace sensors {
namespace V1_0 {

struct SensorInfo;
bool operator==(const SensorInfo& a, const SensorInfo& b) { return &a == &b; }

} // namespace V1_0
} // namespace sensors

} // namespace hardware
} // namespace android
