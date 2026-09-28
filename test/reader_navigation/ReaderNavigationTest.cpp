#include <gtest/gtest.h>

#include "ReaderNavigationFixture.h"

class ReaderNavigationTest : public testing::Test {
 protected:
  void SetUp() override {
    SETTINGS = {};
    nowMs = 1000;
    RenderLock::busy = false;
  }
};

template <typename Reader>
void checkBoundaryInputs() {
  Reader reader;
  reader.mappedInput.prev = true;
  for (int i = 0; i < 20; ++i) reader.loop();
  EXPECT_EQ(reader.currentPage, 0u);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_EQ(reader.pagesUntilFullRefresh, 7);
  EXPECT_FALSE(reader.forcedRefreshPending);

  SETTINGS.longPressButtonBehavior = SETTINGS.CHAPTER_SKIP;
  reader.mappedInput.heldMs = ReaderUtils::SKIP_HOLD_MS;
  for (int i = 0; i < 20; ++i) reader.loop();
  EXPECT_EQ(reader.currentPage, 0u);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_EQ(reader.pagesUntilFullRefresh, 7);
}

TEST_F(ReaderNavigationTest, XtcBoundaryBackAndSkipDoNotRedraw) { checkBoundaryInputs<XtcReaderActivity>(); }

template <typename Reader>
void checkSuccessfulInputs() {
  Reader reader;
  reader.mappedInput.next = true;
  reader.loop();
  EXPECT_EQ(reader.currentPage, 1u);
  EXPECT_EQ(reader.updates, 1);

  reader.mappedInput.next = false;
  reader.mappedInput.prev = true;
  reader.loop();
  EXPECT_EQ(reader.currentPage, 0u);
  EXPECT_EQ(reader.updates, 2);

  SETTINGS.longPressButtonBehavior = SETTINGS.CHAPTER_SKIP;
  reader.mappedInput.heldMs = ReaderUtils::SKIP_HOLD_MS;
  reader.mappedInput.prev = false;
  reader.mappedInput.next = true;
  reader.loop();
  EXPECT_EQ(reader.currentPage, 10u);
  EXPECT_EQ(reader.updates, 3);

  reader.mappedInput.next = false;
  reader.mappedInput.prev = true;
  reader.loop();
  EXPECT_EQ(reader.currentPage, 0u);
  EXPECT_EQ(reader.updates, 4);
}

TEST_F(ReaderNavigationTest, XtcSuccessfulForwardBackAndSkipRedrawOnce) { checkSuccessfulInputs<XtcReaderActivity>(); }

TEST_F(ReaderNavigationTest, MissingXtcDoesNotRequestAnotherRedraw) {
  XtcReaderActivity xtc;
  xtc.xtc.reset();
  xtc.mappedInput.next = true;
  xtc.loop();
  EXPECT_EQ(xtc.updates, 0);
  EXPECT_EQ(xtc.currentPage, 0u);
}

TEST_F(ReaderNavigationTest, TouchUsesTheSameNoChangeContract) {
  XtcReaderActivity reader;
  reader.mappedInput.touchPrev = true;
  reader.loop();
  EXPECT_EQ(reader.updates, 0);
  reader.mappedInput.touchPrev = false;
  reader.mappedInput.touchNext = true;
  reader.loop();
  EXPECT_EQ(reader.currentPage, 1u);
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, TiltDoesNotBecomeChapterSkip) {
  XtcReaderActivity reader;
  SETTINGS.longPressButtonBehavior = SETTINGS.CHAPTER_SKIP;
  reader.mappedInput.next = true;
  reader.mappedInput.fromTilt = true;
  reader.mappedInput.heldMs = ReaderUtils::SKIP_HOLD_MS;
  reader.loop();
  EXPECT_EQ(reader.currentPage, 1u);
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, EndOfBookSentinelsStillRequestAnUpdate) {
  XtcReaderActivity xtc;
  xtc.currentPage = xtc.xtc->getPageCount() - 1;
  xtc.mappedInput.next = true;
  xtc.loop();
  EXPECT_TRUE(xtc.isAtEndOfBook());
  EXPECT_EQ(xtc.updates, 1);
  xtc.endConsumesInput = true;
  xtc.loop();
  EXPECT_EQ(xtc.updates, 1);
  EXPECT_EQ(xtc.endHandlerCalls, 2);
}

