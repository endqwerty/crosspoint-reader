#pragma once
#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <variant>

#include "activities/ActivityResult.h"
#include "util/HomeButtonInput.h"

inline bool searchTestRenderLockHeld = false;
inline bool searchTestReadActive = false;
inline uint32_t millis() { return 0; }
struct SearchSettingsFixture {
  HomeButtonAction homeButtonTapAction = HomeButtonAction::Home;
  HomeButtonAction homeButtonDoubleTapAction = HomeButtonAction::Ignore;
  HomeButtonAction homeButtonLongPressAction = HomeButtonAction::ReaderMenu;
};
inline SearchSettingsFixture SETTINGS;
class GfxRenderer {
 public:
  inline static bool loanActive = false;
  inline static unsigned loans = 0;
  class FrameBufferLoan {
   public:
    explicit FrameBufferLoan(GfxRenderer&) {
      EXPECT_TRUE(searchTestRenderLockHeld);
      EXPECT_FALSE(loanActive);
      loanActive = true;
      loans++;
    }
    ~FrameBufferLoan() { loanActive = false; }
  };
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
};
class MappedInputManager {
 public:
  enum class Button { Back, ScreenDown = 14 };
  enum class SwipeDir { None };
  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };
  struct InputFixture {
    mutable unsigned updates = 0;
    unsigned tapAt = 0;
    unsigned holdAt = 0;
    void update() const { updates++; }
    bool hasHomeKey() const { return true; }
    bool wasHomeKeyTapped() const { return tapAt && updates == tapAt; }
    bool wasHomeKeyLongPressed() const { return holdAt && updates == holdAt; }
    bool wasHomeKeyPressed() const { return false; }
  } gpio;
  unsigned& updates = gpio.updates;
  mutable HomeButtonInput homeButtonInput;
  mutable HomeButtonAction homeAction = HomeButtonAction::Ignore;
  mutable HomeButtonAction deferredHomeAction = HomeButtonAction::Ignore;
  mutable uint32_t longPressFiredButtons = 0;
  unsigned cancelAt = 0;
  bool releaseBack = false;
  bool backGesture = false;
  void update(bool deferHomeButtonAction = false) const;
  bool isPressed(Button button) const {
    return button == Button::Back && cancelAt && updates >= cancelAt && !releaseBack;
  }
  bool wasReleased(Button) const { return releaseBack; }
  bool wasBackGesture() const { return backGesture; }
  bool wasHomeGesture() const { return homeAction == HomeButtonAction::Home; }
  HomeButtonAction homeButtonAction() const { return homeAction; }
  SwipeDir wasSwipe() const { return SwipeDir::None; }
  void resetHomeButtonInput() const {
    homeButtonInput.reset();
    deferredHomeAction = HomeButtonAction::Ignore;
  }
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) const { return {a, b, c, d}; }
};
namespace freeink::ui {
using ActionId = int;
struct ListItem {
  const char* label = nullptr;
  const char* subtitle = nullptr;
  int16_t actionValue = 0;
};
struct Insets {
  int16_t a, b, c, d;
};
struct ListProps {
  const ListItem* items = nullptr;
  uint16_t count = 0;
  int action = 0;
  int inputMask = 0;
};
inline constexpr int InputTouch = 1;
}  // namespace freeink::ui
struct Rect {
  int x, y, width, height;
};
class UiScreen {
 public:
  struct Theme {
    int bodyText = 0;
  };
  Theme theme() const { return {}; }
  void setContentMarginFromScreen(freeink::ui::Insets) {}
  void spacer(int16_t) {}
  void centeredText(const char*, int) { centeredCalls++; }
  void list(freeink::ui::ListProps props) { lastCount = props.count; }
  int lastCount = 0;
  int centeredCalls = 0;
  int selectionOffset = -1;
};
class RenderLock {
 public:
  RenderLock() {
    EXPECT_FALSE(searchTestRenderLockHeld);
    searchTestRenderLockHeld = true;
  }
  template <class T>
  explicit RenderLock(T&) : RenderLock() {}
  ~RenderLock() { searchTestRenderLockHeld = false; }
};
class Activity {
 public:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  inline static bool finished = false;
  inline static unsigned homeRequests = 0;
  inline static MappedInputManager* activeInput = nullptr;
  inline static unsigned paints = 0;
  ActivityResult result;
  ActivityResultHandler keyboardCallback;
  Activity(GfxRenderer& renderer, MappedInputManager& input) : renderer(renderer), mappedInput(input) {
    activeInput = &input;
  }
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void loop() {}
  virtual bool preventAutoSleep() { return false; }
  void requestUpdate(bool = false) {
    EXPECT_FALSE(GfxRenderer::loanActive);
    paints++;
  }
  void requestUpdateAndWait() {
    EXPECT_FALSE(GfxRenderer::loanActive);
    paints++;
  }
  void startActivityForResult(std::unique_ptr<Activity>&&, ActivityResultHandler cb) {
    keyboardCallback = std::move(cb);
  }
  void setResult(ActivityResult&& value) { result = std::move(value); }
  static void finish() {
    finished = true;
    activeInput->resetHomeButtonInput();
  }
  static void onGoHome() {
    EXPECT_FALSE(GfxRenderer::loanActive);
    EXPECT_FALSE(searchTestRenderLockHeld);
    EXPECT_FALSE(searchTestReadActive);
    homeRequests++;
    activeInput->resetHomeButtonInput();
  }
};
class UiListActivity : public Activity {
 public:
  struct App {
    void clearTapFlash() {}
  } app;
  static constexpr int ACTION_ROW = 1;
  UiListActivity(const char*, GfxRenderer& renderer, MappedInputManager& input) : Activity(renderer, input) {}
  virtual int listCount() const = 0;
  virtual void buildScreen(UiScreen&) = 0;
  virtual void activateIndex(int) = 0;
  virtual void onBackButton() {}
  virtual const char* headerTitle() const { return ""; }
  virtual void drawFooter() {}
  void syncListViewport(UiScreen& screen, freeink::ui::ListProps&, int selectionOffset = 0) {
    screen.selectionOffset = selectionOffset;
  }
};
struct BaseTheme {
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) const {}
};
class UITheme {
 public:
  struct Metrics {
    int topPadding = 10;
    int headerHeight = 40;
    int verticalSpacing = 8;
  };
  const Metrics& getMetrics() const {
    static Metrics metrics;
    return metrics;
  }
  Rect getScreenSafeArea(GfxRenderer&, bool, bool) const { return {0, 0, 480, 750}; }
  const BaseTheme& getTheme() const {
    static BaseTheme theme;
    return theme;
  }
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
};
#define GUI UITheme::getInstance().getTheme()
