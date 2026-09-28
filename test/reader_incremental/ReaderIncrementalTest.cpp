#include <gtest/gtest.h>

#include <cstdio>

#include "ReaderIncrementalFixture.h"

namespace {
bool querySchedulingHint(EpubReaderActivity& reader) {
  RenderLock lock(RenderLock::Mode::Try);
  return lock.ownsLock() && reader.skipLoopDelay();
}
}  // namespace

class ReaderIncrementalTest : public testing::TestWithParam<bool> {
 protected:
  EpubReaderActivity reader;
  void setJump() {
    reader.pendingPercentJump = GetParam();
    reader.pendingLastPageJump = !GetParam();
  }
  int expectedPage() const { return GetParam() ? reader.storage.totalPages / 2 : reader.storage.totalPages - 1; }
  void complete() {
    int ticks = 0;
    while (reader.hasPendingSectionJump() && !reader.pendingBuildError && ticks++ < 10000) {
      reader.advanceSectionBuild();
    }
    ASSERT_LT(ticks, 10000);
  }
};

TEST_P(ReaderIncrementalTest, UncachedJumpDefersAllPaginationAndProgressUntilTargetCommits) {
  setJump();
  reader.renderBook();
  ASSERT_TRUE(reader.section);
  EXPECT_FALSE(reader.pageRendered);
  EXPECT_EQ(reader.storage.buildsStarted, 1);
  EXPECT_EQ(reader.storage.chunks, 0);
  EXPECT_EQ(reader.storage.pageReads, 0);
  EXPECT_EQ(reader.saves, 0);
  EXPECT_EQ(GUI.indexingPopups, 1);
  EXPECT_TRUE(querySchedulingHint(reader));
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.pagesBuilt, 2);
  EXPECT_TRUE(reader.hasPendingSectionJump());
  reader.renderBook();
  EXPECT_EQ(reader.storage.chunks, 1);
  EXPECT_EQ(reader.storage.pageReads, 0);
  EXPECT_EQ(GUI.indexingPopups, 1);
  complete();
  EXPECT_EQ(reader.section->currentPage, expectedPage());
  EXPECT_EQ(reader.storage.largestChunk, 2);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_FALSE(querySchedulingHint(reader));
  EXPECT_FALSE(reader.currentPageVisibleOffset);
  EXPECT_EQ(reader.lastPageTurnTime, millis());
  EXPECT_EQ(reader.saves, 0);
  reader.renderer.committed = false;
  reader.renderBook();
  EXPECT_EQ(reader.saves, 0);
  EXPECT_FALSE(reader.currentPageVisibleOffset);
  EXPECT_FALSE(reader.pageRendered);
  reader.renderer.committed = true;
  reader.renderBook();
  EXPECT_TRUE(reader.pageRendered);
  EXPECT_EQ(reader.savedPage, expectedPage());
  EXPECT_EQ(reader.saves, 1);
  reader.renderBook();
  EXPECT_EQ(reader.saves, 1);
}

TEST_P(ReaderIncrementalTest, PendingChapterDoesNotOfferFootnotesFromThePreviousPage) {
  reader.currentPageFootnotes = {1, 2};
  reader.currentPageLinks = {3};
  setJump();
  reader.renderBook();
  EXPECT_TRUE(reader.hasPendingSectionJump());
  EXPECT_TRUE(reader.currentPageFootnotes.empty());
  EXPECT_TRUE(reader.currentPageLinks.empty());
  EXPECT_EQ(reader.storage.pageReads, 0);
  reader.advanceSectionBuild();
  EXPECT_TRUE(reader.currentPageFootnotes.empty());
}