TEST_F(ReaderNavigationTest, SkipCanStillReachEndOfBook) {
  XtcReaderActivity reader;
  reader.currentPage = 7;
  SETTINGS.longPressButtonBehavior = SETTINGS.CHAPTER_SKIP;
  reader.mappedInput.next = true;
  reader.mappedInput.heldMs = ReaderUtils::SKIP_HOLD_MS;
  reader.loop();
  EXPECT_TRUE(reader.isAtEndOfBook());
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, ExistingInputOwnersPreventNavigation) {
  XtcReaderActivity reader;
  reader.mappedInput.next = true;
  reader.menuConsumesInput = true;
  reader.loop();
  reader.menuConsumesInput = false;
  reader.formatConsumesInput = true;
  reader.loop();
  reader.formatConsumesInput = false;
  reader.backConsumesInput = true;
  reader.loop();
  EXPECT_EQ(reader.currentPage, 0u);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_EQ(reader.endHandlerCalls, 0);
}

TEST_F(ReaderNavigationTest, ExplicitRefreshStillRequestsCleanupOnAnUnchangedPage) {
  XtcReaderActivity reader;
  reader.mappedInput.prev = true;
  reader.loop();
  ASSERT_EQ(reader.updates, 0);
  EXPECT_TRUE(reader.handleForcedRefresh());
  EXPECT_EQ(reader.currentPage, 0u);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_EQ(reader.pagesUntilFullRefresh, 1);
  EXPECT_TRUE(reader.forcedRefreshPending);
}

TEST_F(ReaderNavigationTest, EpubImmediateBoundaryDoesNotRedraw) {
  EpubReaderActivity reader;
  for (int i = 0; i < 20; ++i) reader.dispatchManual(true, false);
  EXPECT_EQ(reader.section->currentPage, 0);
  EXPECT_EQ(reader.currentSpineIndex, 0);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_EQ(reader.pagesUntilFullRefresh, 7);
  EXPECT_EQ(reader.lastPageTurnTime, nowMs - 200);
}

TEST_F(ReaderNavigationTest, EpubImmediateForwardAndBackRedrawOnceEach) {
  EpubReaderActivity reader;
  reader.dispatchManual(false, false);
  EXPECT_EQ(reader.section->currentPage, 1);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_EQ(reader.lastPageTurnTime, nowMs);
  reader.dispatchManual(true, false);
  EXPECT_EQ(reader.section->currentPage, 0);
  EXPECT_EQ(reader.updates, 2);
}

TEST_F(ReaderNavigationTest, EpubQueuedBoundaryIsConsumedWithoutRedraw) {
  EpubReaderActivity reader;
  reader.dispatchManual(true, true);
  EXPECT_EQ(reader.pendingManualTurn, -1);
  reader.dispatchQueued(true);
  EXPECT_EQ(reader.pendingManualTurn, -1);
  EXPECT_EQ(reader.updates, 0);
  reader.dispatchQueued(false);
  EXPECT_EQ(reader.pendingManualTurn, 0);
  EXPECT_EQ(reader.section->currentPage, 0);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_EQ(reader.pagesUntilFullRefresh, 7);
  EXPECT_EQ(reader.turnNotes, (std::vector<ReaderActivity::TurnNote>{{false, false}}));
}

TEST_F(ReaderNavigationTest, EpubQueuedForwardIsAppliedOnceAfterGuardClears) {
  EpubReaderActivity reader;
  reader.dispatchManual(false, true);
  ASSERT_EQ(reader.pendingManualTurn, 1);
  reader.dispatchQueued(false);
  EXPECT_EQ(reader.pendingManualTurn, 0);
  EXPECT_EQ(reader.section->currentPage, 1);
  EXPECT_EQ(reader.updates, 1);
  reader.dispatchQueued(false);
  EXPECT_EQ(reader.section->currentPage, 1);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_EQ(reader.turnNotes, (std::vector<ReaderActivity::TurnNote>{{true, true}}));
}

TEST_F(ReaderNavigationTest, EpubQueuedTurnWithoutSectionIsDiscarded) {
  EpubReaderActivity reader;
  reader.pendingManualTurn = 1;
  reader.section.reset();
  reader.dispatchQueued(false);
  EXPECT_EQ(reader.pendingManualTurn, 0);
  EXPECT_EQ(reader.updates, 0);
}

