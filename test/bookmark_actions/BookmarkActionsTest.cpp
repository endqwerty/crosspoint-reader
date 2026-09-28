#include "BookmarkActionsFixture.h"

namespace {
BookmarkEntry entry(const std::string& name, int spine = 8, int page = 4) {
  BookmarkEntry bookmark;
  bookmark.name = name;
  bookmark.summary = "summary " + name;
  bookmark.xpath = "/body/" + name;
  bookmark.percentage = (spine + page / 19.0f) / 10.0f;
  bookmark.computedSpineIndex = spine;
  bookmark.computedChapterPageCount = 20;
  bookmark.computedChapterProgress = page;
  bookmark.hasVisibleTextOffset = true;
  bookmark.visibleTextOffset = page * 100;
  return bookmark;
}

void expectEntries(const std::vector<BookmarkEntry>& actual, const std::vector<BookmarkEntry>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(actual[i].name, expected[i].name) << i;
    EXPECT_EQ(actual[i].summary, expected[i].summary) << i;
    EXPECT_EQ(actual[i].xpath, expected[i].xpath) << i;
    EXPECT_EQ(actual[i].percentage, expected[i].percentage) << i;
    EXPECT_EQ(actual[i].computedSpineIndex, expected[i].computedSpineIndex) << i;
    EXPECT_EQ(actual[i].computedChapterPageCount, expected[i].computedChapterPageCount) << i;
    EXPECT_EQ(actual[i].computedChapterProgress, expected[i].computedChapterProgress) << i;
    EXPECT_EQ(actual[i].hasVisibleTextOffset, expected[i].hasVisibleTextOffset) << i;
    EXPECT_EQ(actual[i].visibleTextOffset, expected[i].visibleTextOffset) << i;
  }
}
}  // namespace

class BookmarkActionsTest : public testing::Test {
 protected:
  void SetUp() override {
    BookmarkSaveSpy::reset();
    BookmarkLoadSpy::reset();
    GUI = {};
    failKeyboardAllocation = false;
    nowMs = 12345;
    ASSERT_FALSE(RenderLock::held);
  }
  void TearDown() override {
    BookmarkSaveSpy::observe = {};
    EXPECT_FALSE(RenderLock::held);
  }
};

TEST_F(BookmarkActionsTest, FailedAddPreservesLiveBookmarksAndIndicator) {
  EpubReaderActivity reader;
  reader.cachedBookmarks = {entry("other")};
  reader.bookmarkRemoved = true;
  const auto original = reader.cachedBookmarks;
  BookmarkSaveSpy::succeeds = false;
  BookmarkSaveSpy::observe = [&] {
    expectEntries(reader.cachedBookmarks, original);
    EXPECT_FALSE(reader.currentPageBookmarked);
  };
  reader.addBookmark();
  expectEntries(reader.cachedBookmarks, original);
  EXPECT_FALSE(reader.currentPageBookmarked);
  EXPECT_TRUE(reader.bookmarkRemoved);
  EXPECT_TRUE(reader.bookmarkSaveFailed);
  EXPECT_TRUE(reader.showBookmarkMessage);
  EXPECT_EQ(reader.bookmarkMessageTime, nowMs);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_TRUE(BookmarkSaveSpy::hadPrepend);
  ASSERT_EQ(BookmarkSaveSpy::proposed.size(), 2u);
  EXPECT_EQ(BookmarkSaveSpy::proposed.front().visibleTextOffset, 400u);
  EXPECT_EQ(BookmarkSaveSpy::proposed.back().name, "other");
}

