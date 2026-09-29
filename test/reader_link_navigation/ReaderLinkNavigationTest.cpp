#include "ReaderLinkNavigationFixture.h"

using Jump = ReaderNavigationHistory::Jump;

class ReaderLinkNavigationTest : public testing::Test {
 protected:
  EpubReaderActivity reader;
  void SetUp() override {
    failPickerAllocation = false;
    Storage = {};
    reader.showPage(2, 4);
  }
};

TEST_F(ReaderLinkNavigationTest, LinkThenOrdinaryReadingKeepsViewedProgressAndSavesBackStack) {
  reader.navigateToHref("chapter.xhtml#begin", Jump::Link);
  EXPECT_EQ(reader.currentSpineIndex, 10);
  EXPECT_EQ(reader.pendingAnchor, "begin");
  EXPECT_EQ(reader.updates, 1);
  reader.showPage(12, 7);
  reader.saveProgress(12, 7, 20);
  reader.saveLinkStack();
  EXPECT_EQ(reader.saves, 1);
  EXPECT_EQ(reader.savedSpine, 12);
  EXPECT_EQ(reader.savedPage, 7);
  EXPECT_EQ(Storage.writtenPath, "/cache/links.bin");
  EXPECT_EQ(Storage.written, (std::vector<uint8_t>{1, 2, 0, 4, 0}));
}

TEST_F(ReaderLinkNavigationTest, EmptyHistoryDoesNotRestoreAPosition) {
  reader.restoreSavedPosition();
  EXPECT_EQ(reader.updates, 0);
  EXPECT_EQ(reader.saves, 0);
}

TEST_F(ReaderLinkNavigationTest, NestedFootnotesKeepViewedProgressAndSaveEveryReturnPoint) {
  reader.currentPageVisibleOffset = 1234;
  reader.navigateToHref("notes.xhtml#one", Jump::Footnote);
  reader.showPage(10, 5);
  reader.navigateToHref("notes.xhtml#two", Jump::Footnote);
  reader.showPage(10, 8);
  reader.saveProgress(10, 8, 20);
  reader.saveLinkStack();
  EXPECT_EQ(reader.saves, 1);
  EXPECT_EQ(reader.savedSpine, 10);
  EXPECT_EQ(reader.savedPage, 8);
  EXPECT_EQ(Storage.written, (std::vector<uint8_t>{2, 2, 0, 4, 0, 10, 0, 5, 0}));
}

TEST_F(ReaderLinkNavigationTest, SavedBackStackIsRestoredOnceAsLinkReturns) {
  Storage.available = true;
  Storage.data = {2, 1, 0, 0x2C, 1, 9, 0, 3, 0};
  reader.loadLinkStack();
  EXPECT_EQ(Storage.removedPath, "/cache/links.bin");
  ASSERT_EQ(reader.navigationHistory.size(), 2);
  EXPECT_FALSE(reader.navigationHistory.footnoteOrigin());
  reader.restoreSavedPosition();
  EXPECT_EQ(reader.currentSpineIndex, 9);
  EXPECT_EQ(reader.nextPageNumber, 3);
  EXPECT_EQ(reader.cachedChapterTotalPageCount, 0);
  EXPECT_FALSE(reader.cachedVisibleTextOffset);
  reader.restoreSavedPosition();
  EXPECT_EQ(reader.currentSpineIndex, 1);
  EXPECT_EQ(reader.nextPageNumber, 300);
  reader.loadLinkStack();
  EXPECT_TRUE(reader.navigationHistory.empty());
}

TEST_F(ReaderLinkNavigationTest, InvalidSavedBackStackIsDiscarded) {
  const std::vector<std::vector<uint8_t>> payloads = {
      {}, {0}, {1, 2, 0, 4}, {4, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}, {1, 10, 0, 4, 0}, {2, 1, 0, 4, 0}};
  for (const auto& payload : payloads) {
    Storage = {};
    Storage.available = true;
    Storage.data = payload;
    reader.loadLinkStack();
    EXPECT_TRUE(reader.navigationHistory.empty());
    EXPECT_EQ(Storage.removedPath, "/cache/links.bin");
  }
}