TEST_P(ReaderIncrementalTest, PartialWatermarkDoesNotMakeOneTickRebuildTheWholeChapter) {
  setJump();
  reader.storage.cache = IncrementalStorage::Cache::Partial;
  reader.storage.cachedPages = 4000;
  reader.storage.totalPages = 8000;
  reader.renderBook();
  EXPECT_EQ(reader.storage.chunks, 0);
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.pagesBuilt, 2);
  EXPECT_EQ(reader.section->pageCount, 4000);
  EXPECT_TRUE(reader.hasPendingSectionJump());
  complete();
  EXPECT_EQ(reader.storage.chunks, 4000);
  EXPECT_EQ(reader.storage.largestChunk, 2);
  EXPECT_EQ(reader.section->currentPage, expectedPage());
  EXPECT_EQ(reader.updates, 1);
}

TEST_P(ReaderIncrementalTest, CompletedCacheResolvesWithoutStartingAParser) {
  setJump();
  reader.storage.cache = IncrementalStorage::Cache::Complete;
  reader.renderBook();
  EXPECT_EQ(reader.storage.buildsStarted, 0);
  EXPECT_EQ(reader.storage.chunks, 0);
  EXPECT_EQ(reader.renderedPage, expectedPage());
  EXPECT_EQ(reader.savedPage, expectedPage());
  EXPECT_FALSE(reader.hasPendingSectionJump());
}

TEST_P(ReaderIncrementalTest, EmptyChapterClearsPendingJumpAndShowsEmptyWithoutSaving) {
  setJump();
  reader.storage.totalPages = 0;
  reader.renderBook();
  reader.advanceSectionBuild();
  EXPECT_FALSE(reader.hasPendingSectionJump());
  EXPECT_EQ(reader.updates, 1);
  EXPECT_FALSE(querySchedulingHint(reader));
  reader.renderBook();
  EXPECT_EQ(reader.renderer.emptyChapters, 1);
  EXPECT_EQ(reader.storage.pageReads, 0);
  EXPECT_EQ(reader.saves, 0);
  for (int i = 0; i < 10; ++i) reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, 1);
  EXPECT_EQ(reader.updates, 1);
}

TEST_P(ReaderIncrementalTest, HeapGatePausesWithoutSpinningAndResumesAtThreshold) {
  setJump();
  reader.renderBook();
  ESP.freeHeap = EpubReaderActivity::BACKGROUND_BUILD_MIN_FREE_HEAP - 1;
  for (int i = 0; i < 10; ++i) reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, 0);
  EXPECT_FALSE(querySchedulingHint(reader));
  ESP.freeHeap++;
  ESP.largest = EpubReaderActivity::BACKGROUND_BUILD_MIN_MAX_ALLOC - 1;
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, 0);
  EXPECT_FALSE(querySchedulingHint(reader));
  ESP.largest++;
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, 1);
  EXPECT_TRUE(querySchedulingHint(reader));
}

TEST_P(ReaderIncrementalTest, BusyRendererPreventsBackgroundWork) {
  setJump();
  reader.renderBook();
  RenderLock::busy = true;
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, 0);
  EXPECT_FALSE(querySchedulingHint(reader));
  RenderLock::busy = false;
  RenderLock::failTryAcquire = true;
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, 0);
  RenderLock::failTryAcquire = false;
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, 1);
}

TEST_P(ReaderIncrementalTest, BackgroundFailureIsLatchedUntilExplicitNavigation) {
  setJump();
  reader.renderBook();
  reader.storage.chunkFails = true;
  reader.advanceSectionBuild();
  EXPECT_TRUE(reader.pendingBuildError);
  EXPECT_FALSE(reader.section);
  EXPECT_FALSE(querySchedulingHint(reader));
  EXPECT_EQ(reader.updates, 1);
  for (int i = 0; i < 5; ++i) {
    reader.advanceSectionBuild();
    reader.renderBook();
  }
  EXPECT_EQ(reader.storage.buildsStarted, 1);
  EXPECT_EQ(reader.storage.chunks, 1);
  EXPECT_EQ(reader.saves, 0);
  reader.clearPendingNavigation();
  reader.storage.chunkFails = false;
  setJump();
  reader.renderBook();
  complete();
  reader.renderBook();
  EXPECT_EQ(reader.savedPage, expectedPage());
}