TEST_F(BookmarkActionsTest, SuccessfulAddCommitsCacheOnlyAfterPersistingCandidate) {
  EpubReaderActivity reader;
  reader.cachedBookmarks = {entry("other")};
  BookmarkSaveSpy::observe = [&] {
    ASSERT_EQ(reader.cachedBookmarks.size(), 1u);
    EXPECT_EQ(reader.cachedBookmarks.front().name, "other");
    EXPECT_FALSE(reader.currentPageBookmarked);
  };
  reader.addBookmark();
  expectEntries(reader.cachedBookmarks, BookmarkSaveSpy::proposed);
  ASSERT_EQ(reader.cachedBookmarks.size(), 2u);
  EXPECT_EQ(reader.cachedBookmarks.front().summary, "page summary");
  EXPECT_EQ(reader.cachedBookmarks.front().xpath, "/body/p[4]");
  EXPECT_EQ(reader.cachedBookmarks.front().computedSpineIndex, 1);
  EXPECT_EQ(reader.cachedBookmarks.front().computedChapterProgress, 4);
  EXPECT_EQ(reader.cachedBookmarks.front().computedChapterPageCount, 20);
  EXPECT_TRUE(reader.currentPageBookmarked);
  EXPECT_FALSE(reader.bookmarkRemoved);
  EXPECT_FALSE(reader.bookmarkSaveFailed);
  EXPECT_TRUE(reader.showBookmarkMessage);
  EXPECT_EQ(reader.section->offsetReads, 0);
  EXPECT_EQ(BookmarkSaveSpy::calls, 1);
}

TEST_F(BookmarkActionsTest, FailedRemovalPreservesEveryMatchAndIndicator) {
  EpubReaderActivity reader;
  reader.cachedBookmarks = {entry("match", 1), entry("other"), entry("duplicate", 1)};
  reader.currentPageBookmarked = true;
  const auto original = reader.cachedBookmarks;
  BookmarkSaveSpy::succeeds = false;
  BookmarkSaveSpy::observe = [&] {
    expectEntries(reader.cachedBookmarks, original);
    EXPECT_TRUE(reader.currentPageBookmarked);
  };
  reader.addBookmark();
  expectEntries(reader.cachedBookmarks, original);
  EXPECT_TRUE(reader.currentPageBookmarked);
  EXPECT_FALSE(reader.bookmarkRemoved);
  EXPECT_TRUE(reader.bookmarkSaveFailed);
  EXPECT_TRUE(reader.showBookmarkMessage);
  EXPECT_EQ(reader.updates, 1);
  ASSERT_EQ(BookmarkSaveSpy::proposed.size(), 1u);
  EXPECT_EQ(BookmarkSaveSpy::proposed.front().name, "other");
  EXPECT_TRUE(BookmarkSaveSpy::hadExclusion);
  EXPECT_FALSE(BookmarkSaveSpy::hadPrepend);
  EXPECT_EQ(reader.section->textReads, 0);
}

TEST_F(BookmarkActionsTest, SuccessfulRemovalPersistsAndRemovesTheSameMatches) {
  EpubReaderActivity reader;
  reader.cachedBookmarks = {entry("match", 1), entry("other"), entry("duplicate", 1)};
  reader.currentPageBookmarked = true;
  BookmarkSaveSpy::observe = [&] {
    EXPECT_EQ(reader.cachedBookmarks.size(), 3u);
    EXPECT_TRUE(reader.currentPageBookmarked);
  };
  reader.addBookmark();
  expectEntries(reader.cachedBookmarks, BookmarkSaveSpy::proposed);
  ASSERT_EQ(reader.cachedBookmarks.size(), 1u);
  EXPECT_EQ(reader.cachedBookmarks.front().name, "other");
  EXPECT_FALSE(reader.currentPageBookmarked);
  EXPECT_TRUE(reader.bookmarkRemoved);
  EXPECT_FALSE(reader.bookmarkSaveFailed);
  EXPECT_EQ(reader.section->textReads, 0);
}

TEST_F(BookmarkActionsTest, UndisplayedOrUnsettledPagesNeverPersistBookmarks) {
  for (int failure = 0; failure < 7; ++failure) {
    SCOPED_TRACE(failure);
    EpubReaderActivity reader;
    reader.cachedBookmarks = {entry("other")};
    reader.currentPageBookmarked = true;
    if (failure == 0) reader.section.reset();
    if (failure == 1) reader.epub.reset();
    if (failure == 2) reader.currentPageVisibleOffset.reset();
    if (failure == 3) reader.renderer.committed = false;
    if (failure == 4) reader.pendingPercentJump = true;
    if (failure == 5) reader.pendingLastPageJump = true;
    if (failure == 6) reader.pendingBuildError = true;
    reader.addBookmark();
    EXPECT_EQ(BookmarkSaveSpy::calls, 0);
    ASSERT_EQ(reader.cachedBookmarks.size(), 1u);
    EXPECT_EQ(reader.cachedBookmarks.front().name, "other");
    EXPECT_TRUE(reader.currentPageBookmarked);
    EXPECT_TRUE(reader.bookmarkSaveFailed);
    EXPECT_TRUE(reader.showBookmarkMessage);
    EXPECT_EQ(reader.updates, 1);
  }
}

