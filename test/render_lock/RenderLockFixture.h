#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "RenderLock.h"

class Activity {};

struct Semaphore {
  bool held = false;
  unsigned takeCalls = 0;
  unsigned giveCalls = 0;
  uint32_t lastWait = 0;
};

inline Semaphore semaphore;
struct TestActivityManager {
  Semaphore* renderingMutex = &semaphore;
  mutable int hintCalls = 0;
  mutable bool hintWasLocked = false;
  bool wantsFastLoop = false;
  bool skipLoopDelay() const {
    ++hintCalls;
    hintWasLocked = semaphore.held;
    return wantsFastLoop;
  }
};
inline TestActivityManager activityManager;

inline constexpr int pdTRUE = 1;
inline constexpr uint32_t portMAX_DELAY = UINT32_MAX;

inline int xSemaphoreTake(Semaphore* mutex, uint32_t wait) {
  ++mutex->takeCalls;
  mutex->lastWait = wait;
  if (mutex->held) return 0;
  mutex->held = true;
  return pdTRUE;
}

inline int xSemaphoreGive(Semaphore* mutex) {
  ++mutex->giveCalls;
  mutex->held = false;
  return pdTRUE;
}

inline int xQueuePeek(Semaphore* mutex, void*, uint32_t) { return mutex->held ? 0 : pdTRUE; }

inline unsigned long schedulingNow = 100000;
inline unsigned long lastActivityTime = 0;
inline unsigned long millis() { return schedulingNow; }
struct HalPowerManager {
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 10000;
};
struct SchedulingPower {
  int calls = 0;
  bool saving = false;
  bool changedWhileLocked = false;
  void setPowerSaving(bool value) {
    ++calls;
    saving = value;
    changedWhileLocked = semaphore.held;
  }
};
inline SchedulingPower powerManager;
struct SchedulingGpio {
  bool active = false;
  bool rawInputActive() const { return active; }
};
inline SchedulingGpio gpio;
inline unsigned delayCalls = 0, delayedMs = 0, yieldCalls = 0;
inline bool yieldedWhileLocked = false;
inline void delay(unsigned ms) {
  ++delayCalls;
  delayedMs += ms;
  schedulingNow += ms;
}
inline void yield() {
  ++yieldCalls;
  yieldedWhileLocked = semaphore.held;
}
void runMainLoopTail();
