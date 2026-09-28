#include "ReaderSearchFixture.h"

class ReaderSearchHandoffTest : public testing::Test {
 protected:
  EpubReaderActivity reader;
  void SetUp() override {
    failSearchAllocation = false;
    ImageBlock::releases = 0;
    EpubReaderActivity::Section::releases = 0;
    reader.navigationHistory.push(1, 4, ReaderNavigationHistory::Jump::Footnote);
  }
};

TEST_F(ReaderSearchHandoffTest, MenuReleasesReaderCachesBeforeStartingSearch) {
  const auto* book = reader.epub.get();
  reader.activateFind();
  ASSERT_TRUE(reader.search);
  EXPECT_FALSE(reader.section);
  EXPECT_EQ(EpubReaderActivity::Section::releases, 1u);
  EXPECT_EQ(reader.renderer.fonts.releases, 1u);
  EXPECT_EQ(reader.nextPageNumber, 7);
  EXPECT_EQ(reader.cachedSpineIndex, 3);
  EXPECT_EQ(reader.cachedChapterTotalPageCount, 42);
  EXPECT_EQ(reader.cachedVisibleTextOffset, 1234);
  EXPECT_EQ(reader.epub.get(), book);
  EXPECT_NE(reader.navigationHistory.footnoteOrigin(), nullptr);
}

TEST_F(ReaderSearchHandoffTest, ActivityAllocationFailureLeavesOriginalReaderIntact) {
  const auto* section = reader.section.get();
  failSearchAllocation = true;
  reader.activateFind();
  EXPECT_EQ(reader.section.get(), section);
  EXPECT_EQ(reader.section->currentPage, 7);
  EXPECT_EQ(reader.overlays, 0u);
  EXPECT_EQ(reader.renderer.fonts.releases, 0u);
  EXPECT_EQ(ImageBlock::releases, 0u);
  EXPECT_FALSE(reader.search);
  EXPECT_EQ(reader.menus, 1u);
}

TEST_F(ReaderSearchHandoffTest, CancellationRetainsExactResumePageAndFootnoteOrigin) {
  reader.activateFind();
  ActivityResult result;
  result.isCancelled = true;
  result.data = ProgressChangeResult{10, 999, true};
  reader.returnFromSearch(result);
  EXPECT_EQ(reader.currentSpineIndex, 3);
  EXPECT_EQ(reader.nextPageNumber, 7);
  EXPECT_EQ(reader.cachedVisibleTextOffset, 1234);
  EXPECT_FALSE(reader.pendingOffsetJump);
  EXPECT_NE(reader.navigationHistory.footnoteOrigin(), nullptr);
  EXPECT_EQ(reader.updates, 1u);
}

TEST_F(ReaderSearchHandoffTest, ResultUsesExistingOffsetNavigationAndClearsTemporaryNoteOrigin) {
  reader.activateFind();
  reader.pendingAnchor = "stale-note";
  reader.pendingPercentJump = true;
  reader.pendingLastPageJump = true;
  reader.returnFromSearch(ActivityResult{false, ProgressChangeResult{10, 999, true}});
  EXPECT_EQ(reader.currentSpineIndex, 10);
  EXPECT_EQ(reader.pendingOffsetJump, 999);
  EXPECT_EQ(reader.nextPageNumber, 0);
  EXPECT_EQ(reader.cachedChapterTotalPageCount, 0);
  EXPECT_FALSE(reader.cachedVisibleTextOffset);
  EXPECT_TRUE(reader.navigationHistory.empty());
  EXPECT_TRUE(reader.pendingAnchor.empty());
  EXPECT_FALSE(reader.pendingPercentJump);
  EXPECT_FALSE(reader.pendingLastPageJump);
}

TEST_F(ReaderSearchHandoffTest, InvalidResultRetainsResumePage) {
  reader.activateFind();
  reader.returnFromSearch(ActivityResult{false, ProgressChangeResult{20, 999, true}});
  EXPECT_EQ(reader.currentSpineIndex, 3);
  EXPECT_EQ(reader.nextPageNumber, 7);
  EXPECT_FALSE(reader.pendingOffsetJump);
}

TEST_F(ReaderSearchHandoffTest, MissingSectionAndFontCachePreserveExistingFallbackPosition) {
  {
    RenderLock lock;
    reader.section.reset();
  }
  reader.nextPageNumber = 12;
  reader.cachedSpineIndex = 3;
  reader.cachedChapterTotalPageCount = 50;
  reader.renderer.hasFonts = false;
  reader.activateFind();
  EXPECT_EQ(reader.nextPageNumber, 12);
  EXPECT_EQ(reader.cachedChapterTotalPageCount, 50);
  EXPECT_EQ(reader.renderer.fonts.releases, 0u);
}
