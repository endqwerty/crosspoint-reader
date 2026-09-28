#pragma once

#include <GfxRenderer.h>
#include <Logging.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "ReaderNavigationHistory.h"
#include "components/HeaderBackTapTarget.h"

inline bool xteinkPanel = true;
inline bool xteinkClassPanel() { return xteinkPanel; }

struct SettingsFixture {
  int frequency = 15;
  int getRefreshFrequency() const { return frequency; }
};
inline SettingsFixture SETTINGS;

struct RenderLock {
  inline static int held = 0;
  bool locked = true;
  RenderLock() {
    EXPECT_EQ(held, 0);
    ++held;
  }
  ~RenderLock() { unlock(); }
  void unlock() {
    if (!locked) return;
    --held;
    locked = false;
  }
};

class OverlayRenderer : public GfxRenderer {
 public:
  using GfxRenderer::GfxRenderer;
  int restores = 0;
  bool lastResync = true;
  unsigned settlements = 0;

  void cleanupGrayscaleWithFrameBuffer() {
    EXPECT_EQ(RenderLock::held, 1);
    ++settlements;
    GfxRenderer::cleanupGrayscaleWithFrameBuffer();
  }

  void restoreBwBuffer(bool resyncPanelBaseline = true) {
    EXPECT_EQ(RenderLock::held, 1);
    ++restores;
    lastResync = resyncPanelBaseline;
    GfxRenderer::restoreBwBuffer(resyncPanelBaseline);
  }
};

class OverlayActivityFixture {
 public:
  virtual ~OverlayActivityFixture() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void onSuspend() {}
};

class EpubReaderActivity : public OverlayActivityFixture {
 public:
  enum class Overlay { None, Toolbar, Text };
  struct Popup {
    bool active = true;
    void dismiss() { active = false; }
  };
  struct Book {
    int getSpineItemsCount() const { return 4; }
  };

  explicit EpubReaderActivity(OverlayRenderer& renderer) : renderer(renderer) {}
  OverlayRenderer& renderer;
  Overlay overlay = Overlay::Text;
  Popup overlayPopup;
  std::unique_ptr<int> toolbarUi = std::make_unique<int>(1);
  bool overlayPageStored = false;
  bool overlayRefreshPending = false;
  struct Input {
    unsigned resets = 0;
    void resetHomeButtonInput() { ++resets; }
  } mappedInput;
  bool renderedPageNeedsGrayscale = true;
  bool forcedRefreshPending = false;
  int pagesUntilFullRefresh = 10;
  int updateRequests = 0;
  std::unique_ptr<Book> epub = std::make_unique<Book>();
  std::unique_ptr<int> section = std::make_unique<int>(1);
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::vector<int> currentPageLinks;
  std::vector<int> currentPageFootnotes;
  ReaderNavigationHistory navigationHistory;
  std::optional<uint16_t> pendingPageJump;
  std::optional<uint32_t> pendingOffsetJump;
  std::optional<uint32_t> currentPageVisibleOffset;
  std::string pendingAnchor;
  bool pendingPercentJump = false;
  bool pendingLastPageJump = false;
  bool pendingBuildError = false;
  bool buildHeapPaused = false;
  int pendingManualTurn = 0;

  void requestUpdate() { ++updateRequests; }
  void clearDeferredReposition() {}
  void discardOverlayPage();
  void pushOverlayRefresh();
  void settleOverlayRefresh();
  void onSuspend() override;
  void closeOverlayToPage();
  void beginRenderForTest();
  void changeChapterForTest(int target);
  void clearPendingNavigation();
};

class OverlayActivityManagerFixture {
 public:
  enum class PendingAction { None, Replace, Push };
  PendingAction pendingAction = PendingAction::Push;
  std::unique_ptr<OverlayActivityFixture> currentActivity;
  std::unique_ptr<OverlayActivityFixture> pendingActivity;
  std::vector<std::unique_ptr<OverlayActivityFixture>> stackActivities;
  void exitActivity(const RenderLock&) {
    if (currentActivity) currentActivity->onExit();
    currentActivity.reset();
  }
  void transitionForTest();
};
