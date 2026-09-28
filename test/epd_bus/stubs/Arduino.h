#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define PROGMEM
#define pgm_read_byte(p) (*(p))

#include "BusFixture.h"

#define IRAM_ATTR
#define DRAM_ATTR
constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int OUTPUT = 1;
constexpr int INPUT = 0;
constexpr int INPUT_PULLUP = 2;
constexpr int CHANGE = 3;
constexpr int RISING = 1;
constexpr int FALLING = 2;

inline uint32_t millis() { return static_cast<uint32_t>(bus_test::state.nowUs / 1000); }
inline void delay(unsigned long ms) { bus_test::advance(static_cast<uint64_t>(ms) * 1000); }
inline void delayMicroseconds(unsigned int us) { bus_test::advance(us); }
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int level) {
  if (pin == bus_test::state.dcPin) bus_test::state.dcLevel = level;
}
inline int digitalRead(int pin) { return pin == bus_test::state.busyPin ? bus_test::state.busyLevel : LOW; }
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*isr)(), int mode) {
  bus_test::state.isr = isr;
  bus_test::state.interruptMode = mode;
  ++bus_test::state.attaches;
}
inline void detachInterrupt(int) {
  bus_test::state.isr = nullptr;
  ++bus_test::state.detaches;
}
struct FakeSerial {
  explicit operator bool() const { return false; }
  template <typename... Args>
  void printf(const char*, Args...) {}
};
inline FakeSerial Serial;
