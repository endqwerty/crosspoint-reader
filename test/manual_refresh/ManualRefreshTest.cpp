#include <algorithm>
#include <utility>

#include "ManualRefreshFixture.h"

class ManualRefreshTest : public ::testing::Test {
 protected:
  HalDisplay display;
  PanelRenderer renderer{display};
  FrontlightPanelActivity panel{renderer};

  void SetUp() override {
    SETTINGS = {};
    RenderLock::held = 0;
    RenderLock::acquisitions = 0;
    renderer.begin();
  }
  void TearDown() override { EXPECT_EQ(RenderLock::held, 0u); }
  void tapTile(int tile) {
    panel.routedTile = tile;
    panel.loop();
    panel.touch = {};
  }
  void paintPanel() {
    RenderLock lock;
    panel.render(std::move(lock));
  }
  void exitPanel() {
    // ActivityManager holds this lock for pop, replacement, and stack teardown.
    RenderLock lock;
    panel.onExit();
  }
  void paintDestination() {
    RenderLock lock;
    renderer.clearScreen(0xa5);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
};

TEST_F(ManualRefreshTest, LatePanelPaintCannotStealCleanupFromUnderlyingPage) {
  tapTile(1);
  EXPECT_TRUE(panel.closeWasLocked);
  EXPECT_EQ(panel.closeCalls, 1u);
  EXPECT_EQ(RenderLock::acquisitions, 1u);
  EXPECT_EQ(renderer.promotions, 0u);

  paintPanel();
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.refreshCount, 1u);
  EXPECT_TRUE(std::all_of(display.frame.begin(), display.frame.end(), [](uint8_t byte) { return byte == 0x33; }));

  exitPanel();
  EXPECT_EQ(renderer.promotions, 1u);
  EXPECT_EQ(display.refreshCount, 1u);
  EXPECT_EQ(panel.exitCalls, 1u);
  EXPECT_EQ(SETTINGS.writes, 0u);
  paintDestination();
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  EXPECT_EQ(display.refreshCount, 2u);
  EXPECT_TRUE(std::all_of(display.frame.begin(), display.frame.end(), [](uint8_t byte) { return byte == 0xa5; }));

  paintDestination();
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
  EXPECT_EQ(renderer.promotions, 1u);
}

TEST_F(ManualRefreshTest, OrdinaryDismissalDoesNotAddCleanupOrSettingsWrites) {
  panel.mappedInput.backReleased = true;
  panel.loop();
  EXPECT_TRUE(panel.closeWasLocked);
  paintPanel();
  exitPanel();
  paintDestination();
  EXPECT_EQ(renderer.promotions, 0u);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
  EXPECT_EQ(SETTINGS.writes, 0u);
}

TEST_F(ManualRefreshTest, PendingNightPaintKeepsHalfAndLeavesFullForDestination) {
  tapTile(0);
  tapTile(1);
  paintPanel();
  EXPECT_EQ(display.lastRefresh, HalDisplay::HALF_REFRESH);
  EXPECT_FALSE(panel.cleanRefreshPending);
  exitPanel();
  paintDestination();
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  EXPECT_EQ(SETTINGS.writes, 1u);
}

TEST_F(ManualRefreshTest, RepeatedRefreshRequestsCoalesceUntilExit) {
  tapTile(1);
  paintPanel();
  tapTile(1);
  paintPanel();
  EXPECT_EQ(renderer.promotions, 0u);
  exitPanel();
  paintDestination();
  EXPECT_EQ(renderer.promotions, 1u);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  EXPECT_FALSE(panel.refreshOnExit);
}

TEST_F(ManualRefreshTest, OrdinaryExitKeepsAnotherCallersPromotion) {
  {
    RenderLock lock;
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  }
  exitPanel();
  paintDestination();
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  EXPECT_EQ(renderer.promotions, 1u);
}

TEST_F(ManualRefreshTest, RenderCallbackUsesExistingLockWithoutNestedAcquisition) {
  panel.renderTile = 1;
  paintPanel();
  EXPECT_TRUE(panel.closeWasLocked);
  EXPECT_EQ(RenderLock::acquisitions, 1u);
  EXPECT_EQ(renderer.promotions, 0u);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
  exitPanel();
  paintDestination();
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
}

TEST_F(ManualRefreshTest, PhysicalAndGestureCallbacksStayInsideLoopLock) {
  panel.mappedInput.confirmReleased = true;
  panel.loop();
  EXPECT_TRUE(panel.lightOn);
  EXPECT_EQ(panel.updateRequests, 1u);
  EXPECT_EQ(RenderLock::acquisitions, 1u);
  panel.mappedInput.confirmReleased = false;
  panel.mappedInput.swipe = MappedInputManager::SwipeDir::Up;
  panel.touch.routed = true;
  panel.loop();
  EXPECT_TRUE(panel.closeWasLocked);
  EXPECT_EQ(panel.closeCalls, 1u);
  EXPECT_EQ(RenderLock::acquisitions, 2u);
  EXPECT_EQ(RenderLock::held, 0u);
}

TEST_F(ManualRefreshTest, IdleLoopAndDragReleaseReleaseLockWithoutRefresh) {
  panel.loop();
  panel.draggingSlider = true;
  panel.touch.snap.touchHeld = false;
  panel.loop();
  EXPECT_FALSE(panel.draggingSlider);
  EXPECT_EQ(panel.closeCalls, 0u);
  EXPECT_EQ(display.refreshCount, 0u);
  EXPECT_EQ(RenderLock::acquisitions, 2u);
}

enum class Destination { BwAsync, Overlay, Absolute, Direct, UnsupportedGray };
class ManualRefreshDestinationTest : public ManualRefreshTest, public ::testing::WithParamInterface<Destination> {};

TEST_P(ManualRefreshDestinationTest, ExitCleanupReachesNextPaintThroughExistingRendererPaths) {
  tapTile(1);
  paintPanel();
  ASSERT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
  exitPanel();

  {
    RenderLock lock;
    renderer.clearScreen(0xa5);
    switch (GetParam()) {
      case Destination::BwAsync:
        renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
        renderer.waitRefreshComplete();
        break;
      case Destination::Overlay:
        renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
        break;
      case Destination::Absolute:
        ASSERT_TRUE(renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, HalDisplay::FAST_REFRESH));
        break;
      case Destination::Direct:
        ASSERT_TRUE(renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Direct, HalDisplay::FAST_REFRESH));
        break;
      case Destination::UnsupportedGray:
        display.acceptsGrayscale = false;
        ASSERT_FALSE(renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, HalDisplay::FAST_REFRESH));
        EXPECT_EQ(display.refreshCount, 1u);
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        break;
    }
  }
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  EXPECT_EQ(display.refreshCount, 2u);
  paintDestination();
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
}

INSTANTIATE_TEST_SUITE_P(ReaderAndReplacementScreens, ManualRefreshDestinationTest,
                         ::testing::Values(Destination::BwAsync, Destination::Overlay, Destination::Absolute,
                                           Destination::Direct, Destination::UnsupportedGray));
