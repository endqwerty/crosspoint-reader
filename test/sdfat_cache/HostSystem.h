#pragma once
#include <cstdint>
inline uint32_t millis() {
  static uint32_t ticks = 0;
  return ++ticks;
}
class __FlashStringHelper;
