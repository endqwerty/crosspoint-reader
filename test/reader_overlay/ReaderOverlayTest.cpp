#include <gtest/gtest.h>

#include <array>

#include "ReaderOverlayFixture.h"

class ReaderOverlayTest : public testing::Test {
 protected:
  HalDisplay display;
  OverlayRenderer renderer{display};
  EpubReaderActivity reader{renderer};
  std::array<uint8_t, HalDisplay::BUFFER_SIZE> page{};

  void SetUp() override {
    xteinkPanel = true;
    eegoA4Panel = false;
    SETTINGS.frequency = 15;
    ASSERT_EQ(RenderLock::held, 0);
    renderer.begin();
    for (size_t i = 0; i < page.size(); ++i) page[i] = static_cast<uint8_t>((i * 37) ^ (i >> 3));
  }

  void TearDown() override {
    reader.discardOverlayPage();
    EXPECT_EQ(RenderLock::held, 0);
  }

  void snapshot(bool grayscale) {
    display.frame = page;
    ASSERT_TRUE(renderer.storeBwBuffer());
    reader.overlayPageStored = true;
    reader.renderedPageNeedsGrayscale = grayscale;
    display.frame.fill(0x00);
  }

  void expectClosed() {
    EXPECT_EQ(reader.overlay, EpubReaderActivity::Overlay::None);
    EXPECT_FALSE(reader.overlayPopup.active);
    EXPECT_FALSE(reader.toolbarUi);
    EXPECT_FALSE(reader.overlayPageStored);
  }
};

TEST_F(ReaderOverlayTest, MonochromeSnapshotRestoresExactPixelsWithoutReloadInEveryOrientation) {
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise, GfxRenderer::PortraitInverted,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    renderer.setOrientation(orientation);
    snapshot(false);
    const auto refreshes = display.refreshCount;
    const auto remaining = reader.pagesUntilFullRefresh;
    reader.closeOverlayToPage();
    EXPECT_EQ(display.frame, page);
    EXPECT_EQ(display.refreshCount, refreshes + 1);
    EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
    EXPECT_EQ(reader.pagesUntilFullRefresh, remaining - 1);
    EXPECT_EQ(reader.updateRequests, 0);
    EXPECT_FALSE(renderer.lastResync);
    expectClosed();
  }
}

TEST_F(ReaderOverlayTest, GrayscalePageRequestsRerenderToRestoreItsGrayPlanes) {
  snapshot(true);
  reader.closeOverlayToPage();
  EXPECT_EQ(reader.updateRequests, 1);
  EXPECT_EQ(renderer.restores, 0);
  EXPECT_EQ(display.refreshCount, 0u);
  EXPECT_EQ(reader.pagesUntilFullRefresh, 10);
  expectClosed();
}

TEST_F(ReaderOverlayTest, MissingSnapshotRequestsRerender) {
  reader.renderedPageNeedsGrayscale = false;
  reader.closeOverlayToPage();
  EXPECT_EQ(reader.updateRequests, 1);
  EXPECT_EQ(renderer.restores, 0);
  EXPECT_EQ(display.refreshCount, 0u);
  expectClosed();
}

TEST_F(ReaderOverlayTest, InvalidatedSnapshotCannotRestoreAnOldLayout) {
  snapshot(false);
  reader.discardOverlayPage();
  reader.closeOverlayToPage();
  EXPECT_EQ(reader.updateRequests, 1);
  EXPECT_EQ(renderer.restores, 0);
  EXPECT_EQ(display.refreshCount, 0u);
}

TEST_F(ReaderOverlayTest, RenderStartInvalidatesSnapshotBeforeAnEarlyReturn) {
  snapshot(false);
  reader.epub.reset();
  reader.beginRenderForTest();
  EXPECT_FALSE(reader.overlayPageStored);
  EXPECT_TRUE(reader.renderedPageNeedsGrayscale);
  reader.closeOverlayToPage();
  EXPECT_EQ(reader.updateRequests, 1);
  EXPECT_EQ(renderer.restores, 0);
  EXPECT_EQ(display.refreshCount, 0u);
}

