#include <gtest/gtest.h>

#include "ReaderRecoveryFixture.h"

TEST(ReaderPageRecovery, FailedTurnRetriesTheTargetAndDoesNotSaveOldProgress) {
  EpubReaderActivity reader;
  reader.section->currentPage = 30;
  reader.renderAttempt();
  ASSERT_EQ(reader.savedPage, 30);
  reader.section->currentPage = 31;
  reader.storage.failRead = true;
  reader.renderAttempt();
  EXPECT_FALSE(reader.section);
  EXPECT_EQ(reader.nextPageNumber, 31);
  EXPECT_EQ(reader.pendingPageJump, 31);
  EXPECT_FALSE(reader.currentPageVisibleOffset);
  EXPECT_EQ(reader.saves, 1);
  EXPECT_EQ(reader.savedPage, 30);
  EXPECT_FALSE(reader.automaticPageTurnActive);
  EXPECT_EQ(reader.storage.abandonCalls, 1);
  EXPECT_EQ(reader.storage.clearCalls, 1);
  EXPECT_EQ(reader.updates, 1);
  const auto position = reader.chapterPosition();
  EXPECT_EQ(position.pageIndex, 31);
  EXPECT_FALSE(position.hasTotal());
  reader.storage.failRead = false;
  reader.resumeSection();
  ASSERT_EQ(reader.section->currentPage, 31);
  reader.renderAttempt();
  EXPECT_EQ(reader.renderedPage, 31);
  EXPECT_EQ(reader.savedPage, 31);
  EXPECT_EQ(reader.saves, 2);
  EXPECT_EQ(reader.pageLoadRetryCount, 0);
  EXPECT_EQ(reader.currentPageVisibleOffset, 3100);
}

TEST(ReaderPageRecovery, RetryLimitPreservesPositionWithoutSavingOrSchedulingAgain) {
  EpubReaderActivity reader;
  reader.section->currentPage = 31;
  reader.storage.failRead = true;
  for (int attempt = 0; attempt <= EpubReaderActivity::MAX_PAGE_LOAD_RETRIES; ++attempt) {
    reader.renderAttempt();
    EXPECT_FALSE(reader.section);
    EXPECT_EQ(reader.nextPageNumber, 31);
    EXPECT_EQ(reader.saves, 0);
    if (attempt < EpubReaderActivity::MAX_PAGE_LOAD_RETRIES) reader.resumeSection();
  }
  EXPECT_EQ(reader.updates, EpubReaderActivity::MAX_PAGE_LOAD_RETRIES);
  EXPECT_EQ(reader.renderer.errors, 1);
  EXPECT_EQ(reader.renderer.displays, 1);
  EXPECT_EQ(reader.pageLoadRetryCount, 0);
  EXPECT_EQ(reader.renderedPage, -1);
  EXPECT_FALSE(reader.pageRendered);
  EXPECT_EQ(reader.chapterPosition().pageIndex, 31);
}

TEST(ReaderPageRecovery, StaleOffsetsAndPageTotalsCannotRemapTheRetry) {
  EpubReaderActivity reader;
  reader.section->currentPage = 31;
  reader.cachedVisibleTextOffset = 500;
  reader.currentPageVisibleOffset = 3000;
  reader.cachedChapterTotalPageCount = 200;
  reader.storage.failRead = true;
  reader.renderAttempt();
  EXPECT_FALSE(reader.cachedVisibleTextOffset);
  EXPECT_EQ(reader.cachedChapterTotalPageCount, 0);
  reader.storage.failRead = false;
  reader.resumeSection();
  EXPECT_EQ(reader.section->currentPage, 31);
}

TEST(ReaderPageRecovery, ResolvedSearchOffsetAndAnchorKeepTheirPageOnFailure) {
  for (bool search : {false, true}) {
    EpubReaderActivity reader;
    reader.section.reset();
    if (search)
      reader.pendingOffsetJump = 4200;
    else
      reader.pendingAnchor = "note";
    reader.resumeSection();
    ASSERT_EQ(reader.section->currentPage, 42);
    EXPECT_FALSE(reader.pendingOffsetJump);
    EXPECT_TRUE(reader.pendingAnchor.empty());
    reader.storage.failRead = true;
    reader.renderAttempt();
    reader.resumeSection();
    EXPECT_EQ(reader.section->currentPage, 42);
    EXPECT_EQ(reader.saves, 0);
  }
}