TEST_F(BookmarkActionsTest, SuccessfulRetryClearsEarlierSaveFailure) {
  EpubReaderActivity reader;
  BookmarkSaveSpy::succeeds = false;
  reader.addBookmark();
  EXPECT_TRUE(reader.cachedBookmarks.empty());
  EXPECT_TRUE(reader.bookmarkSaveFailed);
  BookmarkSaveSpy::succeeds = true;
  reader.addBookmark();
  EXPECT_EQ(reader.cachedBookmarks.size(), 1u);
  EXPECT_FALSE(reader.bookmarkSaveFailed);
  EXPECT_TRUE(reader.currentPageBookmarked);
}

TEST_F(BookmarkActionsTest, FeedbackDistinguishesTranslatedFailureAdditionAndRemoval) {
  EpubReaderActivity reader;
  BookmarkSaveSpy::succeeds = false;
  reader.addBookmark();
  reader.renderFeedback();
  EXPECT_EQ(GUI.lastPopup, "translated save error");
  BookmarkSaveSpy::succeeds = true;
  reader.addBookmark();
  reader.renderFeedback();
  EXPECT_EQ(GUI.lastPopup, "translated added");
  reader.addBookmark();
  reader.renderFeedback();
  EXPECT_EQ(GUI.lastPopup, "translated removed");
}

TEST_F(BookmarkActionsTest, ExistingUnreadableBookmarkFileCannotBeReplacedByAnEmptyCache) {
  EpubReaderActivity reader;
  reader.bookmarkCacheValid = false;
  BookmarkLoadSpy::succeeds = false;
  BookmarkLoadSpy::exists = true;
  reader.addBookmark();
  EXPECT_EQ(BookmarkLoadSpy::calls, 1);
  EXPECT_EQ(BookmarkSaveSpy::calls, 0);
  EXPECT_FALSE(reader.bookmarkCacheValid);
  EXPECT_TRUE(reader.bookmarkSaveFailed);
  EXPECT_TRUE(reader.showBookmarkMessage);
  EXPECT_EQ(reader.updates, 1);
  EXPECT_EQ(reader.section->textReads, 0);
}

TEST_F(BookmarkActionsTest, TransientReadFailureRetriesAndPreservesRecoveredBookmarks) {
  EpubReaderActivity reader;
  reader.bookmarkCacheValid = false;
  BookmarkLoadSpy::succeeds = false;
  BookmarkLoadSpy::exists = true;
  reader.addBookmark();
  EXPECT_EQ(BookmarkSaveSpy::calls, 0);
  BookmarkLoadSpy::succeeds = true;
  BookmarkLoadSpy::entries = {entry("recovered")};
  reader.addBookmark();
  EXPECT_EQ(BookmarkLoadSpy::calls, 2);
  EXPECT_EQ(BookmarkSaveSpy::calls, 1);
  ASSERT_EQ(reader.cachedBookmarks.size(), 2u);
  EXPECT_EQ(reader.cachedBookmarks.back().name, "recovered");
  expectEntries(reader.cachedBookmarks, BookmarkSaveSpy::proposed);
  EXPECT_TRUE(reader.bookmarkCacheValid);
  EXPECT_FALSE(reader.bookmarkSaveFailed);
}

TEST_F(BookmarkActionsTest, MissingBookmarkFileAllowsFirstBookmarkCreation) {
  EpubReaderActivity reader;
  reader.bookmarkCacheValid = false;
  BookmarkLoadSpy::succeeds = false;
  BookmarkLoadSpy::exists = false;
  reader.addBookmark();
  EXPECT_EQ(BookmarkLoadSpy::calls, 1);
  EXPECT_EQ(BookmarkSaveSpy::calls, 1);
  EXPECT_TRUE(reader.bookmarkCacheValid);
  EXPECT_FALSE(reader.bookmarkSaveFailed);
  EXPECT_EQ(reader.cachedBookmarks.size(), 1u);
}