TEST_F(ReaderOverlayTest, ChapterChangeInvalidatesSnapshotBeforeTheRequestedRender) {
  snapshot(false);
  reader.navigationHistory.push(0, 4, ReaderNavigationHistory::Jump::Footnote);
  reader.pendingAnchor = "stale-note";
  reader.changeChapterForTest(1);
  EXPECT_EQ(reader.currentSpineIndex, 1);
  EXPECT_FALSE(reader.overlayPageStored);
  EXPECT_FALSE(reader.section);
  EXPECT_EQ(reader.updateRequests, 1);
  EXPECT_TRUE(reader.navigationHistory.empty());
  EXPECT_TRUE(reader.pendingAnchor.empty());
  reader.closeOverlayToPage();
  EXPECT_EQ(reader.updateRequests, 2);
  EXPECT_EQ(renderer.restores, 0);
  EXPECT_EQ(display.refreshCount, 0u);
}

TEST_F(ReaderOverlayTest, ClampedChapterNavigationDoesNotRepaintOrInvalidateCurrentPage) {
  snapshot(false);
  reader.navigationHistory.push(0, 4, ReaderNavigationHistory::Jump::Footnote);
  reader.changeChapterForTest(-1);
  EXPECT_EQ(reader.currentSpineIndex, 0);
  EXPECT_TRUE(reader.overlayPageStored);
  EXPECT_TRUE(reader.section);
  EXPECT_EQ(reader.updateRequests, 0);
  EXPECT_FALSE(reader.navigationHistory.empty());
  reader.closeOverlayToPage();
  EXPECT_EQ(reader.updateRequests, 0);
  EXPECT_EQ(display.frame, page);
  EXPECT_EQ(display.refreshCount, 1u);
}

TEST_F(ReaderOverlayTest, MonochromeRestoreHonorsScheduledCleanup) {
  snapshot(false);
  reader.pagesUntilFullRefresh = 1;
  reader.closeOverlayToPage();
  EXPECT_EQ(display.frame, page);
  EXPECT_EQ(display.refreshCount, 1u);
  EXPECT_EQ(display.lastRefresh, HalDisplay::HALF_REFRESH);
  EXPECT_EQ(reader.pagesUntilFullRefresh, SETTINGS.frequency);
  EXPECT_EQ(reader.updateRequests, 0);
  EXPECT_FALSE(renderer.lastResync);
}

TEST_F(ReaderOverlayTest, ManualRefreshStillUsesTheReaderRenderPath) {
  snapshot(false);
  reader.forcedRefreshPending = true;
  reader.closeOverlayToPage();
  EXPECT_EQ(reader.updateRequests, 1);
  EXPECT_EQ(renderer.restores, 0);
  EXPECT_EQ(display.refreshCount, 0u);
  EXPECT_TRUE(reader.forcedRefreshPending);
}

TEST_F(ReaderOverlayTest, RendererPromotionStillCleansExactlyOnce) {
  snapshot(false);
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  reader.closeOverlayToPage();
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  EXPECT_EQ(display.refreshCount, 1u);
  EXPECT_EQ(display.frame, page);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
}

TEST_F(ReaderOverlayTest, OtherPanelsKeepExistingSnapshotRestore) {
  xteinkPanel = false;
  snapshot(true);
  reader.closeOverlayToPage();
  EXPECT_EQ(reader.updateRequests, 0);
  EXPECT_EQ(display.refreshCount, 1u);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
  EXPECT_EQ(reader.pagesUntilFullRefresh, 10);
  EXPECT_EQ(display.frame, page);
  EXPECT_FALSE(renderer.lastResync);
}