TEST_F(ReaderNavigationTest, EpubChapterSkipAtBookStartDoesNotRedraw) {
  EpubReaderActivity reader;
  SETTINGS.longPressButtonBehavior = SETTINGS.CHAPTER_SKIP;
  reader.dispatchSkip(true, false);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_EQ(reader.section->currentPage, 0);
  EXPECT_EQ(reader.currentSpineIndex, 0);
}

TEST_F(ReaderNavigationTest, EpubChapterSkipToStartAndPreviousChapterStillRedraws) {
  EpubReaderActivity reader;
  SETTINGS.longPressButtonBehavior = SETTINGS.CHAPTER_SKIP;
  reader.currentSpineIndex = 1;
  reader.section->currentPage = 4;
  reader.dispatchSkip(true, false);
  EXPECT_EQ(reader.section->currentPage, 0);
  EXPECT_EQ(reader.currentSpineIndex, 1);
  EXPECT_EQ(reader.updates, 1);
  reader.dispatchSkip(true, false);
  EXPECT_EQ(reader.currentSpineIndex, 0);
  EXPECT_FALSE(reader.section);
  EXPECT_EQ(reader.updates, 2);
}

TEST_F(ReaderNavigationTest, EpubForwardAndBackwardChapterCrossingsStillRedraw) {
  EpubReaderActivity forward;
  forward.section->currentPage = forward.section->pageCount - 1;
  forward.dispatchManual(false, false);
  EXPECT_EQ(forward.currentSpineIndex, 1);
  EXPECT_FALSE(forward.section);
  EXPECT_EQ(forward.nextPageNumber, 0);
  EXPECT_EQ(forward.updates, 1);

  EpubReaderActivity backward;
  backward.currentSpineIndex = 1;
  backward.dispatchManual(true, false);
  EXPECT_EQ(backward.currentSpineIndex, 0);
  EXPECT_FALSE(backward.section);
  EXPECT_TRUE(backward.pendingLastPageJump);
  EXPECT_FALSE(backward.pendingPageJump);
  EXPECT_EQ(backward.updates, 1);
}

TEST_F(ReaderNavigationTest, EpubBackwardChapterJumpSupersedesPendingPercent) {
  EpubReaderActivity reader;
  reader.currentSpineIndex = 2;
  reader.pendingPercentJump = true;
  reader.pendingPageJump = 23;
  ASSERT_TRUE(reader.pageTurn(false));
  EXPECT_EQ(reader.currentSpineIndex, 1);
  EXPECT_TRUE(reader.pendingLastPageJump);
  EXPECT_FALSE(reader.pendingPercentJump);
  EXPECT_FALSE(reader.pendingPageJump);
}

TEST_F(ReaderNavigationTest, EpubReturnFromEndReleasesOldSectionAndRequestsActualLastPage) {
  EpubReaderActivity reader;
  reader.currentSpineIndex = reader.epub->spineCount;
  reader.pendingPercentJump = true;
  reader.pendingPageJump = 23;
  reader.onReturnFromEndOfBook();
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.nextPageNumber, 0);
  EXPECT_FALSE(reader.section);
  EXPECT_TRUE(reader.pendingLastPageJump);
  EXPECT_FALSE(reader.pendingPercentJump);
  EXPECT_FALSE(reader.pendingPageJump);
}

TEST_F(ReaderNavigationTest, EpubChapterSkipCancelsPendingLastPage) {
  EpubReaderActivity reader;
  reader.pendingLastPageJump = true;
  ASSERT_TRUE(reader.skipPages(1));
  EXPECT_EQ(reader.currentSpineIndex, 1);
  EXPECT_FALSE(reader.pendingLastPageJump);
  EXPECT_FALSE(reader.pendingPercentJump);
}

TEST_F(ReaderNavigationTest, EpubBuildingPageRequestsTheRenderThatExtendsTheSection) {
  EpubReaderActivity reader;
  reader.section->currentPage = reader.section->pageCount - 1;
  reader.section->building = true;
  reader.dispatchManual(false, false);
  ASSERT_TRUE(reader.section);
  EXPECT_EQ(reader.section->currentPage, reader.section->pageCount);
  EXPECT_EQ(reader.currentSpineIndex, 0);
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, EpubFinalPageAndForwardChapterSkipStillReachEndOfBook) {
  EpubReaderActivity turn;
  turn.currentSpineIndex = turn.epub->getSpineItemsCount() - 1;
  turn.section->currentPage = turn.section->pageCount - 1;
  turn.dispatchManual(false, false);
  EXPECT_TRUE(turn.isAtEndOfBook());
  EXPECT_EQ(turn.updates, 1);

  EpubReaderActivity skip;
  skip.currentSpineIndex = skip.epub->getSpineItemsCount() - 1;
  SETTINGS.longPressButtonBehavior = SETTINGS.CHAPTER_SKIP;
  skip.dispatchSkip(true, true);
  EXPECT_TRUE(skip.isAtEndOfBook());
  EXPECT_EQ(skip.updates, 1);
}