TEST_F(ReaderLinkNavigationTest, UnwritableBackStackLeavesHistoryAndProgressAlone) {
  reader.navigateToHref("chapter.xhtml", Jump::Link);
  Storage.writable = false;
  reader.saveLinkStack();
  EXPECT_TRUE(Storage.written.empty());
  EXPECT_EQ(reader.navigationHistory.size(), 1);
  EXPECT_EQ(reader.saves, 0);
}

TEST_F(ReaderLinkNavigationTest, ExplicitPercentJumpRetiresTheTemporaryFootnoteOrigin) {
  reader.navigateToHref("notes.xhtml#one", Jump::Footnote);
  reader.jumpToPercent(85);
  EXPECT_EQ(reader.currentSpineIndex, 8);
  reader.saveProgress(8, 3, 10);
  EXPECT_EQ(reader.savedSpine, 8);
  EXPECT_EQ(reader.savedPage, 3);
  EXPECT_TRUE(reader.navigationHistory.empty());
  EXPECT_TRUE(reader.pendingAnchor.empty());
}

TEST_F(ReaderLinkNavigationTest, ReturningFromFootnoteRestoresContentOffsetAndPageCount) {
  reader.currentPageVisibleOffset = 1234;
  reader.navigateToHref("notes.xhtml#one", Jump::Footnote);
  reader.restoreSavedPosition();
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.nextPageNumber, 4);
  EXPECT_EQ(reader.cachedSpineIndex, 2);
  EXPECT_EQ(reader.cachedChapterTotalPageCount, 20);
  EXPECT_EQ(reader.cachedVisibleTextOffset, 1234);
  EXPECT_TRUE(reader.pendingAnchor.empty());
}

TEST_F(ReaderLinkNavigationTest, SavedSpineZeroResumesEvenWhenBookHasDifferentTextReference) {
  Storage.available = true;
  for (size_t bytes : {4u, 6u, 10u}) {
    Storage.data = {0, 0, 7, 0, 20, 0, 0xD2, 4, 0, 0};
    Storage.data.resize(bytes);
    reader.currentSpineIndex = 0;
    reader.cachedVisibleTextOffset.reset();
    reader.loadSavedProgress();
    EXPECT_EQ(reader.currentSpineIndex, 0) << bytes;
    EXPECT_EQ(reader.nextPageNumber, 7) << bytes;
    if (bytes == 10) EXPECT_EQ(reader.cachedVisibleTextOffset, 1234);
  }
}

TEST_F(ReaderLinkNavigationTest, FreshBookStartsAtTextReference) {
  reader.currentSpineIndex = 0;
  reader.loadSavedProgress();
  EXPECT_EQ(reader.currentSpineIndex, 3);
  EXPECT_EQ(reader.nextPageNumber, 0);
}

TEST_F(ReaderLinkNavigationTest, InvalidProgressPayloadUsesFirstLaunchDestination) {
  Storage.available = true;
  for (size_t bytes : {0u, 1u, 2u, 3u, 5u, 7u, 8u, 9u}) {
    Storage.data.assign(bytes, 0);
    reader.currentSpineIndex = 0;
    reader.loadSavedProgress();
    EXPECT_EQ(reader.currentSpineIndex, 3) << bytes;
  }
}

TEST_F(ReaderLinkNavigationTest, OldLastPageSentinelKeepsSavedSpineZeroWithoutReusingSentinel) {
  Storage.available = true;
  Storage.data = {0, 0, 0xFF, 0xFF, 0, 0};
  reader.currentSpineIndex = 0;
  reader.loadSavedProgress();
  EXPECT_EQ(reader.currentSpineIndex, 0);
  EXPECT_EQ(reader.nextPageNumber, 0);
}

TEST_F(ReaderLinkNavigationTest, PercentBoundarySelectsTheNextSpineStart) {
  reader.jumpToPercent(50);
  EXPECT_EQ(reader.currentSpineIndex, 5);
  EXPECT_FLOAT_EQ(reader.pendingSpineProgress, 0.0f);
}