TEST_F(BookmarkActionsTest, KnownCacheDoesNotReloadTheBookmarkFileForEveryToggle) {
  EpubReaderActivity reader;
  reader.addBookmark();
  reader.addBookmark();
  EXPECT_EQ(BookmarkLoadSpy::calls, 0);
  EXPECT_EQ(BookmarkSaveSpy::calls, 2);
  EXPECT_TRUE(reader.cachedBookmarks.empty());
}

TEST_F(BookmarkActionsTest, FailedDeletePreservesListSelectionAndRouting) {
  EpubReaderBookmarksActivity activity;
  activity.bookmarks = {entry("first"), entry("selected"), entry("last")};
  activity.nav.selected = 1;
  activity.rowNames = {"first", "selected", "last"};
  const auto original = activity.bookmarks;
  BookmarkSaveSpy::succeeds = false;
  activity.deleteSelectedBookmark();
  expectEntries(activity.bookmarks, original);
  EXPECT_EQ(activity.nav.selected, 1);
  EXPECT_EQ(activity.routingCloses, 0);
  EXPECT_EQ(activity.rebuilds, 0);
  EXPECT_EQ(activity.rowNames, (std::vector<std::string>{"first", "selected", "last"}));
  EXPECT_EQ(activity.finishes, 0);
  EXPECT_FALSE(activity.result);
  EXPECT_EQ(GUI.lastPopup, "translated save error");
  ASSERT_EQ(BookmarkSaveSpy::proposed.size(), 2u);
  EXPECT_EQ(BookmarkSaveSpy::proposed[0].name, "first");
  EXPECT_EQ(BookmarkSaveSpy::proposed[1].name, "last");
}

TEST_F(BookmarkActionsTest, FailedDeleteOfOnlyBookmarkDoesNotCloseTheActivity) {
  EpubReaderBookmarksActivity activity;
  activity.bookmarks = {entry("only")};
  BookmarkSaveSpy::succeeds = false;
  activity.deleteSelectedBookmark();
  ASSERT_EQ(activity.bookmarks.size(), 1u);
  EXPECT_EQ(activity.bookmarks.front().name, "only");
  EXPECT_EQ(activity.nav.selected, 0);
  EXPECT_EQ(activity.finishes, 0);
  EXPECT_FALSE(activity.result);
  EXPECT_EQ(activity.routingCloses, 0);
  EXPECT_EQ(activity.rebuilds, 0);
  EXPECT_TRUE(BookmarkSaveSpy::proposed.empty());
}

TEST_F(BookmarkActionsTest, SuccessfulDeleteInvalidatesRoutingAfterSaveAndClampsSelection) {
  for (int selected : {0, 1, 2}) {
    SCOPED_TRACE(selected);
    BookmarkSaveSpy::reset();
    EpubReaderBookmarksActivity activity;
    activity.bookmarks = {entry("first"), entry("middle"), entry("last")};
    activity.nav.selected = selected;
    BookmarkSaveSpy::observe = [&] {
      EXPECT_EQ(activity.bookmarks.size(), 3u);
      EXPECT_EQ(activity.nav.selected, selected);
      EXPECT_EQ(activity.routingCloses, 0);
      EXPECT_EQ(activity.rebuilds, 0);
    };
    activity.deleteSelectedBookmark();
    expectEntries(activity.bookmarks, BookmarkSaveSpy::proposed);
    EXPECT_EQ(activity.nav.selected, std::min(selected, 1));
    EXPECT_EQ(activity.routingCloses, 1);
    EXPECT_EQ(activity.rebuilds, 1);
    EXPECT_EQ(activity.nav.follows, 1);
    EXPECT_EQ(activity.nav.lastCount, 2);
    EXPECT_EQ(activity.finishes, 0);
    EXPECT_TRUE(activity.lastUpdateImmediate);
    EXPECT_EQ(BookmarkSaveSpy::events, (std::vector<std::string>{"save", "close routing", "rebuild"}));
    BookmarkSaveSpy::observe = {};
  }
}

