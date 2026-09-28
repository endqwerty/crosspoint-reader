#pragma once

#include <cstdint>

using BaseType_t = int;
using TickType_t = uint32_t;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdTRUE = 1;
#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR() ((void)0)