TEST_F(ReaderLinkNavigationTest, PercentEndpointsClampAndStayInsideTheBook) {
  reader.jumpToPercent(-10);
  EXPECT_EQ(reader.currentSpineIndex, 0);
  EXPECT_FLOAT_EQ(reader.pendingSpineProgress, 0.0f);
  reader.jumpToPercent(110);
  EXPECT_EQ(reader.currentSpineIndex, 9);
  EXPECT_GT(reader.pendingSpineProgress, 0.99f);
  EXPECT_LT(reader.pendingSpineProgress, 1.0f);
}

TEST_F(ReaderLinkNavigationTest, UnusablePercentDestinationKeepsExistingFootnoteOrigin) {
  reader.navigationHistory.push(1, 8, Jump::Footnote);
  reader.epub->bookSize = 0;
  reader.jumpToPercent(85);
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.navigationHistory.size(), 1);
  EXPECT_EQ(reader.updates, 0);
  reader.epub->bookSize = 10000;
  reader.epub->spineCount = 0;
  reader.jumpToPercent(85);
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.navigationHistory.size(), 1);
}

TEST_F(ReaderLinkNavigationTest, ExactBookmarkResultRetiresFootnoteOriginForCurrentAndOtherSections) {
  for (const int target : {2, 7}) {
    reader.showPage(2, 4);
    reader.navigationHistory.push(1, 8, Jump::Footnote);
    reader.pendingAnchor = "stale-note";
    reader.pendingPercentJump = true;
    ProgressChangeResult position;
    position.spineIndex = target;
    position.page = 6;
    position.hasVisibleTextOffset = true;
    position.visibleTextOffset = 1234;
    reader.returnFromProgress({false, position});
    EXPECT_EQ(reader.currentSpineIndex, target);
    if (target == 2) {
      ASSERT_TRUE(reader.section);
      EXPECT_EQ(reader.section->currentPage, 12);
    } else {
      EXPECT_FALSE(reader.section);
      EXPECT_EQ(reader.pendingOffsetJump, 1234);
    }
    EXPECT_TRUE(reader.navigationHistory.empty());
    EXPECT_TRUE(reader.pendingAnchor.empty());
    EXPECT_FALSE(reader.pendingPercentJump);
  }
}

TEST_F(ReaderLinkNavigationTest, LegacyBookmarkFallbackRetiresFootnoteOrigin) {
  reader.navigationHistory.push(1, 8, Jump::Footnote);
  ProgressChangeResult position;
  position.hasSavedProgress = true;
  reader.returnFromProgress({false, position});
  EXPECT_EQ(reader.currentSpineIndex, 7);
  EXPECT_EQ(reader.nextPageNumber, 9);
  EXPECT_TRUE(reader.navigationHistory.empty());
}

TEST_F(ReaderLinkNavigationTest, CancelledBookmarkAndChapterPickersKeepFootnoteOrigin) {
  reader.navigationHistory.push(1, 8, Jump::Footnote);
  reader.returnFromProgress({true, ProgressChangeResult{}});
  reader.returnFromChapter({true, ChapterResult{}});
  EXPECT_EQ(reader.menus, 2);
  ASSERT_NE(reader.navigationHistory.footnoteOrigin(), nullptr);
  EXPECT_EQ(reader.navigationHistory.footnoteOrigin()->spineIndex, 1);
}

TEST_F(ReaderLinkNavigationTest, ChapterSelectionReplacesStaleOffsetAndFootnoteOrigin) {
  reader.navigationHistory.push(1, 8, Jump::Footnote);
  reader.pendingOffsetJump = 999;
  reader.pendingLastPageJump = true;
  reader.returnFromChapter({false, ChapterResult{6, "chosen"}});
  EXPECT_EQ(reader.currentSpineIndex, 6);
  EXPECT_EQ(reader.pendingAnchor, "chosen");
  EXPECT_FALSE(reader.pendingOffsetJump);
  EXPECT_FALSE(reader.pendingLastPageJump);
  EXPECT_TRUE(reader.navigationHistory.empty());
}