TEST_F(ReaderNavigationTest, EpubAutomaticTurnHonorsTimerAndUpdatesOnce) {
  EpubReaderActivity reader;
  reader.lastPageTurnTime = nowMs;
  reader.dispatchAutomatic();
  EXPECT_EQ(reader.updates, 0);
  nowMs += reader.pageTurnDuration;
  reader.dispatchAutomatic();
  EXPECT_EQ(reader.section->currentPage, 1);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_EQ(reader.lastPageTurnTime, nowMs);
  reader.dispatchAutomatic();
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, EpubAutomaticTurnWaitsForPendingTargetOrBuildFailure) {
  for (int state = 0; state < 3; ++state) {
    EpubReaderActivity reader;
    reader.pendingLastPageJump = state == 0;
    reader.pendingPercentJump = state == 1;
    reader.pendingBuildError = state == 2;
    reader.lastPageTurnTime = 0;
    reader.dispatchAutomatic();
    EXPECT_EQ(reader.updates, 0);
    EXPECT_EQ(reader.section->currentPage, 0);
    EXPECT_EQ(reader.lastPageTurnTime, nowMs);
    EXPECT_TRUE(reader.hasPendingSectionJump() || reader.pendingBuildError);
  }
}

TEST_F(ReaderNavigationTest, EpubExplicitRefreshIsUnaffectedByRejectedBoundaryTurn) {
  EpubReaderActivity reader;
  reader.dispatchManual(true, false);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_TRUE(reader.handleForcedRefresh());
  EXPECT_EQ(reader.updates, 1);
  EXPECT_EQ(reader.pagesUntilFullRefresh, 1);
  EXPECT_TRUE(reader.forcedRefreshPending);
}

TEST_F(ReaderNavigationTest, EpubFreshReverseWinsWhenQueuedTurnBecomesReady) {
  EpubReaderActivity reader;
  reader.section->currentPage = 5;
  reader.lastPageTurnTime = nowMs;
  reader.mappedInput.next = true;
  reader.dispatchInput();
  ASSERT_EQ(reader.pendingManualTurn, 1);
  ASSERT_EQ(reader.updates, 0);
  nowMs += 200;
  reader.mappedInput.next = false;
  reader.mappedInput.prev = true;
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 4);
  EXPECT_EQ(reader.pendingManualTurn, 0);
  EXPECT_EQ(reader.updates, 1);
  reader.mappedInput = {};
  nowMs += 200;
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 4);
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, EpubRapidFramesRetainLatestDirectionUntilGuardExpires) {
  EpubReaderActivity reader;
  reader.section->currentPage = 5;
  reader.lastPageTurnTime = nowMs;
  reader.mappedInput.next = true;
  reader.dispatchInput();
  nowMs += 50;
  reader.mappedInput.next = false;
  reader.mappedInput.prev = true;
  reader.dispatchInput();
  EXPECT_EQ(reader.pendingManualTurn, -1);
  nowMs += 50;
  reader.mappedInput.prev = false;
  reader.mappedInput.touchNext = true;
  reader.dispatchInput();
  EXPECT_EQ(reader.pendingManualTurn, 1);
  reader.mappedInput = {};
  nowMs += 100;
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 6);
  EXPECT_EQ(reader.pendingManualTurn, 0);
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, EpubSameDirectionCollisionKeepsOnePendingIntent) {
  EpubReaderActivity reader;
  reader.lastPageTurnTime = nowMs;
  reader.mappedInput.next = true;
  reader.dispatchInput();
  nowMs += 200;
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 1);
  EXPECT_EQ(reader.updates, 1);
  reader.mappedInput = {};
  nowMs += 200;
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 1);
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, EpubRenderLockQueuesLatestInputWithoutTurning) {
  EpubReaderActivity reader;
  reader.section->currentPage = 5;
  RenderLock::busy = true;
  reader.mappedInput.next = true;
  reader.dispatchInput();
  nowMs += 1000;
  reader.mappedInput.next = false;
  reader.mappedInput.prev = true;
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 5);
  EXPECT_EQ(reader.pendingManualTurn, -1);
  RenderLock::busy = false;
  reader.mappedInput = {};
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 4);
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, EpubChapterSkipClearsQueuedTurnAcrossSectionReload) {
  for (bool guarded : {false, true}) {
    EpubReaderActivity reader;
    SETTINGS.longPressButtonBehavior = SETTINGS.CHAPTER_SKIP;
    reader.pendingManualTurn = -1;
    reader.mappedInput.next = true;
    reader.mappedInput.heldMs = ReaderUtils::SKIP_HOLD_MS;
    RenderLock::busy = guarded;
    reader.dispatchInput();
    ASSERT_FALSE(reader.section);
    EXPECT_EQ(reader.currentSpineIndex, 1);
    EXPECT_EQ(reader.pendingManualTurn, 0);
    reader.section = std::make_unique<NavigationSection>();
    reader.mappedInput = {};
    RenderLock::busy = false;
    nowMs += 200;
    reader.dispatchInput();
    EXPECT_EQ(reader.section->currentPage, 0);
    EXPECT_EQ(reader.updates, 1);
  }
}

