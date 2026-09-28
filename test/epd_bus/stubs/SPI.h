#pragma once

#include <cstdint>

#include "BusFixture.h"

constexpr int MSBFIRST = 1;
constexpr int SPI_MODE0 = 0;
struct SPISettings {
  SPISettings() = default;
  SPISettings(uint32_t, int, int) {}
};
struct FakeSPI {
  void begin(int, int, int, int) {}
  void beginTransaction(const SPISettings&) { bus_test::state.transaction = true; }
  void endTransaction() { bus_test::state.transaction = false; }
  uint8_t transfer(uint8_t value) {
    auto& state = bus_test::state;
    ++state.transfers;
    if (state.dcLevel == 0) {
      state.writes.push_back({value, {}});
      if (state.commandHook) state.commandHook(value);
    } else if (!state.writes.empty()) {
      state.writes.back().bytes.push_back(value);
    }
    return value;
  }
  void writeBytes(const uint8_t* bytes, uint16_t len) {
    auto& state = bus_test::state;
    state.transfers += len;
    if (!state.writes.empty()) state.writes.back().bytes.insert(state.writes.back().bytes.end(), bytes, bytes + len);
  }
};
inline FakeSPI SPI;
