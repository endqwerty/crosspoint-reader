#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <utility>

struct RenderLock {
  inline static int held = 0;
  inline static int acquisitions = 0;
  RenderLock() {
    EXPECT_EQ(held, 0);
    ++held;
    ++acquisitions;
  }
  ~RenderLock() {
    EXPECT_EQ(held, 1);
    --held;
  }
};

namespace fui {
struct ActionEvent {
  int value = 0;
  int dragPermille = -1;
};
}  // namespace fui

struct MappedInputManager {
  enum class Button { Back, Confirm, Left, Right, Up, Down };
  enum class SwipeDir { None, Left, Right };
  std::array<bool, 6> released{};
  std::array<bool, 6> pressed{};
  SwipeDir swipe = SwipeDir::None;
  bool wasReleased(Button button) const { return released[static_cast<size_t>(button)]; }
  SwipeDir wasSwipe() const { return swipe; }
};

struct ButtonNavigator {
  MappedInputManager& input;
  template <class Callback>
  void onPressAndContinuous(std::initializer_list<MappedInputManager::Button> buttons, Callback callback) {
    EXPECT_EQ(RenderLock::held, 1);
    for (auto button : buttons)
      if (input.pressed[static_cast<size_t>(button)]) callback();
  }
};

struct Gpio {
  bool edge = true;
  bool hasEdgeSideButtons() const { return edge; }
};
inline Gpio gpio;
inline constexpr int kSmallStep = 1;
inline constexpr int kLargeStep = 10;

struct ActivityResult {
  bool isCancelled = false;
};
struct PercentResult {
  int percent;
};
struct IntervalResult {
  uint32_t value;
};

struct SliderHost {
  MappedInputManager mappedInput;
  ButtonNavigator buttonNavigator{mappedInput};
  struct App {
    bool dirty = false;
    int flashClears = 0;
    bool invalidated() const {
      EXPECT_EQ(RenderLock::held, 1);
      return dirty;
    }
    void clearTapFlash() {
      EXPECT_EQ(RenderLock::held, 1);
      ++flashClears;
    }
  } app;
  struct TouchRoute {
    bool routed = false;
    bool handled = false;
    fui::ActionEvent event;
    struct Snapshot {
      bool touchHeld = false;
      bool touchReleased = false;
      int touchX = -1;
    } snap;
    explicit operator bool() const { return handled; }
  } touch;
  void (*routedHandler)(const fui::ActionEvent&, void*) = nullptr;
  void* routedUser = nullptr;
  bool draggingSlider = false;
  bool ready = true;
  int updates = 0;
  int finishes = 0;
  bool hasResult = false;
  bool cancelled = false;
  int resultValue = -1;

  TouchRoute routeTouch(const MappedInputManager&, bool, bool held) {
    EXPECT_EQ(RenderLock::held, 1);
    EXPECT_TRUE(held);
    if (routedHandler) {
      touch.routed = touch.handled = true;
      std::exchange(routedHandler, nullptr)(touch.event, routedUser);
      app.dirty = true;
    }
    return touch;
  }
  bool routingReady() const { return ready; }
  void requestUpdate() {
    EXPECT_EQ(RenderLock::held, 1);
    ++updates;
  }
  void finish() {
    EXPECT_EQ(RenderLock::held, 1);
    ++finishes;
  }
  void setResult(ActivityResult result) {
    EXPECT_EQ(RenderLock::held, 1);
    hasResult = true;
    cancelled = result.isCancelled;
  }
  void setResult(PercentResult result) {
    EXPECT_EQ(RenderLock::held, 1);
    hasResult = true;
    resultValue = result.percent;
  }
  void setResult(IntervalResult result) {
    EXPECT_EQ(RenderLock::held, 1);
    hasResult = true;
    resultValue = static_cast<int>(result.value);
  }
};

struct EpubReaderPercentSelectionActivity : SliderHost {
  int percent = 50;
  int selection() const { return percent; }
  void loop();
  void adjustPercent(int);
  void setPercent(int);
  void cancel();
  void confirm();
  static void onSliderEvent(const fui::ActionEvent&, void*);
  static void onStepEvent(const fui::ActionEvent&, void*);
  static void onOkEvent(const fui::ActionEvent&, void*);
};

struct IntervalSelectionActivity : SliderHost {
  int value = 50;
  int minValue = 0, maxValue = 100, smallStep = 1, largeStep = 10;
  int selection() const { return value; }
  int clampedValue(int) const;
  void loop();
  void adjustValue(int);
  void setValue(int);
  void cancel();
  void confirm();
  static void onSliderEvent(const fui::ActionEvent&, void*);
  static void onStepEvent(const fui::ActionEvent&, void*);
  static void onOkEvent(const fui::ActionEvent&, void*);
};