TEST_F(ReaderNavigationTest, EpubOrientationAndEndOfBookActionsClearQueuedTurns) {
  EpubReaderActivity orientation;
  SETTINGS.longPressButtonBehavior = SETTINGS.ORIENTATION_CHANGE;
  orientation.pendingManualTurn = 1;
  orientation.mappedInput.prev = true;
  orientation.mappedInput.heldMs = ReaderUtils::SKIP_HOLD_MS;
  orientation.dispatchInput();
  EXPECT_EQ(orientation.orientationChanges, 1);
  EXPECT_EQ(orientation.pendingManualTurn, 0);
  EXPECT_EQ(orientation.updates, 1);

  EpubReaderActivity end;
  end.pendingManualTurn = 1;
  end.endConsumesInput = true;
  end.mappedInput.prev = true;
  end.dispatchInput();
  EXPECT_EQ(end.endHandlerCalls, 1);
  EXPECT_EQ(end.pendingManualTurn, 0);
  EXPECT_EQ(end.section->currentPage, 0);
}

TEST_F(ReaderNavigationTest, EpubScreenshotCombinationNeverDrainsOrReplacesQueuedTurn) {
  EpubReaderActivity reader;
  reader.pendingManualTurn = -1;
  reader.section->currentPage = 5;
  reader.mappedInput.next = true;
  reader.mappedInput.powerReleased = true;
  reader.mappedInput.downReleased = true;
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 5);
  EXPECT_EQ(reader.pendingManualTurn, -1);
  EXPECT_EQ(reader.updates, 0);
  reader.mappedInput = {};
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 4);
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderNavigationTest, EpubTiltUsesPageTurnEvenWithLongPressActionEnabled) {
  EpubReaderActivity reader;
  SETTINGS.longPressButtonBehavior = SETTINGS.CHAPTER_SKIP;
  reader.pendingManualTurn = -1;
  reader.mappedInput.next = true;
  reader.mappedInput.fromTilt = true;
  reader.mappedInput.heldMs = ReaderUtils::SKIP_HOLD_MS;
  reader.dispatchInput();
  EXPECT_EQ(reader.section->currentPage, 1);
  EXPECT_EQ(reader.currentSpineIndex, 0);
  EXPECT_EQ(reader.pendingManualTurn, 0);
}

TEST_F(ReaderNavigationTest, QueuedTouchFeedbackWaitsForSuccessfulTurn) {
  EpubReaderActivity reader;
  haptic_feedback::taps = 0;
  RenderLock::busy = true;
  reader.mappedInput.touchNext = true;
  reader.dispatchInput();
  EXPECT_EQ(haptic_feedback::taps, 0);
  EXPECT_TRUE(reader.pendingManualTurnTouch);
  reader.mappedInput.touchNext = false;
  RenderLock::busy = false;
  reader.dispatchInput();
  EXPECT_EQ(haptic_feedback::taps, 1);
  EXPECT_FALSE(reader.pendingManualTurnTouch);
  reader.dispatchInput();
  EXPECT_EQ(haptic_feedback::taps, 1);
}
