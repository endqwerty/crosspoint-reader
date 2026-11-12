#include <gtest/gtest.h>

#include "SleepSyncHandoffFixture.h"

class SleepSyncHandoffTest : public testing::Test {
 protected:
  void SetUp() override {
    syncEngine::noted.reset();
    KOREADER_STORE = {};
  }
  EpubReaderActivity reader;
};

TEST_F(SleepSyncHandoffTest, CommittedPageIsHandedOverWithItsTextOffset) {
  reader.currentSpineIndex = 3;
  reader.displayedPosition = ReaderProgressState::Position{3, 7, 20, 1800};
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->spineIndex, 3);
  EXPECT_EQ(syncEngine::noted->pageNumber, 7);
  EXPECT_EQ(syncEngine::noted->totalPages, 20);
  EXPECT_TRUE(syncEngine::noted->hasVisibleTextOffset);
  EXPECT_EQ(syncEngine::noted->visibleTextOffset, 1800u);
  EXPECT_TRUE(syncEngine::noted->hasResolvedSpineIndex);
  EXPECT_TRUE(syncEngine::noted->hasMappedPage);
}

TEST_F(SleepSyncHandoffTest, NavigationAheadOfTheDisplayHandsOverTheDisplayedPage) {
  // A failed refresh, a chapter turn or a dropped section leave the committed page on the glass.
  reader.currentSpineIndex = 4;
  reader.displayedPosition = ReaderProgressState::Position{3, 7, 20, 1800};
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->spineIndex, 3);
  EXPECT_EQ(syncEngine::noted->pageNumber, 7);
}

TEST_F(SleepSyncHandoffTest, PageWithoutTextOffsetIsHandedOverByPage) {
  reader.displayedPosition = ReaderProgressState::Position{2, 0, 5, std::nullopt};
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_FALSE(syncEngine::noted->hasVisibleTextOffset);
  EXPECT_EQ(syncEngine::noted->pageNumber, 0);
}

TEST_F(SleepSyncHandoffTest, NothingIsHandedOverBeforeTheFirstPaint) {
  reader.currentSpineIndex = 2;
  reader.prepareForSleep();
  EXPECT_FALSE(syncEngine::noted.has_value());
}

TEST_F(SleepSyncHandoffTest, FinishedScreenHandsOverTheEndOfTheLastChapter) {
  reader.currentSpineIndex = reader.epub->getSpineItemsCount();
  reader.displayedPosition = ReaderProgressState::Position{9, 3, 4, 500};
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->spineIndex, reader.epub->getSpineItemsCount() - 1);
  EXPECT_EQ(syncEngine::noted->pageNumber, 1);
  EXPECT_EQ(syncEngine::noted->totalPages, 2);
}

TEST_F(SleepSyncHandoffTest, DisabledSyncOnSleepHandsNothingOver) {
  KOREADER_STORE.syncOnSleep = false;
  reader.displayedPosition = ReaderProgressState::Position{3, 7, 20, 1800};
  reader.prepareForSleep();
  EXPECT_FALSE(syncEngine::noted.has_value());
}

TEST_F(SleepSyncHandoffTest, LoadedChapterRefreshesTheCountAndAddsTheParagraphFallback) {
  reader.currentSpineIndex = 3;
  reader.displayedPosition = ReaderProgressState::Position{3, 4, 10, 1800};
  reader.section = std::make_unique<SectionStub>(SectionStub{20, 20, 12});
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->pageNumber, 4);
  EXPECT_EQ(syncEngine::noted->totalPages, 20);
  EXPECT_TRUE(syncEngine::noted->hasParagraphIndex);
  EXPECT_EQ(syncEngine::noted->paragraphIndex, 12);
}

TEST_F(SleepSyncHandoffTest, AnotherLoadedChapterLeavesTheSnapshotAlone) {
  reader.currentSpineIndex = 4;
  reader.displayedPosition = ReaderProgressState::Position{3, 4, 10, 1800};
  reader.section = std::make_unique<SectionStub>(SectionStub{20, 20, 12});
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->totalPages, 10);
  EXPECT_FALSE(syncEngine::noted->hasParagraphIndex);
}

TEST_F(SleepSyncHandoffTest, LeavingTheFinishedScreenStillHandsOverTheEnd) {
  // Back from a committed finished screen, before the last page paints, the glass still shows the end.
  reader.currentSpineIndex = reader.epub->getSpineItemsCount() - 1;
  reader.finishedScreenShown = true;
  reader.displayedPosition = ReaderProgressState::Position{5, 2, 30, 900};
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->spineIndex, reader.epub->getSpineItemsCount() - 1);
  EXPECT_EQ(syncEngine::noted->pageNumber, 1);
  EXPECT_EQ(syncEngine::noted->totalPages, 2);
}

TEST_F(SleepSyncHandoffTest, ChapterStillBuildingKeepsThePaintedCount) {
  reader.currentSpineIndex = 3;
  reader.displayedPosition = ReaderProgressState::Position{3, 20, 100, 1800};
  reader.section = std::make_unique<SectionStub>(SectionStub{85, 40, 12, true, false});
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->totalPages, 100);
}

TEST_F(SleepSyncHandoffTest, PartialChapterKeepsThePaintedCount) {
  reader.currentSpineIndex = 3;
  reader.displayedPosition = ReaderProgressState::Position{3, 20, 100, 1800};
  reader.section = std::make_unique<SectionStub>(SectionStub{85, 40, 12, false, true});
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->totalPages, 100);
}

TEST_F(SleepSyncHandoffTest, ChapterStillBuildingTakesARisingEstimate) {
  reader.currentSpineIndex = 3;
  reader.displayedPosition = ReaderProgressState::Position{3, 20, 40, 1800};
  reader.section = std::make_unique<SectionStub>(SectionStub{60, 40, 12, true, false});
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->totalPages, 60);
}

TEST_F(SleepSyncHandoffTest, FinishedChapterTakesItsExactCountEvenWhenSmaller) {
  reader.currentSpineIndex = 3;
  reader.displayedPosition = ReaderProgressState::Position{3, 20, 100, 1800};
  reader.section = std::make_unique<SectionStub>(SectionStub{85, 85, 12, false, false});
  reader.prepareForSleep();
  ASSERT_TRUE(syncEngine::noted.has_value());
  EXPECT_EQ(syncEngine::noted->totalPages, 85);
}