TEST_P(ReaderIncrementalTest, StartFailureDoesNotEnterRepeatedBuildOrSave) {
  setJump();
  reader.storage.startFails = true;
  reader.renderBook();
  EXPECT_TRUE(reader.pendingBuildError);
  for (int i = 0; i < 5; ++i) reader.renderBook();
  EXPECT_EQ(reader.storage.buildsStarted, 1);
  EXPECT_EQ(reader.storage.chunks, 0);
  EXPECT_EQ(reader.saves, 0);
}

TEST_P(ReaderIncrementalTest, ExplicitJumpIgnoresOldCachedOffsetAndRatio) {
  setJump();
  reader.cachedVisibleTextOffset = 9000;
  reader.cachedChapterTotalPageCount = 50;
  reader.nextPageNumber = 20;
  reader.renderBook();
  EXPECT_EQ(reader.storage.chunks, 0);
  complete();
  EXPECT_EQ(reader.section->currentPage, expectedPage());
  EXPECT_FALSE(reader.cachedVisibleTextOffset);
  EXPECT_EQ(reader.cachedChapterTotalPageCount, 0);
  reader.renderBook();
  EXPECT_EQ(reader.savedPage, expectedPage());
}

TEST_P(ReaderIncrementalTest, NewChapterNavigationCancelsStaleJump) {
  setJump();
  reader.renderBook();
  reader.advanceSectionBuild();
  EXPECT_TRUE(reader.skipPages(1));
  EXPECT_FALSE(reader.hasPendingSectionJump());
  EXPECT_EQ(reader.currentSpineIndex, 2);
  reader.storage.cache = IncrementalStorage::Cache::Complete;
  reader.renderBook();
  EXPECT_EQ(reader.renderedPage, 0);
  EXPECT_EQ(reader.savedPage, 0);
  EXPECT_EQ(reader.storage.chunks, 1);
}

INSTANTIATE_TEST_SUITE_P(PercentAndLastPage, ReaderIncrementalTest, testing::Bool());

TEST(ReaderIncremental, OrdinaryFirstPageStillBuildsOnlyItsExistingForegroundWindow) {
  EpubReaderActivity reader;
  reader.renderBook();
  EXPECT_EQ(reader.storage.chunks, 1);
  EXPECT_EQ(reader.storage.pagesBuilt, EpubReaderActivity::BUILD_PAGES_PER_CHUNK);
  EXPECT_EQ(reader.renderedPage, 0);
  EXPECT_EQ(reader.savedPage, 0);
  EXPECT_FALSE(querySchedulingHint(reader));
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, 1);
}

TEST(ReaderIncremental, SectionAllocationFailureHasAStableErrorWithoutProgress) {
  EpubReaderActivity reader;
  reader.pendingPercentJump = true;
  reader.storage.allocationFails = true;
  for (int i = 0; i < 5; ++i) reader.renderBook();
  EXPECT_TRUE(reader.pendingBuildError);
  EXPECT_EQ(reader.storage.buildsStarted, 0);
  EXPECT_EQ(reader.saves, 0);
}

TEST(ReaderIncremental, PendingPartialFarFromWatermarkStillStartsUnderHeapGate) {
  EpubReaderActivity reader;
  reader.storage.cache = IncrementalStorage::Cache::Partial;
  reader.storage.cachedPages = 80;
  reader.renderBook();
  EXPECT_EQ(reader.storage.buildsStarted, 0);
  reader.pendingLastPageJump = true;
  ESP.freeHeap = 0;
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.buildsStarted, 0);
  ESP = {};
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.buildsStarted, 1);
  EXPECT_EQ(reader.storage.pagesBuilt, 2);
}

