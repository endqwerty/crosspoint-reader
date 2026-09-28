#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

inline uint32_t inputNow = 0;
inline uint32_t millis() { return inputNow; }
class MappedInputManager {
 public:
  enum class Button { NavNext, NavPrevious, Left, Right, Up, Down, Count };
  std::array<bool, static_cast<size_t>(Button::Count)> pressed{}, released{}, held{};
  uint32_t heldTime = 0;
  bool wasPressed(Button b) const { return pressed[static_cast<size_t>(b)]; }
  bool wasReleased(Button b) const { return released[static_cast<size_t>(b)]; }
  bool isPressed(Button b) const { return held[static_cast<size_t>(b)]; }
  uint32_t getHeldTime() const { return heldTime; }
};