TEST_F(ReaderOverlayTest, DeferredChromeSettlesBeforeRestoringThePageSnapshot) {
  snapshot(false);
  display.frame.fill(0x55);
  reader.pushOverlayRefresh();
  ASSERT_TRUE(reader.overlayRefreshPending);
  ASSERT_TRUE(display.refreshPending);
  reader.closeOverlayToPage();
  EXPECT_EQ(renderer.settlements, 1u);
  EXPECT_EQ(display.waitCount, 1u);
  EXPECT_EQ(display.firstCleanupByte, 0x55);
  EXPECT_EQ(display.frame, page);
  EXPECT_FALSE(reader.overlayRefreshPending);
  EXPECT_FALSE(display.refreshPending);
  EXPECT_EQ(reader.mappedInput.resets, 1u);
  EXPECT_EQ(reader.updateRequests, 0);
}

TEST_F(ReaderOverlayTest, FailedDeferredChromeKeepsFullPageRecoveryPending) {
  for (bool failWait : {false, true}) {
    snapshot(false);
    display.cleanupSucceeds = failWait;
    display.waitSucceeds = !failWait;
    reader.pushOverlayRefresh();
    reader.closeOverlayToPage();
    EXPECT_TRUE(reader.forcedRefreshPending);
    EXPECT_FALSE(reader.overlayRefreshPending);
    EXPECT_EQ(renderer.restores, 0);
    EXPECT_FALSE(renderer.displayCommitted());
    display.cleanupSucceeds = true;
    display.waitSucceeds = true;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
    EXPECT_TRUE(renderer.displayCommitted());
    reader.forcedRefreshPending = false;
  }
  EXPECT_EQ(reader.updateRequests, 2);
}

TEST_F(ReaderOverlayTest, DeferredChromeSettlesBeforeGrayscaleRerender) {
  snapshot(true);
  reader.pushOverlayRefresh();
  reader.closeOverlayToPage();
  EXPECT_EQ(renderer.settlements, 1u);
  EXPECT_FALSE(display.refreshPending);
  EXPECT_EQ(reader.updateRequests, 1);
  EXPECT_EQ(renderer.restores, 0);
}

TEST_F(ReaderOverlayTest, BlockingChromeDoesNotCreateADeferredRefresh) {
  display.asyncAvailable = false;
  reader.pushOverlayRefresh();
  EXPECT_FALSE(reader.overlayRefreshPending);
  EXPECT_FALSE(display.refreshPending);
  EXPECT_TRUE(renderer.displayCommitted());
  EXPECT_EQ(display.refreshCount, 1u);
}

namespace {
class PaintingChild : public OverlayActivityFixture {
  OverlayRenderer& renderer;
  HalDisplay& display;
  EpubReaderActivity& parent;
  bool recoveryExpected;

