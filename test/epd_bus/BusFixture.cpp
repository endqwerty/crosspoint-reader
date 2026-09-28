#include "BusFixture.h"

#include <Arduino.h>

// Mach-O requires definitions for the SDK's weak board extension symbols.
extern "C" void freeink_board_epd_power(bool) {}
extern "C" void freeink_board_epd_reset(bool) {}

namespace bus_test {
State state;

void reset() {
  resetBusSemaphore();
  state = State{};
}

void advance(const uint64_t us) {
  const uint64_t target = state.nowUs + us;
  while (state.nextEdge < state.edges.size() && state.edges[state.nextEdge].atUs <= target) {
    const auto edge = state.edges[state.nextEdge++];
    state.nowUs = edge.atUs;
    const bool changed = state.busyLevel != edge.level;
    state.busyLevel = edge.level;
    const bool triggers = state.interruptMode == CHANGE || (state.interruptMode == RISING && edge.level == HIGH) ||
                          (state.interruptMode == FALLING && edge.level == LOW);
    if (changed && triggers && state.isr) state.isr();
  }
  state.nowUs = target;
}

void beginHook() { ++state.beginHooks; }
void endHook() { ++state.endHooks; }
bool sliceHook(const int8_t pin, const uint8_t level) {
  ++state.sliceHooks;
  state.slicePin = pin;
  state.sliceLevel = level;
  if (state.sliceWaits) advance(1000);
  return state.sliceWaits;
}
}  // namespace bus_test
