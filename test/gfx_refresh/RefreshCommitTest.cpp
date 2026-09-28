#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include "../../src/activities/reader/ReaderRefreshTransaction.h"

TEST(RefreshCommit, FailedBlockingCleanupIsRetriedOnlyOnTheNextSubmission) {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  display.displaySucceeds = false;
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_FALSE(renderer.displayCommitted());
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  EXPECT_EQ(display.refreshCount, 1u);

  display.displaySucceeds = true;
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_TRUE(renderer.displayCommitted());
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.refreshCount, 3u);
}

TEST(RefreshCommit, DeferredFailureRetainsCleanupAndSuccessfulWaitConsumesIt) {
  for (const bool succeeds : {false, true}) {
    HalDisplay display;
    GfxRenderer renderer(display);
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    display.waitSucceeds = succeeds;
    renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
    EXPECT_FALSE(renderer.displayCommitted());
    EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
    renderer.waitRefreshComplete();
    EXPECT_EQ(renderer.displayCommitted(), succeeds);
    EXPECT_EQ(display.refreshCount, 1u);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    EXPECT_EQ(display.lastRefresh, succeeds ? HalDisplay::FAST_REFRESH : HalDisplay::FULL_REFRESH);
  }
}

TEST(RefreshCommit, FadingFixBlockingFailureRetainsAnAsyncCallersCleanup) {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.setFadingFix(true);
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  display.displaySucceeds = false;
  renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
  EXPECT_TRUE(display.lastTurnOff);
  EXPECT_FALSE(display.refreshPending);
  EXPECT_FALSE(renderer.displayCommitted());
  display.displaySucceeds = true;
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
}

TEST(RefreshCommit, DeferredCompletionCannotConsumeANewerCleanupRequest) {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
  renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  renderer.waitRefreshComplete();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
}

TEST(RefreshCommit, CombinedBaseIgnoresEarlierOutputAndCommitsDuringCleanup) {
  for (const bool succeeds : {false, true}) {
    HalDisplay display;
    GfxRenderer renderer(display);
    renderer.begin();
    renderer.displayBuffer();
    ASSERT_TRUE(renderer.displayCommitted());
    display.combinedGray = true;
    display.cleanupSucceeds = succeeds;
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
    EXPECT_FALSE(renderer.displayCommitted());
    EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
    renderer.cleanupGrayscaleWithFrameBuffer();
    EXPECT_EQ(renderer.displayCommitted(), succeeds);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    EXPECT_EQ(display.lastRefresh, succeeds ? HalDisplay::FAST_REFRESH : HalDisplay::FULL_REFRESH);
  }
}

TEST(RefreshCommit, WholePageFailureRestoresForcedCadenceAfterSuccessfulBase) {
  for (const bool cleanupFailure : {false, true}) {
    HalDisplay display;
    GfxRenderer renderer(display);
    renderer.begin();
    int cadence = 1;
    bool forced = true;
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    {
      ReaderRefreshTransaction transaction(renderer, cadence, forced);
      forced = false;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      ASSERT_TRUE(renderer.displayCommitted());
      cadence = 20;
      display.graySucceeds = cleanupFailure;
      renderer.displayGrayBuffer();
      display.cleanupSucceeds = !cleanupFailure;
      renderer.cleanupGrayscaleWithFrameBuffer();
    }
    EXPECT_FALSE(renderer.displayCommitted());
    EXPECT_EQ(cadence, 1);
    EXPECT_TRUE(forced);
    display.cleanupSucceeds = true;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  }
}

TEST(RefreshCommit, SuccessfulWholePageConsumesCadenceAndForcedCleanup) {
  HalDisplay display;
  GfxRenderer renderer(display);
  int cadence = 1;
  bool forced = true;
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  {
    ReaderRefreshTransaction transaction(renderer, cadence, forced);
    forced = false;
    renderer.displayBufferAsync(HalDisplay::HALF_REFRESH);
    cadence = 20;
    renderer.waitRefreshComplete();
    renderer.displayGrayBuffer();
  }
  EXPECT_TRUE(renderer.displayCommitted());
  EXPECT_EQ(cadence, 20);
  EXPECT_FALSE(forced);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
}

TEST(RefreshCommit, PlaceholderSuccessCannotCommitACancelledCombinedPage) {
  HalDisplay display;
  GfxRenderer renderer(display);
  int cadence = 9;
  bool forced = false;
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  {
    ReaderRefreshTransaction transaction(renderer, cadence, forced);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    ASSERT_TRUE(renderer.displayCommitted());
    renderer.beginDisplayWork();
    display.combinedGray = true;
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
    --cadence;
    // The combined page is abandoned before any waveform is activated.
  }
  EXPECT_FALSE(renderer.displayCommitted());
  EXPECT_EQ(cadence, 9);
  EXPECT_FALSE(forced);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
}

TEST(RefreshCommit, EarlyReturnAcceptsACommittedBwFallbackButRestoresAFailedOne) {
  for (const bool succeeds : {false, true}) {
    HalDisplay display;
    GfxRenderer renderer(display);
    int cadence = 6;
    bool forced = true;
    const auto render = [&]() {
      ReaderRefreshTransaction transaction(renderer, cadence, forced);
      forced = false;
      display.acceptsGrayscale = false;
      if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, HalDisplay::FAST_REFRESH)) {
        display.displaySucceeds = succeeds;
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        --cadence;
        return;
      }
      FAIL() << "Expected the unsupported-mode fallback";
    };
    render();
    EXPECT_EQ(renderer.displayCommitted(), succeeds);
    EXPECT_EQ(cadence, succeeds ? 5 : 6);
    EXPECT_EQ(forced, !succeeds);
  }
}

TEST(RefreshCommit, FailureDoesNotDiscardANewForcedRequest) {
  HalDisplay display;
  GfxRenderer renderer(display);
  int cadence = 5;
  bool forced = false;
  {
    ReaderRefreshTransaction transaction(renderer, cadence, forced);
    forced = true;
    --cadence;
  }
  EXPECT_EQ(cadence, 5);
  EXPECT_TRUE(forced);
}
