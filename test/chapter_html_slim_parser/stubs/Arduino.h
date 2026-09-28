#pragma once

#include <HalStorage.h>

#include <cstdint>

inline void vTaskDelay(uint32_t) {}

struct EspHostStub {
  uint32_t getFreeHeap() const { return UINT32_MAX; }
};

inline EspHostStub ESP;