TEST_F(BookmarkActionsTest, SuccessfulDeleteOfLastBookmarkFinishesWithCancellation) {
  EpubReaderBookmarksActivity activity;
  activity.bookmarks = {entry("only")};
  activity.deleteSelectedBookmark();
  EXPECT_TRUE(activity.bookmarks.empty());
  EXPECT_EQ(activity.finishes, 1);
  ASSERT_TRUE(activity.result);
  EXPECT_TRUE(activity.result->isCancelled);
  EXPECT_EQ(activity.routingCloses, 1);
  EXPECT_EQ(activity.rebuilds, 1);
}

TEST_F(BookmarkActionsTest, InvalidDeleteSelectionDoesNotWriteOrChangeRouting) {
  for (int selected : {-1, 1, 10}) {
    EpubReaderBookmarksActivity activity;
    activity.bookmarks = {entry("only")};
    activity.nav.selected = selected;
    activity.deleteSelectedBookmark();
    EXPECT_EQ(BookmarkSaveSpy::calls, 0);
    EXPECT_EQ(activity.bookmarks.size(), 1u);
    EXPECT_EQ(activity.routingCloses, 0);
    EXPECT_EQ(activity.updates, 0);
  }
}

TEST_F(BookmarkActionsTest, FailedRenameRestoresNameAndDerivedRowsWithTranslatedError) {
  EpubReaderBookmarksActivity activity;
  activity.bookmarks = {entry("original")};
  activity.startRename();
  ASSERT_TRUE(activity.keyboard);
  EXPECT_EQ(activity.keyboard->initial, "original");
  EXPECT_EQ(activity.keyboard->title, "translated rename");
  EXPECT_EQ(activity.keyboard->maximum, BookmarkEntry::MAX_NAME_LENGTH);
  BookmarkSaveSpy::succeeds = false;
  activity.keyboardResult({false, KeyboardResult{"new name"}});
  EXPECT_EQ(activity.bookmarks.front().name, "original");
  EXPECT_EQ(activity.rowNames, (std::vector<std::string>{"original"}));
  EXPECT_EQ(activity.rebuilds, 2);
  EXPECT_EQ(activity.updates, 1);
  EXPECT_EQ(activity.finishes, 0);
  ASSERT_EQ(BookmarkSaveSpy::proposed.size(), 1u);
  EXPECT_EQ(BookmarkSaveSpy::proposed.front().name, "new name");
  EXPECT_EQ(GUI.lastPopup, "translated save error");
}

TEST_F(BookmarkActionsTest, SuccessfulRenameUsesTheCapturedRowAndRebuildsIt) {
  EpubReaderBookmarksActivity activity;
  activity.bookmarks = {entry("first"), entry("second")};
  activity.nav.selected = 0;
  activity.startRename();
  activity.nav.selected = 1;
  activity.keyboardResult({false, KeyboardResult{"renamed"}});
  EXPECT_EQ(activity.bookmarks[0].name, "renamed");
  EXPECT_EQ(activity.bookmarks[1].name, "second");
  EXPECT_EQ(activity.rowNames, (std::vector<std::string>{"renamed", "second"}));
  EXPECT_EQ(GUI.popups, 0);
  EXPECT_EQ(BookmarkSaveSpy::calls, 1);
}

TEST_F(BookmarkActionsTest, CancelledOrStaleRenameDoesNotPersistAnything) {
  EpubReaderBookmarksActivity activity;
  activity.bookmarks = {entry("only")};
  activity.startRename();
  activity.keyboardResult({true, KeyboardResult{"ignored"}});
  EXPECT_EQ(activity.bookmarks.front().name, "only");
  EXPECT_EQ(BookmarkSaveSpy::calls, 0);
  activity.bookmarks.clear();
  activity.keyboardResult({false, KeyboardResult{"stale"}});
  EXPECT_EQ(BookmarkSaveSpy::calls, 0);
  EXPECT_EQ(activity.rebuilds, 0);
}

TEST_F(BookmarkActionsTest, RenameAllocationFailureKeepsTheBookmark) {
  EpubReaderBookmarksActivity activity;
  activity.bookmarks = {entry("only")};
  failKeyboardAllocation = true;
  activity.startRename();
  EXPECT_FALSE(activity.keyboard);
  EXPECT_EQ(activity.bookmarks.front().name, "only");
  EXPECT_EQ(BookmarkSaveSpy::calls, 0);
}