TEST_F(ReaderLinkNavigationTest, ToolbarTocSelectionRetiresFootnoteOrigin) {
  reader.navigationHistory.push(1, 8, Jump::Footnote);
  reader.pendingOffsetJump = 999;
  reader.selectToc();
  EXPECT_EQ(reader.currentSpineIndex, 4);
  EXPECT_EQ(reader.pendingAnchor, "chapter");
  EXPECT_FALSE(reader.pendingOffsetJump);
  EXPECT_TRUE(reader.navigationHistory.empty());
  EXPECT_EQ(reader.overlay, EpubReaderActivity::Overlay::None);
}

TEST_F(ReaderLinkNavigationTest, InvalidToolbarTocKeepsFootnoteOrigin) {
  reader.navigationHistory.push(1, 8, Jump::Footnote);
  reader.epub->tocItem.spineIndex = -1;
  reader.selectToc();
  EXPECT_EQ(reader.currentSpineIndex, 2);
  ASSERT_NE(reader.navigationHistory.footnoteOrigin(), nullptr);
}

TEST_F(ReaderLinkNavigationTest, LegacyHistoryRestoresPageWithoutInventingAnOffset) {
  reader.navigationHistory.push(2, 7, Jump::Footnote);
  reader.pendingOffsetJump = 999;
  reader.pendingLastPageJump = true;
  reader.restoreSavedPosition();
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.nextPageNumber, 7);
  EXPECT_EQ(reader.cachedChapterTotalPageCount, 0);
  EXPECT_FALSE(reader.cachedVisibleTextOffset);
  EXPECT_FALSE(reader.pendingOffsetJump);
  EXPECT_FALSE(reader.pendingLastPageJump);
}

TEST_F(ReaderLinkNavigationTest, HistoryEvictionPreservesOutermostOffsetAndPageCount) {
  reader.navigationHistory.push(1, 4, Jump::Footnote, 50, 1234);
  for (int i = 0; i < 100; ++i) reader.navigationHistory.push(2, i, Jump::Link, 100, 5678);
  const auto* origin = reader.navigationHistory.footnoteOrigin();
  ASSERT_TRUE(origin);
  EXPECT_EQ(origin->spineIndex, 1);
  EXPECT_EQ(origin->pageNumber, 4);
  EXPECT_EQ(origin->pageCount, 50);
  EXPECT_EQ(origin->visibleTextOffset, 1234);
}

TEST_F(ReaderLinkNavigationTest, FootnoteFromLinkTargetStartsItsExcursionAtThatTarget) {
  reader.navigateToHref("begin.xhtml", Jump::Link);
  reader.showPage(10, 12);
  reader.navigateToHref("notes.xhtml#one", Jump::Footnote);
  const auto* origin = reader.navigationHistory.footnoteOrigin();
  ASSERT_TRUE(origin);
  EXPECT_EQ(origin->spineIndex, 10);
  EXPECT_EQ(origin->pageNumber, 12);
}

TEST_F(ReaderLinkNavigationTest, LinkFromFootnotePreservesExcursionOrigin) {
  reader.navigateToHref("notes.xhtml#one", Jump::Footnote);
  reader.showPage(10, 3);
  reader.navigateToHref("other.xhtml", Jump::Link);
  const auto* origin = reader.navigationHistory.footnoteOrigin();
  ASSERT_TRUE(origin);
  EXPECT_EQ(origin->spineIndex, 2);
  EXPECT_EQ(origin->pageNumber, 4);
}

TEST_F(ReaderLinkNavigationTest, BackUnwindsNotesAndLinksWithoutLeavingAStaleOrigin) {
  reader.navigateToHref("chapter.xhtml", Jump::Link);
  reader.showPage(10, 12);
  reader.navigateToHref("notes.xhtml#one", Jump::Footnote);
  reader.restoreSavedPosition();
  EXPECT_EQ(reader.currentSpineIndex, 10);
  EXPECT_EQ(reader.nextPageNumber, 12);
  EXPECT_FALSE(reader.navigationHistory.footnoteOrigin());
  EXPECT_EQ(reader.saves, 0);
  reader.restoreSavedPosition();
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.nextPageNumber, 4);
  EXPECT_TRUE(reader.navigationHistory.empty());
}

