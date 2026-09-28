#pragma once

#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <initializer_list>

inline constexpr int BRIGHTNESS_STEP = 1;
inline constexpr uint8_t MIN_BRIGHTNESS = 1;

struct RenderLock {
  inline static unsigned held = 0;
  inline static unsigned acquisitions = 0;
  RenderLock() {
    EXPECT_EQ(held, 0u);
    ++held;
    ++acquisitions;
  }
  ~RenderLock() { --held; }
};

struct SettingsFixture {
  uint8_t frontlightBrightness = 60;
  uint8_t frontlightWarmth = 50;
  uint8_t frontlightOn = 0;
  uint8_t screenInverted = 0;
  uint8_t orientation = 0;
  uint8_t touchReaderControls = 1;
  unsigned writes = 0;
  void saveToFile() { ++writes; }
};
inline SettingsFixture SETTINGS;

struct FrontlightFixture {
  void setBrightness(uint8_t) { EXPECT_EQ(RenderLock::held, 1u); }
  void setWarmth(uint8_t) { EXPECT_EQ(RenderLock::held, 1u); }
  void setOn(bool) { EXPECT_EQ(RenderLock::held, 1u); }
};
inline FrontlightFixture Frontlight;

namespace fui {
struct ActionEvent {
  int value = 0;
  int dragPermille = -1;
};
}  // namespace fui

struct MappedInputManager {
  enum class Button { Back, Confirm, Left, Right };
  enum class SwipeDir { None, Up };
  bool backReleased = false;
  bool confirmReleased = false;
  SwipeDir swipe = SwipeDir::None;
  SwipeDir wasSwipe() const { return swipe; }
  bool wasReleased(Button button) const {
    return (button == Button::Back && backReleased) || (button == Button::Confirm && confirmReleased);
  }
};

struct ButtonNavigator {
  template <typename Callback>
  void onPressAndContinuous(std::initializer_list<MappedInputManager::Button>, Callback) {
    EXPECT_EQ(RenderLock::held, 1u);
  }
};

struct CrossPointSettings {
  static constexpr uint8_t TOUCH_READER_OFF = 0;
};

class PanelRenderer : public GfxRenderer {
 public:
  using GfxRenderer::GfxRenderer;
  unsigned promotions = 0;
  void promoteNextRefresh(HalDisplay::RefreshMode mode) {
    EXPECT_EQ(RenderLock::held, 1u);
    ++promotions;
    GfxRenderer::promoteNextRefresh(mode);
  }
};

class Activity {
 public:
  unsigned exitCalls = 0;
  unsigned closeCalls = 0;
  bool closeWasLocked = false;
  void onExit() {
    EXPECT_EQ(RenderLock::held, 1u);
    ++exitCalls;
  }
  void finish() {
    closeWasLocked = RenderLock::held == 1;
    ++closeCalls;
  }
};

class FrontlightPanelActivity : public Activity {
 public:
  explicit FrontlightPanelActivity(PanelRenderer& renderer) : renderer(renderer) {}
  PanelRenderer& renderer;
  uint8_t brightness = 60;
  uint8_t warmth = 50;
  bool lightOn = false;
  bool lightOnChanged = false;
  bool draggingSlider = false;
  uint8_t touchModeRestore = 1;
  int panelBottom = 0;
  bool cleanRefreshPending = false;
  bool refreshOnExit = false;
  unsigned updateRequests = 0;
  MappedInputManager mappedInput;
  ButtonNavigator buttonNavigator;
  struct App {
    bool dirty = false;
    bool invalidated() const {
      EXPECT_EQ(RenderLock::held, 1u);
      return dirty;
    }
  } app;
  struct TouchRoute {
    bool routed = false;
    bool handled = false;
    fui::ActionEvent event;
    struct Snapshot {
      bool touchReleased = false;
      bool touchHeld = false;
      int touchY = 0;
    } snap;
    explicit operator bool() const { return handled; }
  } touch;
  int routedTile = -1;
  int renderTile = -1;

  TouchRoute routeTouch(const MappedInputManager&, bool, bool) {
    EXPECT_EQ(RenderLock::held, 1u);
    if (routedTile >= 0) {
      touch.routed = touch.handled = true;
      touch.event.value = routedTile;
      routedTile = -1;
      onTileEvent(touch.event, this);
      app.dirty = true;
    }
    return touch;
  }

  void requestUpdate() { ++updateRequests; }
  int computePanelBottom() const { return 400; }
  void renderUi() {
    EXPECT_EQ(RenderLock::held, 1u);
    renderer.clearScreen(0x33);
    if (renderTile >= 0) {
      const fui::ActionEvent event{renderTile};
      renderTile = -1;
      onTileEvent(event, this);
    }
  }
  void persistLightSettings();
  void onExit();
  void runTile(int idx);
  static void onTileEvent(const fui::ActionEvent&, void*);
  void adjustBrightness(int delta);
  void adjustWarmth(int delta);
  void toggleLight();
  void loop();
  void close();
  void render(RenderLock&&);
};
