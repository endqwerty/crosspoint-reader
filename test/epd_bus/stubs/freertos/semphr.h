#pragma once

#include "BusFixture.h"
#include "FreeRTOS.h"

using SemaphoreHandle_t = bool*;
inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  if (!bus_test::state.allowSemaphore) ++bus_test::state.failedSemaphoreAllocations;
  return bus_test::state.allowSemaphore ? &bus_test::state.token : nullptr;
}
inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t handle, BaseType_t* woken) {
  *handle = true;
  *woken = pdTRUE;
  return pdTRUE;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks) {
  auto& s = bus_test::state;
  const uint64_t deadline = s.nowUs + static_cast<uint64_t>(ticks) * 1000;
  while (!*handle && s.nextEdge < s.edges.size() && s.edges[s.nextEdge].atUs <= deadline) {
    bus_test::advance(s.edges[s.nextEdge].atUs - s.nowUs);
  }
  if (*handle) {
    *handle = false;
    return pdTRUE;
  }
  bus_test::advance(deadline - s.nowUs);
  return pdFALSE;
}
