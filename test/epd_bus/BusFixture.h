#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bus_test {
struct Edge {
  uint64_t atUs;
  int level;
};
struct Write {
  uint8_t command;
  std::vector<uint8_t> bytes;
};
struct State {
  uint64_t nowUs = 0;
  int busyLevel = 0;
  int8_t busyPin = 6;
  std::vector<Edge> edges;
  size_t nextEdge = 0;
  bool allowSemaphore = true;
  unsigned failedSemaphoreAllocations = 0;
  bool token = false;
  void (*isr)() = nullptr;
  int interruptMode = 0;
  unsigned attaches = 0;
  unsigned detaches = 0;
  unsigned beginHooks = 0;
  unsigned endHooks = 0;
  unsigned sliceHooks = 0;
  unsigned transfers = 0;
  bool transaction = false;
  int dcPin = 4;
  int dcLevel = 1;
  std::vector<Write> writes;
  void (*commandHook)(uint8_t) = nullptr;
  unsigned activations = 0;
  unsigned stuckActivation = 0;
  int stuckCommand = -1;
  bool sliceWaits = false;
  int8_t slicePin = -1;
  uint8_t sliceLevel = 0;
};
extern State state;
void reset();
void resetBusSemaphore();
void advance(uint64_t us);
void beginHook();
void endHook();
bool sliceHook(int8_t pin, uint8_t level);
}  // namespace bus_test