TEST(ReaderIncremental, DeferredStartFailurePreservesTheOldCacheButStopsThePendingJump) {
  EpubReaderActivity reader;
  reader.storage.cache = IncrementalStorage::Cache::Partial;
  reader.storage.cachedPages = 80;
  reader.renderBook();
  reader.pendingLastPageJump = true;
  reader.storage.startFails = true;
  for (int i = 0; i < 5; ++i) reader.advanceSectionBuild();
  ASSERT_TRUE(reader.section);
  EXPECT_TRUE(reader.pendingBuildError);
  EXPECT_EQ(reader.storage.buildsStarted, 1);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_FALSE(querySchedulingHint(reader));
}

TEST(ReaderIncremental, InvalidBoundaryKeepsCommittedOffsetWhileAcceptedTurnClearsIt) {
  EpubReaderActivity reader;
  reader.storage.cache = IncrementalStorage::Cache::Complete;
  reader.currentSpineIndex = 0;
  reader.renderBook();
  ASSERT_TRUE(reader.currentPageVisibleOffset);
  EXPECT_FALSE(reader.pageTurn(false));
  EXPECT_TRUE(reader.currentPageVisibleOffset);
  EXPECT_TRUE(reader.pageTurn(true));
  EXPECT_FALSE(reader.currentPageVisibleOffset);
}

TEST(ReaderIncremental, EmptyReflowCannotReuseThePreviouslyDisplayedPageOffset) {
  EpubReaderActivity reader;
  reader.storage.cache = IncrementalStorage::Cache::Complete;
  reader.renderBook();
  ASSERT_TRUE(reader.currentPageVisibleOffset);
  reader.section.reset();
  reader.storage.totalPages = 0;
  reader.renderBook();
  EXPECT_EQ(reader.renderer.emptyChapters, 1);
  EXPECT_FALSE(reader.currentPageVisibleOffset);
  EXPECT_EQ(reader.saves, 1);
}

TEST(ReaderIncremental, OnlyTtfFontsReleaseResidentCachesBeforeFreshChapterBuild) {
  for (const bool ttf : {false, true}) {
    EpubReaderActivity reader;
    if (ttf) reader.renderer.ttfFonts.push_back(1);
    reader.renderBook();
    ASSERT_EQ(reader.storage.buildsStarted, 1);
    EXPECT_EQ(reader.storage.fontReleasesAtStart, ttf ? 1 : 0);
    EXPECT_EQ(reader.renderer.fonts.releases, ttf ? 1 : 0);
    reader.renderBook();
    EXPECT_EQ(reader.renderer.fonts.releases, ttf ? 1 : 0);
  }
}

TEST(ReaderIncremental, CachedTtfChapterDoesNotReleaseResidentCaches) {
  EpubReaderActivity reader;
  reader.renderer.ttfFonts.push_back(1);
  reader.storage.cache = IncrementalStorage::Cache::Complete;
  reader.renderBook();
  EXPECT_EQ(reader.storage.buildsStarted, 0);
  EXPECT_EQ(reader.renderer.fonts.releases, 0);
  EXPECT_EQ(reader.renderedPage, 0);
}

TEST_P(ReaderIncrementalTest, BusyBuildAndSchedulingReadNoSectionStateBeforeAcquisition) {
  setJump();
  reader.renderBook();
  for (bool busy : {false, true}) {
    RenderLock::busy = busy;
    RenderLock::failTryAcquire = !busy;
    reader.storage.stateReads = 0;
    RenderLock::tryAttempts = RenderLock::peekCalls = 0;
    reader.advanceSectionBuild();
    EXPECT_FALSE(querySchedulingHint(reader));
    EXPECT_EQ(reader.storage.stateReads, 0);
    EXPECT_EQ(reader.storage.chunks, 0);
    EXPECT_EQ(RenderLock::tryAttempts, 2u);
    EXPECT_EQ(RenderLock::peekCalls, 0u);
  }
}

