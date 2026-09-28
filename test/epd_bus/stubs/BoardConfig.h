#pragma once

#define FREEINK_DEVICE_PAPERMONO 0
#define FREEINK_DEVICE_WS397 0
#include "../../../freeink-sdk/libs/display/FreeInkDisplay/test/host/pro_stubs/BoardConfig.h"
namespace BoardConfig {
inline bool isOnePage() { return false; }
}  // namespace BoardConfig