TEST_F(BookmarkActionsTest, InvalidOpenSelectionDoesNotFinishOrThrow) {
  for (int selected : {-1, 1, 10}) {
    EpubReaderBookmarksActivity activity;
    activity.bookmarks = {entry("only")};
    activity.nav.selected = selected;
    activity.openSelectedBookmark();
    EXPECT_EQ(activity.finishes, 0);
    EXPECT_FALSE(activity.result);
  }
}

TEST_F(BookmarkActionsTest, OpenBookmarkCarriesExactOffsetEvenWhenPageHintsAreStale) {
  EpubReaderBookmarksActivity activity;
  activity.bookmarks = {entry("only", 2, 4)};
  activity.bookmarks.front().computedChapterPageCount = 0;
  activity.openSelectedBookmark();
  ASSERT_TRUE(activity.result);
  const auto* progress = std::get_if<ProgressChangeResult>(&activity.result->data);
  ASSERT_NE(progress, nullptr);
  EXPECT_EQ(progress->spineIndex, 2);
  EXPECT_EQ(progress->visibleTextOffset, 400u);
  EXPECT_TRUE(progress->hasVisibleTextOffset);
  EXPECT_TRUE(progress->hasSavedProgress);
  EXPECT_EQ(progress->page, -1);
  EXPECT_EQ(progress->totalPages, 0);
  EXPECT_EQ(activity.finishes, 1);
}

TEST_F(BookmarkActionsTest, BookmarkScreenDistinguishesMissingEmptyAndUnreadableFiles) {
  for (int state = 0; state < 3; ++state) {
    EpubReaderBookmarksActivity activity;
    activity.bookmarks = {entry("stale")};
    BookmarkLoadSpy::exists = state != 0;
    BookmarkLoadSpy::succeeds = state == 1;
    activity.onEnter();
    UiScreen screen;
    activity.buildScreen(screen);
    EXPECT_EQ(activity.loadFailed, state == 2);
    EXPECT_EQ(screen.message, state == 2 ? "translated load error" : "translated empty");
    EXPECT_TRUE(activity.bookmarks.empty());
    EXPECT_EQ(BookmarkSaveSpy::calls, 0);
    const int reads = BookmarkLoadSpy::calls;
    activity.buildScreen(screen);
    EXPECT_EQ(BookmarkLoadSpy::calls, reads);
  }
}

TEST_F(BookmarkActionsTest, FailedBookmarkLoadStillAllowsBackWithoutWritingOrSelecting) {
  EpubReaderBookmarksActivity activity;
  BookmarkLoadSpy::succeeds = false;
  activity.onEnter();
  activity.mappedInput.confirm = true;
  EXPECT_TRUE(activity.handleButtons());
  EXPECT_EQ(activity.finishes, 0);
  activity.mappedInput.confirm = false;
  activity.mappedInput.back = true;
  EXPECT_TRUE(activity.handleButtons());
  ASSERT_TRUE(activity.result);
  EXPECT_TRUE(activity.result->isCancelled);
  EXPECT_EQ(activity.finishes, 1);
  EXPECT_EQ(BookmarkLoadSpy::calls, 1);
  EXPECT_EQ(BookmarkSaveSpy::calls, 0);
}

TEST_F(BookmarkActionsTest, ReenteringBookmarkScreenClearsPriorReadErrorOnRecovery) {
  EpubReaderBookmarksActivity activity;
  BookmarkLoadSpy::succeeds = false;
  activity.onEnter();
  ASSERT_TRUE(activity.loadFailed);
  BookmarkLoadSpy::succeeds = true;
  BookmarkLoadSpy::entries = {entry("recovered")};
  activity.onEnter();
  EXPECT_FALSE(activity.loadFailed);
  ASSERT_EQ(activity.bookmarks.size(), 1u);
  EXPECT_EQ(activity.rowNames, (std::vector<std::string>{"recovered"}));
  UiScreen screen;
  activity.buildScreen(screen);
  EXPECT_TRUE(screen.message.empty());
  EXPECT_EQ(BookmarkSaveSpy::calls, 0);
}
