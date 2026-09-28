#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

inline unsigned long nowMs = 1000;
inline unsigned long millis() { return nowMs; }

struct NavigationSettings {
  enum { PAGE_TURN, CHAPTER_SKIP, ORIENTATION_CHANGE, ORIENTATION_COUNT = 4 };
  uint8_t orientation = 0;
  int longPressButtonBehavior = PAGE_TURN;
};
inline NavigationSettings SETTINGS;

struct MappedInputManager {
  enum class Button { Power, Down };
  bool powerReleased = false;
  bool downReleased = false;
  bool wasReleased(Button button) const { return button == Button::Power ? powerReleased : downReleased; }
  bool prev = false;
  bool next = false;
  bool fromTilt = false;
  bool touchPrev = false;
  bool touchNext = false;
  unsigned long heldMs = 0;
  unsigned long touchHeldMs = 0;
  unsigned long getHeldTime() const { return heldMs; }
};

namespace ReaderUtils {
inline constexpr unsigned long SKIP_HOLD_MS = 600;
struct TouchPageTurn {
  bool prev;
  bool next;
  unsigned long heldMs;
};
inline TouchPageTurn detectTouchPageTurn(int, const MappedInputManager& input) {
  return {input.touchPrev, input.touchNext, input.touchHeldMs};
}
inline auto detectPageTurn(const MappedInputManager& input) {
  return std::tuple(input.prev, input.next, input.fromTilt);
}
}  // namespace ReaderUtils

class RenderLock {
 public:
  static inline bool busy = false;
  static bool peek() { return busy; }
  RenderLock() = default;
  template <typename T>
  explicit RenderLock(T&) {}
  ~RenderLock() {}
};

class ReaderActivity {
 public:
  virtual ~ReaderActivity() = default;
  MappedInputManager mappedInput;
  int renderer = 0;
  int updates = 0;
  int pagesUntilFullRefresh = 7;
  bool forcedRefreshPending = false;
  bool menuConsumesInput = false;
  bool formatConsumesInput = false;
  bool backConsumesInput = false;
  bool endConsumesInput = false;
  int endHandlerCalls = 0;

  virtual bool pageTurn(bool forward) = 0;
  virtual bool skipPages(int amount) = 0;
  void loop();
  void rememberBookOnceRendered() { ++rememberChecks; }
  int rememberChecks = 0;
  bool handleForcedRefresh();
  void requestUpdate() { ++updates; }
  struct TurnNote {
    bool forward;
    bool succeeded;
    bool operator==(const TurnNote&) const = default;
  };
  std::vector<TurnNote> turnNotes;
  void notePageTurn(bool forward, bool succeeded) { turnNotes.push_back({forward, succeeded}); }
  void clearEndOfBookOptionsIfNeeded() {}
  bool handleEndOfBookMenu() { return menuConsumesInput; }
  bool handleFormatInput() { return formatConsumesInput; }
  bool handleBackNavigation() { return backConsumesInput; }
  bool handleEndOfBookPageTurn(bool, bool) {
    ++endHandlerCalls;
    return endConsumesInput;
  }
};

struct NavigationXtc {
  unsigned pageCount = 12;
  unsigned getPageCount() const { return pageCount; }
};
class XtcReaderActivity : public ReaderActivity {
 public:
  std::unique_ptr<NavigationXtc> xtc = std::make_unique<NavigationXtc>();
  uint32_t currentPage = 0;
  bool pageTurn(bool forward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const;
  void onReturnFromEndOfBook();
};

struct NavigationSection {
  int currentPage = 0;
  int pageCount = 12;
  bool building = false;
  bool isBuilding() const { return building; }
};
struct NavigationEpub {
  int spineCount = 3;
  int getSpineItemsCount() const { return spineCount; }
};
class EpubReaderActivity : public ReaderActivity {
 public:
  std::unique_ptr<NavigationSection> section = std::make_unique<NavigationSection>();
  std::unique_ptr<NavigationEpub> epub = std::make_unique<NavigationEpub>();
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  bool pendingLastPageJump = false;
  bool pendingPercentJump = false;
  bool pendingBuildError = false;
  int pendingManualTurn = 0;
  unsigned long lastPageTurnTime = 0;
  unsigned long pageTurnDuration = 500;
  int deferredClears = 0;

  bool pageTurn(bool forward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const;
  void clearDeferredReposition() { ++deferredClears; }
  bool hasPendingSectionJump() const { return pendingPercentJump || pendingLastPageJump; }
  void clearPendingNavigation() {
    clearDeferredReposition();
    pendingPageJump.reset();
    pendingLastPageJump = false;
    pendingPercentJump = false;
    pendingBuildError = false;
    pendingManualTurn = 0;
  }
  void onReturnFromEndOfBook();
  int orientationChanges = 0;
  void applyOrientation(uint8_t value) {
    SETTINGS.orientation = value;
    ++orientationChanges;
    section.reset();
  }
  void dispatchAutomatic();
  void dispatchInput();
  void dispatchQueued(bool turnGuardActive) {
    mappedInput = {};
    dispatchWithGuard(turnGuardActive);
  }
  void dispatchSkip(bool longPress, bool nextTriggered) {
    mappedInput = {};
    mappedInput.prev = !nextTriggered;
    mappedInput.next = nextTriggered;
    mappedInput.heldMs = longPress ? ReaderUtils::SKIP_HOLD_MS : 0;
    dispatchWithGuard(false);
  }
  void dispatchManual(bool prevTriggered, bool turnGuardActive) {
    mappedInput = {};
    mappedInput.prev = prevTriggered;
    mappedInput.next = !prevTriggered;
    dispatchWithGuard(turnGuardActive);
  }
  void dispatchWithGuard(bool guarded) {
    RenderLock::busy = guarded;
    lastPageTurnTime = nowMs - 200;
    dispatchInput();
    RenderLock::busy = false;
  }
};
