#pragma once
#include <FreeInkApp.h>
#include <Logging.h>

#include <string>
#include <utility>
#include <vector>
struct Rect {
  int x, y, width, height;
};
struct RenderLock {
  RenderLock() = default;
  template <class T>
  explicit RenderLock(T&) {}
};
struct GfxRenderer {
  enum class Orientation { Portrait, PortraitInverted, LandscapeClockwise, LandscapeCounterClockwise };
  Orientation orientation = Orientation::Portrait;
  int width = 240, height = 200, frames = 0;
  struct Line {
    freeink::ui::Rect rect;
    std::string text;
  };
  std::vector<Line> lines;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  Orientation getOrientation() const { return orientation; }
  void clearScreen() { lines.clear(); }
  void displayBuffer() { ++frames; }
};
struct MappedInputManager {
  enum class Button { Back, Confirm, NavNext, NavPrevious };
  enum class SwipeDir { None, Up, Down };
  bool back = false, confirm = false, next = false, previous = false;
  SwipeDir swipe = SwipeDir::None;
  bool wasReleased(Button b) {
    return b == Button::Back      ? std::exchange(back, false)
           : b == Button::Confirm ? std::exchange(confirm, false)
                                  : false;
  }
  SwipeDir wasSwipe() { return std::exchange(swipe, SwipeDir::None); }
  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) { return {a, b, c, d}; }
};
struct MenuResult {
  int action;
};
struct Activity {
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  int updates = 0, finishes = 0, result = -1;
  Activity(const char*, GfxRenderer& r, MappedInputManager& i) : renderer(r), mappedInput(i) {}
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void loop() {}
  virtual void render(RenderLock&&) {}
  void requestUpdate() { ++updates; }
  void finish() { ++finishes; }
  void setResult(MenuResult r) { result = r.action; }
};