 public:
  PaintingChild(OverlayRenderer& renderer, HalDisplay& display, EpubReaderActivity& parent, bool recoveryExpected)
      : renderer(renderer), display(display), parent(parent), recoveryExpected(recoveryExpected) {}
  void onEnter() override {
    EXPECT_EQ(RenderLock::held, 0);
    EXPECT_FALSE(parent.overlayRefreshPending);
    EXPECT_FALSE(display.refreshPending);
    EXPECT_EQ(parent.forcedRefreshPending, recoveryExpected);
    EXPECT_EQ(display.frame[0], 0x55);
    display.frame.fill(0xaa);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
};
}  // namespace

TEST_F(ReaderOverlayTest, ActivityPushSettlesChromeBeforeChildCanPaint) {
  OverlayActivityManagerFixture manager;
  auto parent = std::make_unique<EpubReaderActivity>(renderer);
  auto* suspended = parent.get();
  display.frame.fill(0x55);
  {
    RenderLock lock;
    parent->pushOverlayRefresh();
  }
  manager.pendingActivity = std::make_unique<PaintingChild>(renderer, display, *parent, false);
  manager.currentActivity = std::move(parent);
  manager.transitionForTest();
  ASSERT_EQ(manager.stackActivities.size(), 1u);
  EXPECT_EQ(manager.stackActivities[0].get(), suspended);
  EXPECT_EQ(renderer.settlements, 1u);
  EXPECT_EQ(display.waitCount, 1u);
  EXPECT_EQ(display.firstCleanupByte, 0x55);
  EXPECT_EQ(display.frame[0], 0xaa);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
  EXPECT_EQ(manager.pendingAction, OverlayActivityManagerFixture::PendingAction::None);
}

TEST_F(ReaderOverlayTest, ActivityPushWithoutPendingChromeDoesNotWriteBaseline) {
  OverlayActivityManagerFixture manager;
  auto parent = std::make_unique<EpubReaderActivity>(renderer);
  display.frame.fill(0x55);
  manager.pendingActivity = std::make_unique<PaintingChild>(renderer, display, *parent, false);
  manager.currentActivity = std::move(parent);
  manager.transitionForTest();
  EXPECT_EQ(renderer.settlements, 0u);
  EXPECT_EQ(display.waitCount, 0u);
  EXPECT_EQ(display.cleanupCount, 0u);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
}

TEST_F(ReaderOverlayTest, ActivityPushDropsTurnQueuedBehindPageUpdate) {
  OverlayActivityManagerFixture manager;
  auto parent = std::make_unique<EpubReaderActivity>(renderer);
  auto* suspended = parent.get();
  parent->pendingManualTurn = 1;
  display.frame.fill(0x55);
  manager.pendingActivity = std::make_unique<PaintingChild>(renderer, display, *parent, false);
  manager.currentActivity = std::move(parent);
  manager.transitionForTest();
  ASSERT_EQ(manager.stackActivities.size(), 1u);
  EXPECT_EQ(suspended->pendingManualTurn, 0);
}

TEST_F(ReaderOverlayTest, ActivityPushCarriesFailedChromeRecoveryIntoChildRefresh) {
  for (bool failWait : {false, true}) {
    SCOPED_TRACE(failWait);
    OverlayActivityManagerFixture manager;
    auto parent = std::make_unique<EpubReaderActivity>(renderer);
    display.frame.fill(0x55);
    display.cleanupSucceeds = failWait;
    display.waitSucceeds = !failWait;
    {
      RenderLock lock;
      parent->pushOverlayRefresh();
    }
    manager.pendingActivity = std::make_unique<PaintingChild>(renderer, display, *parent, true);
    manager.currentActivity = std::move(parent);
    manager.transitionForTest();
    EXPECT_EQ(display.firstCleanupByte, 0x55);
    EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
    EXPECT_TRUE(renderer.displayCommitted());
    display.cleanupSucceeds = true;
    display.waitSucceeds = true;
  }
}

TEST_F(ReaderOverlayTest, PushedScreenCannotInheritParentHeaderBackTarget) {
  OverlayActivityManagerFixture manager;
  auto parent = std::make_unique<EpubReaderActivity>(renderer);
  display.frame.fill(0x55);
  manager.pendingActivity = std::make_unique<PaintingChild>(renderer, display, *parent, false);
  manager.currentActivity = std::move(parent);
  HeaderBackTapTarget::set(10, 20, 30, 40);
  ASSERT_TRUE(HeaderBackTapTarget::contains(15, 25));
  manager.transitionForTest();
  EXPECT_FALSE(HeaderBackTapTarget::contains(15, 25));
}

TEST_F(ReaderOverlayTest, A4AlwaysRerendersAfterOverlayWithScheduledCleanup) {
  eegoA4Panel = true;
  snapshot(false);
  reader.closeOverlayToPage();
  EXPECT_EQ(reader.updateRequests, 1);
  EXPECT_EQ(renderer.restores, 0);
  EXPECT_EQ(reader.pagesUntilFullRefresh, 1);
  expectClosed();
}
