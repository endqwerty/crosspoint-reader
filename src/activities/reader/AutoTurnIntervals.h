#pragma once

#include <cstddef>
#include <cstdint>

// Auto page-turn intervals in seconds, shared by the reader menu and the More
// panel. Option 0 is off; the option index is what the menus pass around.
namespace AutoTurn {

constexpr uint16_t SECONDS[] = {0, 5, 10, 15, 20, 30, 45, 60, 90, 120};
constexpr size_t OPTION_COUNT = sizeof(SECONDS) / sizeof(SECONDS[0]);

constexpr bool isActive(const size_t option) { return option > 0 && option < OPTION_COUNT; }

constexpr unsigned long durationMs(const size_t option) {
  return isActive(option) ? static_cast<unsigned long>(SECONDS[option]) * 1000UL : 0UL;
}

}  // namespace AutoTurn