TEST_F(ReaderLinkNavigationTest, FailedLinkDoesNotMutatePageOrFullHistory) {
  for (int i = 0; i < ReaderNavigationHistory::CAPACITY; ++i) {
    reader.navigationHistory.push(i, i + 4, Jump::Footnote);
  }
  reader.epub->target = -1;
  reader.pendingAnchor = "original";
  reader.navigateToHref("missing.xhtml", Jump::Link);
  EXPECT_EQ(reader.navigationHistory.size(), ReaderNavigationHistory::CAPACITY);
  EXPECT_EQ(reader.pendingAnchor, "original");
  ASSERT_TRUE(reader.section);
  EXPECT_EQ(reader.section->currentPage, 4);
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.clears, 0);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_EQ(reader.navigationHistory.pop()->spineIndex, 2);
}

TEST_F(ReaderLinkNavigationTest, SameSectionFragmentDoesNotResolveAnotherSpine) {
  reader.navigateToHref("#note", Jump::Footnote);
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.pendingAnchor, "note");
  EXPECT_EQ(reader.epub->resolves, 0);
  reader.restoreSavedPosition();
  EXPECT_EQ(reader.nextPageNumber, 4);
  EXPECT_TRUE(reader.pendingAnchor.empty());
}

TEST_F(ReaderLinkNavigationTest, MissingBookAndSectionDoNotCreateBogusReturnPositions) {
  reader.epub.reset();
  reader.navigateToHref("chapter.xhtml", Jump::Link);
  EXPECT_TRUE(reader.navigationHistory.empty());
  EXPECT_EQ(reader.updates, 0);
  reader.epub = std::make_unique<EpubReaderActivity::Book>();
  {
    RenderLock lock;
    reader.section.reset();
  }
  reader.navigateToHref("chapter.xhtml", Jump::Link);
  EXPECT_TRUE(reader.navigationHistory.empty());
}

TEST_F(ReaderLinkNavigationTest, HistoryStaysBoundedAndKeepsFootnoteOpenedAfterManyLinks) {
  for (int i = 0; i < 100; ++i) reader.navigationHistory.push(i, 0, Jump::Link);
  reader.navigationHistory.push(100, 7, Jump::Footnote);
  EXPECT_EQ(reader.navigationHistory.size(), ReaderNavigationHistory::CAPACITY);
  ASSERT_NE(reader.navigationHistory.footnoteOrigin(), nullptr);
  EXPECT_EQ(reader.navigationHistory.footnoteOrigin()->spineIndex, 100);
  for (int i = 0; i < 100; ++i) reader.navigationHistory.push(200 + i, 0, Jump::Footnote);
  EXPECT_EQ(reader.navigationHistory.size(), ReaderNavigationHistory::CAPACITY);
  EXPECT_EQ(reader.navigationHistory.footnoteOrigin()->spineIndex, 100);
}

TEST_F(ReaderLinkNavigationTest, NoFootnotesOpensNothingForEitherEntryPoint) {
  reader.openFootnoteSelect(false);
  reader.openFootnoteSelect(true);
  EXPECT_FALSE(reader.picker);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_TRUE(reader.navigationHistory.empty());
}

TEST_F(ReaderLinkNavigationTest, SingleFootnoteGoesDirectlyFromEitherEntryPoint) {
  reader.currentPageFootnotes = {{"notes.xhtml#one"}};
  reader.openFootnoteSelect(false);
  EXPECT_FALSE(reader.picker);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_EQ(reader.pendingAnchor, "one");
  reader.restoreSavedPosition();
  reader.showPage(2, 4);
  reader.openFootnoteSelect(true);
  EXPECT_FALSE(reader.picker);
  EXPECT_EQ(reader.pendingAnchor, "one");
  ASSERT_NE(reader.navigationHistory.footnoteOrigin(), nullptr);
  EXPECT_EQ(reader.navigationHistory.footnoteOrigin()->pageNumber, 4);
}