TEST_P(ReaderIncrementalTest, SharedBuildPredicateRespectsWindowAndPendingNavigation) {
  reader.renderBook();
  ASSERT_TRUE(reader.section);
  reader.section->pageCount = EpubReaderActivity::BUILD_WINDOW_AHEAD;
  reader.section->currentPage = 0;
  reader.section->partial = false;
  reader.section->building = true;
  const auto chunks = reader.storage.chunks;
  EXPECT_FALSE(querySchedulingHint(reader));
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, chunks);
  setJump();
  EXPECT_TRUE(querySchedulingHint(reader));
  reader.advanceSectionBuild();
  EXPECT_EQ(reader.storage.chunks, chunks + 1);
}

namespace {
void prepareIdleReader(EpubReaderActivity& reader) {
  reader.storage.cache = IncrementalStorage::Cache::Complete;
  reader.renderBook();
  ASSERT_TRUE(reader.section);
  ASSERT_FALSE(reader.section->isBuilding());
  reader.lastRenderCompleteMs = 100;
  reader.storage.pageReads = 0;
}
}  // namespace

TEST(ReaderIdlePrefetch, RetainsNextPageWithoutDiscardedFontWork) {
  EpubReaderActivity reader;
  prepareIdleReader(reader);
  const int chunks = reader.storage.chunks;
  ESP.freeReads = ESP.largestReads = 0;
  for (int tick = 0; tick < 100; ++tick) reader.runIdlePrewarm();
  EXPECT_EQ(ESP.freeReads, 102);
  EXPECT_EQ(ESP.largestReads, 102);
  std::printf("IDLE_HEAP ticks=100 free_reads=%d largest_reads=%d\n", ESP.freeReads, ESP.largestReads);
  EXPECT_EQ(reader.renderer.fonts.scans, 0);
  EXPECT_EQ(reader.renderer.fonts.prewarms, 0);
  EXPECT_EQ(reader.renderer.scannedPages, 0);
  EXPECT_EQ(reader.storage.pageReads, 1);
  EXPECT_EQ(reader.storage.retainedPages, 1);
  EXPECT_EQ(reader.storage.chunks, chunks);
  EXPECT_EQ(reader.renderer.displays, 0);
  std::printf("IDLE_PREFETCH ticks=100 scans=%d page_reads=%d retained=%d chunk_delta=%d\n",
              reader.renderer.fonts.scans, reader.storage.pageReads, reader.storage.retainedPages,
              reader.storage.chunks - chunks);
  ++reader.section->currentPage;
  reader.runIdlePrewarm();
  EXPECT_EQ(reader.storage.pageReads, 2);
  EXPECT_EQ(reader.storage.retainedPages, 2);
  EXPECT_EQ(reader.renderer.fonts.scans, 0);
}

TEST(ReaderIdlePrefetch, LockUiAndHeapGatesAvoidIdleReads) {
  for (int gate = 0; gate < 8; ++gate) {
    SCOPED_TRACE(gate);
    EpubReaderActivity reader;
    prepareIdleReader(reader);
    switch (gate) {
      case 0:
        RenderLock::busy = true;
        break;
      case 1:
        RenderLock::failTryAcquire = true;
        break;
      case 2:
        reader.overlay = EpubReaderActivity::Overlay::Test;
        break;
      case 3:
        reader.renderer.frameAvailable = false;
        break;
      case 4:
        reader.lastRenderCompleteMs = 0;
        break;
      case 5:
        reader.lastRenderCompleteMs = millis() - 400;
        break;
      case 6:
        ESP.freeHeap = PagePrefetchPolicy::MIN_FREE_HEAP - 1;
        break;
      case 7:
        ESP.largest = PagePrefetchPolicy::MIN_MAX_ALLOC - 1;
        break;
    }
    reader.runIdlePrewarm();
    EXPECT_EQ(reader.renderer.fonts.scans, 0);
    EXPECT_EQ(reader.storage.pageReads, 0);
    EXPECT_EQ(reader.idlePrewarmSpine, -1);
  }
  EpubReaderActivity reader;
  prepareIdleReader(reader);
  ESP.freeHeap = PagePrefetchPolicy::MIN_FREE_HEAP;
  ESP.largest = PagePrefetchPolicy::MIN_MAX_ALLOC;
  reader.lastRenderCompleteMs = millis() - 401;
  reader.runIdlePrewarm();
  EXPECT_EQ(reader.storage.pageReads, 1);
  EXPECT_EQ(reader.renderer.fonts.scans, 0);
}