TEST(ReaderPageRecovery, LinksAndNestedFootnotesKeepHistoryAndViewedProgress) {
  for (bool footnote : {false, true}) {
    EpubReaderActivity reader;
    reader.navigationHistory.push(
        1, 7, footnote ? ReaderNavigationHistory::Jump::Footnote : ReaderNavigationHistory::Jump::Link);
    if (footnote) reader.navigationHistory.push(2, 20, ReaderNavigationHistory::Jump::Footnote);
    reader.section->currentPage = 31;
    reader.storage.failRead = true;
    reader.renderAttempt();
    EXPECT_EQ(reader.navigationHistory.size(), footnote ? 2 : 1);
    reader.storage.failRead = false;
    reader.resumeSection();
    reader.renderAttempt();
    EXPECT_EQ(reader.savedPage, 31);
    EXPECT_EQ(reader.savedSpine, 2);
    EXPECT_EQ(reader.navigationHistory.size(), footnote ? 2 : 1);
  }
}

TEST(ReaderPageRecovery, FailedRetryInsidePartialBuildKeepsPageNumber) {
  EpubReaderActivity reader;
  reader.storage.building = true;
  reader.section->currentPage = 31;
  reader.storage.failRead = true;
  reader.renderAttempt();
  reader.resumeSection();
  EXPECT_EQ(reader.section->currentPage, 31);
  reader.storage.building = false;
  EXPECT_FALSE(reader.applyDeferredReposition());
  EXPECT_EQ(reader.section->currentPage, 31);
}

TEST(ReaderPageRecovery, DeferredReflowInvalidatesVisibleOffsetOnlyWhenThePageChanges) {
  EpubReaderActivity reader;
  reader.section->currentPage = 5;
  reader.currentPageVisibleOffset = 500;
  reader.cachedVisibleTextOffset = 800;
  EXPECT_TRUE(reader.applyDeferredReposition());
  EXPECT_EQ(reader.section->currentPage, 8);
  EXPECT_FALSE(reader.currentPageVisibleOffset);

  reader.currentPageVisibleOffset = 800;
  reader.cachedVisibleTextOffset = 800;
  EXPECT_FALSE(reader.applyDeferredReposition());
  EXPECT_EQ(reader.currentPageVisibleOffset, 800);
}

TEST(ReaderPageRecovery, FailedPanelUpdatePreservesProgressAndStopsAutomaticTurns) {
  EpubReaderActivity reader;
  reader.section->currentPage = 30;
  reader.renderAttempt();
  ASSERT_EQ(reader.savedPage, 30);
  reader.section->currentPage = 31;
  reader.renderer.committed = false;
  reader.renderAttempt();
  EXPECT_EQ(reader.saves, 1);
  EXPECT_EQ(reader.savedPage, 30);
  EXPECT_FALSE(reader.automaticPageTurnActive);
  EXPECT_EQ(reader.lastRenderCompleteMs, 0u);
  EXPECT_EQ(reader.updates, 0);
  ASSERT_TRUE(reader.section);
  EXPECT_EQ(reader.section->currentPage, 31);
  EXPECT_EQ(reader.storage.clearCalls, 0);
  reader.renderer.committed = true;
  reader.renderAttempt();
  EXPECT_EQ(reader.savedPage, 31);
  EXPECT_EQ(reader.saves, 2);
  EXPECT_EQ(reader.lastRenderCompleteMs, millis());
  reader.renderAttempt();
  EXPECT_EQ(reader.saves, 2);
}

TEST(ReaderPageRecovery, InitialPanelFailureNeverCreatesSavedProgress) {
  EpubReaderActivity reader;
  reader.section->currentPage = 31;
  reader.renderer.committed = false;
  for (int attempt = 0; attempt < 3; ++attempt) reader.renderAttempt();
  EXPECT_FALSE(reader.pageRendered);
  EXPECT_EQ(reader.saves, 0);
  EXPECT_EQ(reader.savedPage, -1);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_EQ(reader.storage.clearCalls, 0);
  reader.renderer.committed = true;
  reader.renderAttempt();
  EXPECT_TRUE(reader.pageRendered);
  EXPECT_EQ(reader.savedPage, 31);
  EXPECT_EQ(reader.saves, 1);
}