TEST_F(ReaderLinkNavigationTest, MultipleFootnotesWaitForSelectionAndJumpOnce) {
  reader.currentPageFootnotes = {{"notes.xhtml#one"}, {"notes.xhtml#two"}};
  reader.openFootnoteSelect(true);
  ASSERT_TRUE(reader.picker);
  EXPECT_EQ(reader.picker->count, 2u);
  EXPECT_TRUE(reader.navigationHistory.empty());
  reader.pickerResult({false, FootnoteResult{"notes.xhtml#two"}});
  EXPECT_EQ(reader.pendingAnchor, "two");
  EXPECT_EQ(reader.navigationHistory.size(), 1);
}

TEST_F(ReaderLinkNavigationTest, CancellingPickerReturnsToMenuOnlyWhenOpenedThere) {
  reader.currentPageFootnotes = {{"one"}, {"two"}};
  reader.openFootnoteSelect(true);
  reader.pickerResult({true, FootnoteResult{}});
  EXPECT_EQ(reader.menus, 1);
  EXPECT_EQ(reader.updates, 0);
  EXPECT_TRUE(reader.navigationHistory.empty());
  reader.openFootnoteSelect(false);
  reader.pickerResult({true, FootnoteResult{}});
  EXPECT_EQ(reader.menus, 1);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_TRUE(reader.navigationHistory.empty());
}

TEST_F(ReaderLinkNavigationTest, FailedPickerAllocationLeavesPageAndHistoryIntact) {
  failPickerAllocation = true;
  reader.currentPageFootnotes = {{"one"}, {"two"}};
  reader.openFootnoteSelect(true);
  EXPECT_FALSE(reader.picker);
  ASSERT_TRUE(reader.section);
  EXPECT_EQ(reader.section->currentPage, 4);
  EXPECT_TRUE(reader.navigationHistory.empty());
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderLinkNavigationTest, OutOfRangeChapterDestinationsPreserveReadingState) {
  for (const int destination : {-2, -1, 10, 32767}) {
    SCOPED_TRACE(destination);
    reader.navigationHistory.clear();
    reader.navigationHistory.push(1, 8, Jump::Footnote);
    reader.pendingOffsetJump = 321;
    auto* original = reader.section.get();
    reader.returnFromChapter({false, ChapterResult{destination, "invalid"}});
    EXPECT_EQ(reader.currentSpineIndex, 2);
    EXPECT_EQ(reader.section.get(), original);
    EXPECT_EQ(reader.pendingOffsetJump, 321u);
    ASSERT_NE(reader.navigationHistory.footnoteOrigin(), nullptr);
    reader.epub->tocItem.spineIndex = destination;
    reader.selectToc();
    EXPECT_EQ(reader.currentSpineIndex, 2);
    EXPECT_EQ(reader.section.get(), original);
    EXPECT_EQ(reader.pendingOffsetJump, 321u);
    ASSERT_NE(reader.navigationHistory.footnoteOrigin(), nullptr);
  }
  reader.epub->spineCount = 0;
  reader.returnFromChapter({false, ChapterResult{0, "invalid"}});
  reader.epub->tocItem.spineIndex = 0;
  reader.selectToc();
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.saves, 0);
}

TEST(ChapterSelection, RejectsInvalidSpineAndPreservesSentinelCancellation) {
  for (const int destination : {-2, -1, 10, 32767}) {
    EpubReaderChapterSelectionActivity picker;
    picker.epub->tocItems = {{0, "first"}};
    picker.epub->tocItem.spineIndex = destination;
    picker.activateIndex(1);
    ASSERT_TRUE(picker.result);
    EXPECT_TRUE(picker.result->isCancelled) << destination;
    EXPECT_EQ(picker.finishes, 1);
  }
  EpubReaderChapterSelectionActivity empty;
  empty.epub->spineCount = 0;
  empty.epub->tocItem.spineIndex = 0;
  empty.activateIndex(0);
  ASSERT_TRUE(empty.result);
  EXPECT_TRUE(empty.result->isCancelled);
}