TEST(ReaderIdlePrefetch, ActiveParserWaitsForCompletion) {
  EpubReaderActivity reader;
  reader.renderBook();
  int ticks = 0;
  while (reader.backgroundBuildWanted() && ticks++ < 10) reader.advanceSectionBuild();
  ASSERT_LT(ticks, 10);
  ASSERT_TRUE(reader.section->isBuilding());
  ASSERT_FALSE(reader.backgroundBuildWanted());
  reader.lastRenderCompleteMs = 100;
  reader.storage.pageReads = 0;
  reader.runIdlePrewarm();
  EXPECT_EQ(reader.storage.pageReads, 0);
  EXPECT_EQ(reader.renderer.fonts.scans, 0);
  reader.buildHeapPaused = true;
  ++reader.section->currentPage;
  reader.runIdlePrewarm();
  EXPECT_EQ(reader.storage.pageReads, 0);
}

TEST(ReaderIdlePrefetch, EndOfSectionSkipsRead) {
  EpubReaderActivity reader;
  prepareIdleReader(reader);
  reader.section->currentPage = reader.section->pageCount - 1;
  reader.runIdlePrewarm();
  EXPECT_EQ(reader.storage.pageReads, 0);
  EXPECT_EQ(reader.storage.retainedPages, 0);
  EXPECT_EQ(reader.renderer.fonts.scans, 0);
}

TEST(ReaderIdlePrefetch, MemoryPressureSkipsDecodeUntilRetentionIsPossible) {
  EpubReaderActivity reader;
  prepareIdleReader(reader);
  for (int tick = 0; tick < 100; ++tick) {
    ESP.freeHeap = tick % 2 ? 96 * 1024 : 64 * 1024;
    ESP.largest = tick % 2 ? 24 * 1024 : 48 * 1024;
    reader.runIdlePrewarm();
  }
  EXPECT_EQ(reader.storage.pageReads, 0);
  EXPECT_EQ(reader.storage.retainedPages, 0);
  EXPECT_EQ(reader.storage.retainAttempts, 0);
  EXPECT_EQ(reader.idlePrewarmSpine, -1);
  std::printf("IDLE_MEMORY ticks=100 page_reads=%d retained=%d\n", reader.storage.pageReads,
              reader.storage.retainedPages);
  ESP.freeHeap = 128 * 1024;
  ESP.largest = 64 * 1024;
  reader.runIdlePrewarm();
  EXPECT_EQ(reader.storage.pageReads, 1);
  EXPECT_EQ(reader.storage.retainedPages, 1);
  EXPECT_EQ(reader.renderer.displays, 0);
}

TEST(ReaderIdlePrefetch, RechecksMemoryAfterPageDecode) {
  EpubReaderActivity reader;
  prepareIdleReader(reader);
  ESP.freeHeap = PagePrefetchPolicy::MIN_FREE_HEAP + 1024;
  ESP.largest = PagePrefetchPolicy::MIN_MAX_ALLOC;
  reader.storage.idleReadHeapCost = 2048;
  reader.runIdlePrewarm();
  EXPECT_EQ(reader.storage.pageReads, 1);
  EXPECT_EQ(reader.storage.retainAttempts, 1);
  EXPECT_EQ(reader.storage.retainedPages, 0);
  reader.runIdlePrewarm();
  EXPECT_EQ(reader.storage.pageReads, 1);
}