TEST(ChapterSelection, LeadingUnresolvedEntriesOpenTheBookStart) {
  // Calibre's stale NCX "Cover" entry names a page it removed from the spine.
  for (const int index : {0, 1}) {
    EpubReaderChapterSelectionActivity picker;
    picker.epub->tocItems = {{-1, "cover"}, {-1, "cover-2"}, {3, "chapter"}};
    picker.activateIndex(index);
    ASSERT_TRUE(picker.result);
    EXPECT_FALSE(picker.result->isCancelled) << index;
    const auto& chapter = std::get<ChapterResult>(picker.result->data);
    EXPECT_EQ(chapter.spineIndex, 0);
    EXPECT_TRUE(chapter.anchor.empty());
  }
  EpubReaderChapterSelectionActivity empty;
  empty.epub->spineCount = 0;
  empty.epub->tocItem.spineIndex = -1;
  empty.activateIndex(0);
  ASSERT_TRUE(empty.result);
  EXPECT_TRUE(empty.result->isCancelled);
}

TEST(ChapterSelection, ValidFirstAndLastSpineKeepAnchors) {
  for (const int destination : {0, 9}) {
    EpubReaderChapterSelectionActivity picker;
    picker.epub->tocItem = {destination, "selected-anchor"};
    picker.activateIndex(1);
    ASSERT_TRUE(picker.result);
    EXPECT_FALSE(picker.result->isCancelled);
    const auto& chapter = std::get<ChapterResult>(picker.result->data);
    EXPECT_EQ(chapter.spineIndex, destination);
    EXPECT_EQ(chapter.anchor, "selected-anchor");
  }
}

TEST_F(ReaderLinkNavigationTest, FootnoteSelectorOwnsLoadedPageAndUsesReaderMargins) {
  reader.currentPageFootnotes = {{"one"}, {"two"}};
  reader.openFootnoteSelect(false);
  ASSERT_TRUE(reader.picker);
  EXPECT_TRUE(reader.picker->page);
  EXPECT_EQ(reader.picker->left, 12);
  EXPECT_EQ(reader.picker->top, 9);
  EXPECT_EQ(reader.section->loads, 1);
}

TEST_F(ReaderLinkNavigationTest, FailedFootnotePageLoadKeepsOriginalPosition) {
  reader.currentPageFootnotes = {{"one"}, {"two"}};
  reader.section->loadSucceeds = false;
  reader.openFootnoteSelect(true);
  EXPECT_FALSE(reader.picker);
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.section->currentPage, 4);
  EXPECT_TRUE(reader.navigationHistory.empty());
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderLinkNavigationTest, DictionarySelectorSnapshotsPageUnderLockAndLaunchesAfterUnlock) {
  reader.showPage(2, 4);
  reader.openDictionaryWordSelect();
  ASSERT_TRUE(reader.picker);
  EXPECT_EQ(reader.section->loads, 1);
  EXPECT_EQ(reader.picker->left, 12);
  EXPECT_EQ(reader.picker->top, 9);
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.section->currentPage, 4);
  reader.pickerResult(ActivityResult{true, FootnoteResult{}});
  EXPECT_EQ(reader.updates, 1);
}

TEST_F(ReaderLinkNavigationTest, DictionaryPageLoadFailureDoesNotLaunchOrChangeReadingPosition) {
  reader.showPage(2, 4);
  reader.section->loadSucceeds = false;
  reader.openDictionaryWordSelect();
  EXPECT_FALSE(reader.picker);
  EXPECT_EQ(reader.currentSpineIndex, 2);
  EXPECT_EQ(reader.section->currentPage, 4);
  EXPECT_EQ(reader.section->loads, 1);
  EXPECT_EQ(reader.saves, 0);
}
