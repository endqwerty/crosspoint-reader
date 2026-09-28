#include <gtest/gtest.h>

#include "../huge_book_index/HeapCap.h"
#include "LibraryUiFixture.h"

class LibraryUiTest : public testing::Test {
 protected:
  void SetUp() override {
    SETTINGS = {};
    library::dirtyIndex = false;
    RECENT_BOOKS = {};
    GUI = {};
    Storage = {};
    clearedCaches.clear();
    library::states.clear();
    library::stateReadOk = library::stateWriteOk = true;
    library::stateReadCalls = 0;
    library::failStateKey = UINT64_MAX;
    library::librarySession.invalidate();
    lockDepth = locksTaken = 0;
    failNextAllocation = false;
  }
  static void populate(LibraryListActivity& ui) {
    ui.activeTabIndex = TITLE_TAB;
    ui.sortOrder = library::SortOrder::TitleAsc;
    ui.index.head.metadataEnabled = 1;
    ui.index.books = {{"Alpha", "Jane Austen", "/book1", "Long series", 0, 100, '1'},
                      {"Beta", "Mary Shelley", "/book2", "Long series", 0, 200, '2'},
                      {"Gamma", "Jane Austen", "/book3", "Long series", 1, 100, '3'},
                      {"Omega", "Other", "/book4", "", library::CLIX_SERIES_NONE, library::SERIES_INDEX_NONE, '4'}};
    ui.index.permutations[static_cast<size_t>(library::SortOrder::TitleDesc)] = {3, 2, 1, 0};
    ui.index.permutations[static_cast<size_t>(library::SortOrder::AddedDesc)] = {3, 2, 1, 0};
    ui.index.permutations[static_cast<size_t>(library::SortOrder::AuthorAsc)] = {0, 2, 3, 1};
    ui.index.permutations[static_cast<size_t>(library::SortOrder::AuthorDesc)] = {1, 3, 2, 0};
    ui.index.permutations[static_cast<size_t>(library::SortOrder::SeriesDesc)] = {3, 2, 1, 0};
    library::states['1'] = {true, library::ReadingState::Unread};
    library::states['2'] = {false, library::ReadingState::Reading};
    library::states['3'] = {true, library::ReadingState::Finished};
    library::states['4'] = {false, library::ReadingState::Unread};
  }
  static ActivityResult menu(int action) { return {false, MenuResult{action}}; }
};

TEST_F(LibraryUiTest, CacheReconcilesOncePerSessionAndAgainAfterInvalidation) {
  LibraryListActivity first;
  populate(first);
  first.onEnter();
  EXPECT_EQ(first.rebuilds, 1);
  EXPECT_TRUE(first.index.isOpen());
  EXPECT_EQ(lockDepth, 0);
  first.onExit();
  LibraryListActivity next;
  populate(next);
  next.onEnter();
  EXPECT_EQ(next.rebuilds, 0);
  EXPECT_TRUE(next.index.isOpen());
  library::librarySession.invalidate();
  LibraryListActivity changed;
  populate(changed);
  changed.onEnter();
  EXPECT_EQ(changed.rebuilds, 1);
}
TEST_F(LibraryUiTest, MetadataChangeAndFailedRebuildRemainRetryable) {
  library::librarySession.reconciled(true, library::librarySession.refreshToken());
  LibraryListActivity ui;
  populate(ui);
  ui.index.head.metadataEnabled = 0;
  ui.rebuildOk = false;
  ui.onEnter();
  EXPECT_EQ(ui.rebuilds, 1);
  EXPECT_TRUE(ui.refreshFailed);
  EXPECT_TRUE(library::librarySession.needsRefresh(true, false));
}
TEST_F(LibraryUiTest, TextMatchesTitleAuthorOrSeriesAndCombinesWithShelfFilter) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "jane";
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 2);
  EXPECT_EQ(ui.rowFor(0), 0);
  EXPECT_EQ(ui.rowFor(1), 2);
  ui.query = "long ser";
  ui.shelfFilter = library::ShelfFilter::Reading;
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.rowFor(0), 1);
  ui.query = "alpha";
  ui.shelfFilter = library::ShelfFilter::Finished;
  ui.applyFilter();
  EXPECT_EQ(ui.filteredCount, 0);
  EXPECT_FALSE(ui.filterFailed);
}
TEST_F(LibraryUiTest, FilterErrorsDoNotExposePartialOrUnfilteredResults) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "long";
  ui.index.failOrdinal = 2;
  ui.applyFilter();
  EXPECT_TRUE(ui.filterFailed);
  EXPECT_EQ(ui.filteredCount, 0);
  EXPECT_FALSE(ui.filtered);
  EXPECT_EQ(ui.bookRowCount(), 0);
  EXPECT_EQ(ui.rowFor(0), -1);
  ui.index.failOrdinal = -1;
  ui.index.failed = false;
  failNextAllocation = true;
  ui.applyFilter();
  EXPECT_TRUE(ui.filterFailed);
  EXPECT_EQ(ui.filteredCount, 0);
}
TEST_F(LibraryUiTest, StateReadFailureCannotMasqueradeAsUnreadOrAllBooks) {
  LibraryListActivity ui;
  populate(ui);
  ui.shelfFilter = library::ShelfFilter::Unread;
  library::stateReadOk = false;
  ui.applyFilter();
  EXPECT_TRUE(ui.filterFailed);
  EXPECT_EQ(ui.bookRowCount(), 0);
}
TEST_F(LibraryUiTest, SortToggleRebuildsFilteredMappingAndResetsTabFocus) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "jane";
  ui.applyFilter();
  ui.activeNav().selected = 2;
  ui.toggleSortDirection();
  EXPECT_EQ(ui.sortOrder, library::SortOrder::TitleDesc);
  ASSERT_EQ(ui.filteredCount, 2);
  EXPECT_EQ(ui.rowFor(0), 1);
  EXPECT_EQ(ui.rowFor(1), 3);
  EXPECT_EQ(ui.activeNav().selected, 0);
  std::string title, author;
  ASSERT_TRUE(ui.rowTextFor(0, title, author));
  EXPECT_EQ(title, "Gamma");
}
TEST_F(LibraryUiTest, RecentTabClearsShelfFilterAndQuery) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "jane";
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.applyFilter();
  ui.selectTab(RECENT_TAB, false);
  EXPECT_EQ(ui.shelfFilter, library::ShelfFilter::All);
  EXPECT_TRUE(ui.query.empty());
  EXPECT_FALSE(ui.filtered);
}
TEST_F(LibraryUiTest, VisibleBookUsesOneCombinedMetadataRequest) {
  LibraryListActivity ui;
  populate(ui);
  std::string title, author;
  ASSERT_TRUE(ui.rowTextFor(0, title, author));
  EXPECT_EQ(title, "Alpha");
  EXPECT_EQ(author, "Jane Austen");
  EXPECT_EQ(ui.index.metadataReadCalls, 1);
  EXPECT_EQ(ui.index.titleReadCalls, 0);
  EXPECT_EQ(ui.index.authorReadCalls, 0);
  EXPECT_EQ(ui.index.nameReadCalls, 0);
}
TEST_F(LibraryUiTest, VisibleTitleHeadingComesFromTheSameRecordAsItsLabel) {
  LibraryListActivity ui;
  populate(ui);
  std::string title, author;
  uint32_t initial = 123;
  ASSERT_TRUE(ui.rowTextFor(0, title, author, &initial));
  EXPECT_EQ(initial, 'a');
  EXPECT_EQ(ui.index.readCalls, 1);
  EXPECT_EQ(ui.index.metadataReadCalls, 1);
  EXPECT_FALSE(ui.rowTextFor(-1, title, author, &initial));
  EXPECT_EQ(initial, 0u);
}
TEST_F(LibraryUiTest, MissingTitleFallsBackToFilenameAndEmptyAuthorRemainsValid) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.books[0].hasTitleMetadata = false;
  ui.index.books[0].author.clear();
  ui.index.books[0].fileName = "Book on SD.epub";
  std::string title, author;
  ASSERT_TRUE(ui.rowTextFor(0, title, author));
  EXPECT_EQ(title, "Book on SD.epub");
  EXPECT_TRUE(author.empty());
  EXPECT_EQ(ui.index.metadataReadCalls, 1);
  EXPECT_EQ(ui.index.nameReadCalls, 1);
}
TEST_F(LibraryUiTest, FailedCombinedMetadataDoesNotMasqueradeAsMissingMetadata) {
  for (bool failAuthor : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    if (failAuthor)
      ui.index.failAuthorOrdinal = 0;
    else
      ui.index.failTitleOrdinal = 0;
    std::string title = "stale", author = "stale";
    EXPECT_FALSE(ui.rowTextFor(0, title, author));
    EXPECT_TRUE(title.empty());
    EXPECT_TRUE(author.empty());
    EXPECT_TRUE(ui.index.ioFailed());
    EXPECT_EQ(ui.index.nameReadCalls, 0);
  }
}
TEST_F(LibraryUiTest, FilenameFallbackReadFailureRejectsVisibleRow) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.books[0].hasTitleMetadata = false;
  ui.index.failNameOrdinal = 0;
  std::string title, author;
  EXPECT_FALSE(ui.rowTextFor(0, title, author));
  EXPECT_TRUE(ui.index.ioFailed());
}
TEST_F(LibraryUiTest, StaleFilteredRowCannotOpenOrResolveFirstBook) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "alpha";
  ui.applyFilter();
  std::string path, title, author;
  EXPECT_EQ(ui.rowFor(-1), -1);
  EXPECT_EQ(ui.rowFor(1), -1);
  EXPECT_FALSE(ui.resolveBook(1, path, title));
  EXPECT_FALSE(ui.rowTextFor(1, title, author));
  ui.activeNav().selected = 3;
  ui.openSelectedBook();
  EXPECT_TRUE(ui.selectedPath.empty());
  ui.activateIndex(6);
  EXPECT_TRUE(ui.selectedPath.empty());
}
TEST_F(LibraryUiTest, SeriesGroupsUseIdentityRatherThanTruncatedLabel) {
  LibraryListActivity ui;
  populate(ui);
  ui.sortOrder = library::SortOrder::SeriesAsc;
  ASSERT_TRUE(ui.collapseGroups(2));
  ASSERT_EQ(ui.groupCount, 3);
  EXPECT_EQ(ui.groupStarts[0], 0);
  EXPECT_EQ(ui.groupStarts[1], 2);
  EXPECT_EQ(ui.groupStarts[2], 3);
  EXPECT_EQ(ui.activeNav().selected, 2);
  ui.expandGroup(1);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 1);
  EXPECT_EQ(ui.activeNav().selected, 1);
  EXPECT_EQ(ui.activeNav().top, 0);
  EXPECT_EQ(ui.listCount(), 1);
  EXPECT_EQ(ui.rowFor(0), 2);
}
TEST_F(LibraryUiTest, InvalidGroupActionsLeaveSelectionUntouched) {
  LibraryListActivity ui;
  populate(ui);
  ui.sortOrder = library::SortOrder::SeriesAsc;
  ASSERT_TRUE(ui.collapseGroups(0));
  const auto nav = ui.activeNav();
  ui.expandGroup(-1);
  ui.expandGroup(ui.groupCount);
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.activeNav().selected, nav.selected);
  failNextAllocation = true;
  LibraryListActivity noMemory;
  populate(noMemory);
  EXPECT_FALSE(noMemory.collapseGroups(0));
}
TEST_F(LibraryUiTest, ChildCancellationRestoresIndexUnderLockWithoutMutation) {
  LibraryListActivity ui;
  populate(ui);
  for (int operation = 0; operation < 5; ++operation) {
    if (operation == 0) ui.openOptions();
    if (operation == 1) ui.openGrouping();
    if (operation == 2) ui.openShelfFilter();
    if (operation == 3) ui.openBookOptions(0);
    if (operation == 4) ui.openSearch();
    ASSERT_TRUE(ui.child);
    EXPECT_FALSE(ui.childStartedWithIndexOpen);
    EXPECT_FALSE(ui.index.isOpen());
    const int before = locksTaken;
    ui.returnChild({true, {}});
    EXPECT_GT(locksTaken, before);
    EXPECT_EQ(lockDepth, 0);
    EXPECT_TRUE(ui.index.isOpen());
    EXPECT_EQ(ui.activeTabIndex, TITLE_TAB);
    EXPECT_TRUE(ui.query.empty());
  }
  EXPECT_EQ(SETTINGS.saves, 0);
  EXPECT_TRUE(Storage.removed.empty());
}
TEST_F(LibraryUiTest, FailedChildIndexReopenStopsActionAndRequestsReconciliation) {
  LibraryListActivity ui;
  populate(ui);
  library::librarySession.reconciled(true, library::librarySession.refreshToken());
  ui.openBookOptions(0);
  ui.index.openOk = false;
  ui.returnChild(menu(0));
  EXPECT_TRUE(ui.selectedPath.empty());
  EXPECT_TRUE(ui.refreshFailed);
  EXPECT_TRUE(library::librarySession.needsRefresh(true, true));
}
TEST_F(LibraryUiTest, CancelledOrFailedDeleteKeepsBookCacheAndRecents) {
  LibraryListActivity ui;
  populate(ui);
  RECENT_BOOKS.books = {{"/book1", "Alpha", "Jane"}};
  ui.promptDeleteBook("/book1", "Alpha");
  EXPECT_FALSE(ui.childStartedWithIndexOpen);
  ui.returnChild({true, {}});
  EXPECT_TRUE(Storage.removed.empty());
  EXPECT_TRUE(clearedCaches.empty());
  EXPECT_EQ(RECENT_BOOKS.books.size(), 1);
  Storage.removeOk = false;
  ui.promptDeleteBook("/book1", "Alpha");
  ui.returnChild({false, {}});
  EXPECT_TRUE(clearedCaches.empty());
  EXPECT_EQ(RECENT_BOOKS.books.size(), 1);
  EXPECT_EQ(RECENT_BOOKS.saves, 0);
  EXPECT_TRUE(ui.index.isOpen());
  EXPECT_EQ(ui.rebuilds, 0);
}
TEST_F(LibraryUiTest, SuccessfulDeleteClearsCacheAndRecentOnlyAfterFileRemoval) {
  LibraryListActivity ui;
  populate(ui);
  RECENT_BOOKS.books = {{"/book1", "Alpha", "Jane"}};
  ui.promptDeleteBook("/book1", "Alpha");
  ui.returnChild({false, {}});
  ASSERT_EQ(Storage.removed.size(), 1);
  EXPECT_EQ(Storage.removed[0], "/book1");
  ASSERT_EQ(clearedCaches.size(), 1);
  EXPECT_EQ(clearedCaches[0], "/book1");
  EXPECT_TRUE(RECENT_BOOKS.books.empty());
  EXPECT_EQ(RECENT_BOOKS.saves, 1);
  EXPECT_EQ(ui.rebuilds, 1);
}
TEST_F(LibraryUiTest, BookActionsPreserveReadingStateWhenTogglingFavorite) {
  LibraryListActivity ui;
  populate(ui);
  ui.openBookOptions(2);
  ui.returnChild(menu(1));
  EXPECT_FALSE(library::states['3'].favorite);
  EXPECT_EQ(library::states['3'].reading, library::ReadingState::Finished);
  ui.openBookOptions(0);
  ui.returnChild(menu(4));
  EXPECT_TRUE(library::states['1'].favorite);
  EXPECT_EQ(library::states['1'].reading, library::ReadingState::Finished);
}
TEST_F(LibraryUiTest, UnavailableGroupActionIsOmittedFromAddedShelf) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(ADDED_TAB, false);
  ui.openBookOptions(0);
  ASSERT_TRUE(ui.child);
  EXPECT_EQ(ui.child->labels.size(), 7);
  EXPECT_EQ(ui.child->labels.back(), StrId::STR_LIBRARY_BOOK_DETAILS);
}
TEST_F(LibraryUiTest, LongConfirmThresholdOpensOptionsAndResumedHeldReleaseIsConsumed) {
  LibraryListActivity ui;
  populate(ui);
  ui.activeNav().selected = 0;
  ui.mappedInput.confirmLongPress = true;
  ui.mappedInput.held = LONG_PRESS_MS;
  EXPECT_TRUE(ui.handleButtons());
  ASSERT_TRUE(ui.child);
  EXPECT_EQ(ui.child->kind, Child::Menu);
  ui.mappedInput.confirmPressed = true;
  ui.returnChild({true, {}});
  EXPECT_TRUE(ui.lockNextConfirmRelease);
  ui.mappedInput.confirmReleased = true;
  EXPECT_TRUE(ui.handleCustomInput());
  EXPECT_FALSE(ui.lockNextConfirmRelease);
  EXPECT_TRUE(ui.selectedPath.empty());
}
TEST_F(LibraryUiTest, GroupingPreferenceOnlyPersistsAChange) {
  LibraryListActivity ui;
  populate(ui);
  ui.openGrouping();
  ui.returnChild(menu(2));
  EXPECT_TRUE(SETTINGS.libraryGroupBySeries);
  EXPECT_EQ(ui.sortOrder, library::SortOrder::SeriesAsc);
  EXPECT_EQ(SETTINGS.saves, 1);
  ui.openGrouping();
  ui.returnChild(menu(2));
  EXPECT_EQ(SETTINGS.saves, 1);
}

TEST_F(LibraryUiTest, FailedExplicitRefreshDoesNotMarkCachedIndexFresh) {
  LibraryListActivity ui;
  populate(ui);
  library::librarySession.reconciled(true, library::librarySession.refreshToken());
  ui.rebuildOk = false;
  ui.refreshLibrary();
  EXPECT_TRUE(ui.refreshFailed);
  EXPECT_TRUE(library::librarySession.needsRefresh(true, true));
}

TEST_F(LibraryUiTest, VisibleSeriesRowsOnlyAddHeadersAtIdentityBoundaries) {
  LibraryListActivity ui;
  populate(ui);
  ui.sortOrder = library::SortOrder::SeriesAsc;
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 4);
  EXPECT_NE(ui.winItems[0].sectionHeading, nullptr);
  EXPECT_EQ(ui.winItems[1].sectionHeading, nullptr);
  EXPECT_NE(ui.winItems[2].sectionHeading, nullptr);
  EXPECT_NE(ui.winItems[3].sectionHeading, nullptr);
  EXPECT_STREQ(ui.winItems[0].label, "1 · Alpha");
  EXPECT_STREQ(ui.winItems[1].label, "2 · Beta");
  EXPECT_EQ(screen.props.inputMask, fui::InputTouch | fui::InputLongPress);
  ui.activeNav().visibleRows = 2;
  ui.activeNav().top = 1;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 3);
  EXPECT_EQ(screen.props.itemsWindowFirst, 1);
  EXPECT_EQ(ui.winItems[0].actionValue, 1);
  EXPECT_EQ(ui.winItems[1].actionValue, 2);
  EXPECT_NE(ui.winItems[0].sectionHeading, nullptr);
  EXPECT_NE(ui.winItems[1].sectionHeading, nullptr);
}
TEST_F(LibraryUiTest, VisibleWindowStaysBoundedAtEndOfLargeLibrary) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.books.resize(4096, ui.index.books.back());
  ui.activeNav().visibleRows = 7;
  ui.activeNav().top = 4094;
  UiScreen screen;
  ui.buildRows(screen);
  EXPECT_EQ(ui.winItems.size(), 7);
  EXPECT_EQ(screen.props.itemsWindowFirst, 4089);
  EXPECT_EQ(screen.props.itemsWindowCount, 7);
  EXPECT_EQ(ui.winItems.back().actionValue, 4095);
  EXPECT_EQ(ui.winTitles.size(), 8);
  EXPECT_EQ(ui.winAuthors.size(), 8);
  EXPECT_EQ(ui.index.readCalls, 7);
  EXPECT_EQ(ui.index.metadataReadCalls, 7);
}

TEST_F(LibraryUiTest, IndexOpenFailureAfterRebuildIsVisibleAndRetryable) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.openOk = false;
  ui.onEnter();
  EXPECT_TRUE(ui.refreshFailed);
  EXPECT_FALSE(ui.index.isOpen());
  EXPECT_TRUE(library::librarySession.needsRefresh(true, true));
}

TEST_F(LibraryUiTest, BookRowsWrapWithoutMovingFocusThroughTabs) {
  LibraryListActivity ui;
  populate(ui);
  ui.activeNav().selected = 4;
  ui.pressNext();
  EXPECT_EQ(ui.ringPos(), 1);
  ui.pressPrevious();
  EXPECT_EQ(ui.ringPos(), 4);
  EXPECT_TRUE(ui.activeNav().followOnBuild);
}

TEST_F(LibraryUiTest, EmptyShelfNavigationKeepsTabFocusAndSearchAvailable) {
  LibraryListActivity ui;
  ui.activeTabIndex = TITLE_TAB;
  ui.pressNext();
  EXPECT_EQ(ui.ringPos(), 0);
  ui.pressPrevious();
  EXPECT_EQ(ui.ringPos(), 0);
  EXPECT_FALSE(ui.child);
  ui.buttonNavigator.previousRelease = true;
  ui.navigateButtons();
  ASSERT_TRUE(ui.child);
  EXPECT_EQ(ui.child->kind, Child::Keyboard);
}

TEST_F(LibraryUiTest, HeldNavigationChangesTabsOnlyWhileTabsAreFocused) {
  LibraryListActivity ui;
  populate(ui);
  ui.pressNext();
  EXPECT_EQ(ui.ringPos(), 1);
  ui.buttonNavigator.nextHold = true;
  ui.navigateButtons();
  EXPECT_EQ(ui.activeTabIndex, AUTHOR_TAB);
  EXPECT_EQ(ui.ringPos(), 0);
  ui.pressPrevious();
  EXPECT_EQ(ui.ringPos(), 0);
  ui.buttonNavigator.previousHold = true;
  ui.navigateButtons();
  EXPECT_EQ(ui.activeTabIndex, TITLE_TAB);
}

TEST_F(LibraryUiTest, HeldPageJumpUsesPublishedDrawnRowsAndDefersViewportMutation) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.books.resize(12, ui.index.books.back());
  auto& nav = ui.activeNav();
  nav.visibleRows = 6;
  nav.selected = 3;
  nav.top = 1;
  nav.onListRendered(1, 2, true);
  ui.buttonNavigator.nextHold = true;
  ui.navigateButtons();
  EXPECT_EQ(ui.ringPos(), 5);
  EXPECT_EQ(nav.top, 1);
  EXPECT_TRUE(nav.followOnBuild);
  fui::ListProps props;
  nav.syncToProps({0, 0, 200, 20}, 10, 0, ui.listCount(), props, 1);
  EXPECT_EQ(props.selectedIndex, 4);
  EXPECT_EQ(props.topIndex, 3);
}

TEST_F(LibraryUiTest, BackRestoresExpandedBooksBeforeReturningFocusToTabs) {
  LibraryListActivity ui;
  populate(ui);
  ui.activeNav().selected = 3;
  ASSERT_TRUE(ui.collapseGroups(2));
  ui.mappedInput.backReleased = true;
  EXPECT_TRUE(ui.handleButtons());
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.ringPos(), 3);
  EXPECT_EQ(ui.homes, 0);
  EXPECT_TRUE(ui.handleButtons());
  EXPECT_EQ(ui.ringPos(), 0);
  EXPECT_EQ(ui.homes, 0);
}

TEST_F(LibraryUiTest, FilterChangeClearsQueuedScrollAndKeepsQueryCaption) {
  LibraryListActivity ui;
  populate(ui);
  ui.activeNav().requestScroll(50);
  ui.query = "long";
  ui.applyFilter();
  EXPECT_EQ(ui.headerSearchTitle, "\"long\"");
  fui::ListProps props;
  ui.activeNav().syncToProps({0, 0, 200, 10}, 10, 0, ui.listCount(), props, 1);
  EXPECT_EQ(props.topIndex, 0);
  ui.query.clear();
  ui.applyFilter();
  EXPECT_TRUE(ui.headerSearchTitle.empty());
}

TEST_F(LibraryUiTest, SearchFindsTitleWordsBeyondStoredPrefix) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.books[0].title = std::string(104, 'x') + " rarequartz";
  ui.query = "rarequartz";
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.rowFor(0), 0);
  EXPECT_EQ(ui.index.titleReadCalls, 1);
  EXPECT_EQ(ui.index.nameReadCalls, 0);
  EXPECT_FALSE(ui.filterFailed);
}

TEST_F(LibraryUiTest, SearchFindsUtf8TitleAfterIncompleteFourByteBoundary) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.books[0].title = std::string(93, 'x') + "\xF0\xA0\x80\x80 银河";
  library::ClixRecord record{};
  ASSERT_TRUE(ui.index.readRecord(0, record));
  ASSERT_EQ(record.foldLen, library::CLIX_FOLD_BYTES - 3);
  ui.query = "银河";
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.rowFor(0), 0);
  EXPECT_EQ(ui.index.titleReadCalls, 1);
}

TEST_F(LibraryUiTest, MissingMetadataSearchUsesCompleteFilenameStemWithoutExtension) {
  LibraryListActivity ui;
  populate(ui);
  auto& book = ui.index.books[0];
  book.title.clear();
  book.hasTitleMetadata = false;
  book.fileName = std::string(104, 'x') + " archive.epub";
  ui.query = "archive";
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.rowFor(0), 0);
  EXPECT_EQ(ui.index.nameReadCalls, 1);
  ui.query = "epub";
  ui.applyFilter();
  EXPECT_EQ(ui.filteredCount, 0);
  EXPECT_FALSE(ui.filterFailed);
}

TEST_F(LibraryUiTest, StoredTitlePrefixMatchAvoidsFullTitleRead) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.books[0].title = "quartz " + std::string(104, 'x');
  ui.query = "quartz";
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.index.titleReadCalls, 0);
  EXPECT_EQ(ui.index.nameReadCalls, 0);
}

TEST_F(LibraryUiTest, MatchingPrefixesAvoidAllMetadataAndStateReads) {
  LibraryListActivity ui;
  populate(ui);
  for (auto& book : ui.index.books) book.title = "quartz " + std::string(104, 'x');
  ui.query = "quartz";
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 4);
  EXPECT_EQ(ui.index.titleReadCalls, 0);
  EXPECT_EQ(ui.index.nameReadCalls, 0);
  EXPECT_EQ(ui.index.authorReadCalls, 0);
  EXPECT_EQ(ui.index.seriesRefReadCalls, 0);
  EXPECT_EQ(library::stateReadCalls, 0u);
}

TEST_F(LibraryUiTest, FullTitleAndFilenameReadFailuresDiscardPartialMatches) {
  for (const bool metadata : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    ui.index.books[0].title = "needle";
    ui.index.books[1].title = std::string(104, 'x') + " needle";
    ui.index.books[1].hasTitleMetadata = metadata;
    if (metadata) {
      ui.index.failTitleOrdinal = 1;
    } else {
      ui.index.books[1].fileName = ui.index.books[1].title + ".epub";
      ui.index.books[1].title.clear();
      ui.index.failNameOrdinal = 1;
    }
    ui.query = "needle";
    ui.applyFilter();
    EXPECT_TRUE(ui.filterFailed);
    EXPECT_EQ(ui.filteredCount, 0);
    EXPECT_FALSE(ui.filtered);
  }
}

TEST_F(LibraryUiTest, NarrowQueryChecksOneShelfStateAmong4096Books) {
  LibraryListActivity ui;
  populate(ui);
  library::Book ordinary;
  ordinary.title = "ordinary title";
  ordinary.path = "/ordinary";
  ui.index.books.assign(4096, ordinary);
  for (size_t i = 0; i < ui.index.books.size(); ++i) ui.index.books[i].key = i;
  constexpr uint16_t MATCH = 3097;
  ui.index.books[MATCH].title = "rarequartz";
  library::states[MATCH] = {true, library::ReadingState::Unread};
  ui.query = "rarequartz";
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.rowFor(0), MATCH);
  EXPECT_EQ(ui.index.readCalls, 4096);
  EXPECT_EQ(ui.index.titleReadCalls, 0);
  EXPECT_EQ(ui.index.pathHashReadCalls, 1);
  EXPECT_EQ(library::stateReadCalls, 1u);
}

TEST_F(LibraryUiTest, EmptyQueryChecksEveryShelfStateWithoutTitleReads) {
  LibraryListActivity ui;
  populate(ui);
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.applyFilter();
  EXPECT_EQ(ui.filteredCount, 2);
  EXPECT_EQ(library::stateReadCalls, 4u);
  EXPECT_EQ(ui.index.titleReadCalls, 0);
  ui.shelfFilter = library::ShelfFilter::All;
  library::stateReadCalls = 0;
  ui.applyFilter();
  EXPECT_FALSE(ui.filtered);
  EXPECT_EQ(ui.bookRowCount(), 4);
  EXPECT_EQ(library::stateReadCalls, 0u);
}

TEST_F(LibraryUiTest, StateErrorsOnlyAffectMatchingCandidates) {
  LibraryListActivity ui;
  populate(ui);
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.query = "alpha";
  library::failStateKey = '2';
  ui.applyFilter();
  EXPECT_EQ(ui.filteredCount, 1);
  EXPECT_FALSE(ui.filterFailed);
  library::failStateKey = '1';
  ui.applyFilter();
  EXPECT_EQ(ui.filteredCount, 0);
  EXPECT_TRUE(ui.filterFailed);
  EXPECT_FALSE(ui.filtered);
}

TEST_F(LibraryUiTest, CandidatePathHashFailureCannotExposeUnfilteredResults) {
  LibraryListActivity ui;
  populate(ui);
  ui.shelfFilter = library::ShelfFilter::Unread;
  ui.query = "alpha";
  ui.index.failPathHashOrdinal = 0;
  ui.applyFilter();
  EXPECT_TRUE(ui.filterFailed);
  EXPECT_EQ(ui.filteredCount, 0);
  EXPECT_FALSE(ui.filtered);
}

TEST_F(LibraryUiTest, AuthorRootShowsNamesWithoutBookTitlesOrSubtitles) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  ASSERT_EQ(ui.listCount(), 3);
  ui.activeNav().visibleRows = 4;
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 3);
  EXPECT_STREQ(ui.winItems[0].label, "Austen, Jane");
  EXPECT_STREQ(ui.winItems[1].label, "Other");
  EXPECT_STREQ(ui.winItems[2].label, "Shelley, Mary");
  for (const auto& item : ui.winItems) {
    EXPECT_EQ(item.subtitle, nullptr);
    EXPECT_EQ(item.sectionHeading, nullptr);
  }
}

TEST_F(LibraryUiTest, AuthorChildContainsOnlySelectedAuthorsBooks) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(0);
  ASSERT_FALSE(ui.groupsCollapsed);
  ASSERT_EQ(ui.selectedGroup, 0);
  EXPECT_EQ(ui.groupTitle, "Austen, Jane");
  EXPECT_STREQ(ui.headerTitle(), "Austen, Jane");
  EXPECT_EQ(ui.totalBookRowCount(), 4);
  EXPECT_EQ(ui.bookRowCount(), 2);
  EXPECT_TRUE(ui.selectedPath.empty());
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 2);
  EXPECT_STREQ(ui.winItems[0].label, "Alpha");
  EXPECT_STREQ(ui.winItems[1].label, "Gamma");
  for (const auto& item : ui.winItems) EXPECT_EQ(item.sectionHeading, nullptr);
  std::string path, title;
  ASSERT_TRUE(ui.resolveBook(1, path, title));
  EXPECT_EQ(path, "/book3");
  EXPECT_EQ(title, "Gamma");
  EXPECT_FALSE(ui.resolveBook(2, path, title));
  EXPECT_EQ(ui.rowFor(-1), -1);
  EXPECT_EQ(ui.rowFor(2), -1);
  ui.activeNav().selected = 2;
  ui.activateIndex(1);
  EXPECT_EQ(ui.selectedPath, "/book3");
}

TEST_F(LibraryUiTest, NonFirstAuthorChildMapsLocalRowForBookActions) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(2);
  ASSERT_EQ(ui.selectedGroup, 2);
  ASSERT_EQ(ui.listCount(), 1);
  EXPECT_EQ(ui.rowFor(0), 3);
  ui.openBookOptions(0);
  ASSERT_TRUE(ui.child);
  EXPECT_EQ(ui.child->title, "Beta");
  ui.returnChild(menu(0));
  EXPECT_EQ(ui.selectedPath, "/book2");
}

TEST_F(LibraryUiTest, SeriesRootShowsNamesAndChildKeepsVolumeOrder) {
  LibraryListActivity ui;
  populate(ui);
  SETTINGS.libraryGroupBySeries = true;
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_TRUE(ui.groupsCollapsed);
  ASSERT_EQ(ui.listCount(), 3);
  ui.activeNav().visibleRows = 4;
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 3);
  EXPECT_STREQ(ui.winItems[0].label, "Long series");
  EXPECT_STREQ(ui.winItems[1].label, "Long series");
  for (const auto& item : ui.winItems) {
    EXPECT_EQ(item.subtitle, nullptr);
    EXPECT_EQ(item.sectionHeading, nullptr);
  }
  ui.activateIndex(0);
  EXPECT_EQ(ui.groupTitle, "Long series");
  EXPECT_STREQ(ui.headerTitle(), "Long series");
  ASSERT_EQ(ui.listCount(), 2);
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 2);
  EXPECT_STREQ(ui.winItems[0].label, "1 · Alpha");
  EXPECT_STREQ(ui.winItems[1].label, "2 · Beta");
  EXPECT_STREQ(ui.winItems[0].subtitle, "Jane Austen");
  for (const auto& item : ui.winItems) EXPECT_EQ(item.sectionHeading, nullptr);
}

TEST_F(LibraryUiTest, EqualDisplayedSeriesNamesRemainSeparateChildren) {
  LibraryListActivity ui;
  populate(ui);
  SETTINGS.libraryGroupBySeries = true;
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(1);
  ASSERT_EQ(ui.listCount(), 1);
  std::string path, title;
  ASSERT_TRUE(ui.resolveBook(0, path, title));
  EXPECT_EQ(path, "/book3");
  EXPECT_FALSE(ui.resolveBook(1, path, title));
}

TEST_F(LibraryUiTest, StandaloneBooksRemainReachableAsTheirOwnGroup) {
  LibraryListActivity ui;
  populate(ui);
  SETTINGS.libraryGroupBySeries = true;
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(2);
  ASSERT_EQ(ui.listCount(), 1);
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 1);
  EXPECT_STREQ(ui.winItems[0].label, "Omega");
  std::string path, title;
  ASSERT_TRUE(ui.resolveBook(0, path, title));
  EXPECT_EQ(path, "/book4");
}

TEST_F(LibraryUiTest, BackReturnsToSelectedGroupBeforeClearingSearch) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "long";
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 2);
  ui.activeNav().selected = 2;
  ui.activeNav().top = 1;
  ui.activateIndex(1);
  ASSERT_EQ(ui.listCount(), 1);
  ui.mappedInput.backReleased = true;
  EXPECT_TRUE(ui.handleButtons());
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  EXPECT_EQ(ui.query, "long");
  EXPECT_TRUE(ui.groupTitle.empty());
  EXPECT_EQ(ui.activeNav().selected, 2);
  EXPECT_EQ(ui.activeNav().top, 1);
  EXPECT_EQ(ui.listCount(), 2);
  EXPECT_EQ(ui.homes, 0);
  EXPECT_TRUE(ui.handleButtons());
  EXPECT_TRUE(ui.query.empty());
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.listCount(), 3);
}

TEST_F(LibraryUiTest, GroupRootBackFocusesTabsThenReturnsHome) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activeNav().selected = 2;
  ui.mappedInput.backReleased = true;
  EXPECT_TRUE(ui.handleButtons());
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.activeNav().selected, 0);
  EXPECT_EQ(ui.homes, 0);
  EXPECT_TRUE(ui.handleButtons());
  EXPECT_EQ(ui.homes, 1);
  EXPECT_TRUE(ui.groupsCollapsed);
}

TEST_F(LibraryUiTest, GroupMapIsReusedWhenEnteringAndLeavingChild) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  const auto* starts = ui.groupStarts.get();
  const int reads = ui.index.readCalls;
  ui.activeNav().selected = 3;
  ui.activateIndex(2);
  failNextAllocation = true;
  ASSERT_TRUE(ui.collapseGroups(0));
  EXPECT_TRUE(failNextAllocation);
  EXPECT_EQ(ui.groupStarts.get(), starts);
  EXPECT_EQ(ui.selectedGroup, -1);
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.activeNav().selected, 3);
  // Entering the child may read its name; returning to the root must not rescan.
  EXPECT_LE(ui.index.readCalls - reads, 1);
}

TEST_F(LibraryUiTest, AuthorFilterMappingIsAppliedBeforeChildOffset) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "long";
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.filteredCount, 3);
  ASSERT_EQ(ui.listCount(), 2);
  ui.activateIndex(1);
  ASSERT_EQ(ui.listCount(), 1);
  EXPECT_EQ(ui.rowFor(0), 3);
  std::string path, title;
  ASSERT_TRUE(ui.resolveBook(0, path, title));
  EXPECT_EQ(path, "/book2");
  EXPECT_FALSE(ui.resolveBook(1, path, title));
}

TEST_F(LibraryUiTest, ShelfFilterOmitsEmptyGroupsAndKeepsMatchingChildBooks) {
  LibraryListActivity ui;
  populate(ui);
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 1);
  ui.activateIndex(0);
  ASSERT_EQ(ui.listCount(), 2);
  std::string path, title;
  ASSERT_TRUE(ui.resolveBook(1, path, title));
  EXPECT_EQ(path, "/book3");
}

TEST_F(LibraryUiTest, ChangingSortFromChildReturnsToFreshGroupRoot) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(0);
  ui.toggleSortDirection();
  ASSERT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  EXPECT_TRUE(ui.groupTitle.empty());
  EXPECT_EQ(ui.sortOrder, library::SortOrder::AuthorDesc);
  ASSERT_EQ(ui.listCount(), 3);
  ui.activeNav().visibleRows = 4;
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 3);
  EXPECT_STREQ(ui.winItems[0].label, "Shelley, Mary");
  EXPECT_STREQ(ui.winItems[2].label, "Austen, Jane");
  ui.activateIndex(2);
  ASSERT_EQ(ui.listCount(), 2);
  ui.buildRows(screen);
  EXPECT_STREQ(ui.winItems[0].label, "Gamma");
  EXPECT_STREQ(ui.winItems[1].label, "Alpha");
}

TEST_F(LibraryUiTest, SwitchingToFlatTabClearsChildScope) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(2);
  ASSERT_EQ(ui.listCount(), 1);
  ui.selectTab(TITLE_TAB, false);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  EXPECT_TRUE(ui.groupTitle.empty());
  EXPECT_EQ(ui.listCount(), 4);
  EXPECT_EQ(ui.rowFor(0), 0);
  EXPECT_EQ(ui.rowFor(3), 3);
}

TEST_F(LibraryUiTest, CancelledBookMenuPreservesGroupScope) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(2);
  ui.openBookOptions(0);
  ASSERT_TRUE(ui.child);
  EXPECT_FALSE(ui.index.isOpen());
  ui.returnChild({true, {}});
  EXPECT_TRUE(ui.index.isOpen());
  EXPECT_EQ(ui.selectedGroup, 2);
  ASSERT_EQ(ui.listCount(), 1);
  std::string path, title;
  ASSERT_TRUE(ui.resolveBook(0, path, title));
  EXPECT_EQ(path, "/book2");
}

TEST_F(LibraryUiTest, FavoriteActionUsesSelectedGroupBookAndPreservesUnfilteredScope) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(2);
  ui.openBookOptions(0);
  ui.returnChild(menu(1));
  EXPECT_TRUE(library::states['2'].favorite);
  EXPECT_TRUE(library::states['1'].favorite);
  EXPECT_FALSE(library::states['4'].favorite);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 2);
  EXPECT_EQ(ui.listCount(), 1);
}

TEST_F(LibraryUiTest, DeleteFromNonFirstGroupUsesTheSelectedBooksPath) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(2);
  ui.openBookOptions(0);
  ui.returnChild(menu(5));
  ASSERT_TRUE(ui.child);
  EXPECT_EQ(ui.child->kind, Child::Confirmation);
  EXPECT_EQ(ui.child->title, "Beta");
  ui.returnChild({false, {}});
  ASSERT_EQ(Storage.removed.size(), 1);
  EXPECT_EQ(Storage.removed[0], "/book2");
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
}

TEST_F(LibraryUiTest, InvalidGroupActivationCannotOpenBookOrChangeScope) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activeNav().selected = 2;
  ui.activateIndex(-1);
  ui.activateIndex(ui.listCount());
  ui.onRowLongPress(ui.listCount());
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  EXPECT_EQ(ui.activeNav().selected, 2);
  EXPECT_TRUE(ui.selectedPath.empty());
  EXPECT_FALSE(ui.child);
}

TEST_F(LibraryUiTest, GroupRootAllocationFailureCannotExposeAllBooks) {
  for (const bool series : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    SETTINGS.libraryGroupBySeries = series;
    failNextAllocation = true;
    ui.selectTab(AUTHOR_TAB, false);
    EXPECT_TRUE(ui.filterFailed);
    EXPECT_EQ(ui.listCount(), 0);
    ui.activateIndex(0);
    EXPECT_TRUE(ui.selectedPath.empty());
    EXPECT_EQ(ui.selectedGroup, -1);
  }
}

TEST_F(LibraryUiTest, AuthorGroupReadFailuresDiscardPartialGroupMap) {
  for (int failure = 0; failure < 2; ++failure) {
    LibraryListActivity ui;
    populate(ui);
    if (failure == 0) ui.index.failRankRow = 2;
    if (failure == 1) ui.index.failOrdinal = 3;
    ui.selectTab(AUTHOR_TAB, false);
    EXPECT_TRUE(ui.filterFailed) << failure;
    EXPECT_EQ(ui.groupCount, 0) << failure;
    EXPECT_EQ(ui.listCount(), 0) << failure;
    ui.activateIndex(0);
    EXPECT_TRUE(ui.selectedPath.empty());
  }
}

TEST_F(LibraryUiTest, SeriesGroupReadFailuresDiscardPartialGroupMap) {
  for (int failure = 0; failure < 3; ++failure) {
    LibraryListActivity ui;
    populate(ui);
    SETTINGS.libraryGroupBySeries = true;
    if (failure == 0) ui.index.failRankRow = 2;
    if (failure == 1) ui.index.failSeriesRefOrdinal = 2;
    if (failure == 2) ui.index.failSeriesId = 1;
    ui.selectTab(AUTHOR_TAB, false);
    EXPECT_TRUE(ui.filterFailed) << failure;
    EXPECT_EQ(ui.groupCount, 0) << failure;
    EXPECT_EQ(ui.listCount(), 0) << failure;
    ui.activateIndex(0);
    EXPECT_TRUE(ui.selectedPath.empty());
  }
}

TEST_F(LibraryUiTest, EmptyAndFilteredOutAuthorRootsContainNoBookRows) {
  LibraryListActivity empty;
  empty.selectTab(AUTHOR_TAB, false);
  EXPECT_EQ(empty.listCount(), 0);
  EXPECT_FALSE(empty.filterFailed);
  LibraryListActivity ui;
  populate(ui);
  ui.query = "unmatched query";
  ui.selectTab(AUTHOR_TAB, false);
  EXPECT_EQ(ui.listCount(), 0);
  EXPECT_FALSE(ui.filterFailed);
  ui.activateIndex(0);
  EXPECT_EQ(ui.selectedGroup, -1);
  EXPECT_TRUE(ui.selectedPath.empty());
}

TEST_F(LibraryUiTest, UnknownAuthorBooksHaveOneReachableGroup) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.books[0].author.clear();
  ui.index.books[2].author.clear();
  ui.index.permutations[static_cast<size_t>(library::SortOrder::AuthorAsc)] = {3, 1, 0, 2};
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 3);
  ui.activateIndex(2);
  ASSERT_EQ(ui.listCount(), 2);
  std::string path, title;
  ASSERT_TRUE(ui.resolveBook(0, path, title));
  EXPECT_EQ(path, "/book1");
  ASSERT_TRUE(ui.resolveBook(1, path, title));
  EXPECT_EQ(path, "/book3");
}

TEST_F(LibraryUiTest, GroupMappingAndVisibleWindowStayBoundedAt4096Books) {
  LibraryListActivity ui;
  ui.index.books.resize(4096);
  for (size_t i = 0; i < ui.index.books.size(); ++i) {
    auto& book = ui.index.books[i];
    book.title = "Book " + std::to_string(i);
    book.author = "Author" + std::to_string(i);
    book.path = "/book" + std::to_string(i);
  }
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 4096);
  EXPECT_EQ(ui.groupCapacity, 4096);
  EXPECT_FALSE(ui.filtered);
  ui.activeNav().visibleRows = 7;
  ui.activeNav().top = 4089;
  UiScreen screen;
  ui.buildRows(screen);
  EXPECT_LE(ui.winItems.size(), 8);
  EXPECT_EQ(ui.winTitles.size(), 8);
  EXPECT_EQ(ui.winAuthors.size(), 8);
  ui.activateIndex(4095);
  ASSERT_EQ(ui.listCount(), 1);
  EXPECT_EQ(ui.rowFor(0), 4095);
  std::string path, title;
  ASSERT_TRUE(ui.resolveBook(0, path, title));
  EXPECT_EQ(path, "/book4095");
  EXPECT_FALSE(ui.resolveBook(1, path, title));
}

TEST_F(LibraryUiTest, AuthorRootAndDrillDownNeverReadHiddenBookTitles) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.failTitleOrdinal = 0;
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 3);
  ui.activeNav().visibleRows = 4;
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 3);
  EXPECT_STREQ(ui.winItems[0].label, "Austen, Jane");
  ui.activateIndex(0);
  EXPECT_EQ(ui.selectedGroup, 0);
  EXPECT_EQ(ui.listCount(), 2);
  EXPECT_EQ(ui.index.titleReadCalls, 0);
  EXPECT_EQ(ui.index.nameReadCalls, 0);
  EXPECT_FALSE(ui.index.ioFailed());
}

TEST_F(LibraryUiTest, RemovingLastFavoriteRebuildsFilteredGroupsWithoutStaleScope) {
  LibraryListActivity ui;
  populate(ui);
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.query = "gamma";
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 1);
  ui.activateIndex(0);
  ASSERT_EQ(ui.listCount(), 1);
  ui.openBookOptions(0);
  ui.returnChild(menu(1));
  EXPECT_FALSE(library::states['3'].favorite);
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  EXPECT_EQ(ui.listCount(), 0);
  EXPECT_TRUE(ui.groupTitle.empty());
  ui.activateIndex(0);
  EXPECT_TRUE(ui.selectedPath.empty());
}

TEST_F(LibraryUiTest, StateActionKeepsFilteredAuthorChildAndSelectedBook) {
  LibraryListActivity ui;
  populate(ui);
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(0);
  ui.activeNav().selected = 2;
  ui.activeNav().top = 1;
  ui.openBookOptions(1);
  ui.returnChild(menu(3));
  EXPECT_EQ(library::states['3'].reading, library::ReadingState::Reading);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.groupTitle, "Austen, Jane");
  ASSERT_EQ(ui.listCount(), 2);
  EXPECT_EQ(ui.activeNav().selected, 2);
  EXPECT_EQ(ui.activeNav().top, 1);
  ui.openSelectedBook();
  EXPECT_EQ(ui.selectedPath, "/book3");
}

TEST_F(LibraryUiTest, RemovedFavoriteSelectsNextOrPreviousBookInsideTheSameAuthor) {
  for (const int removed : {0, 1}) {
    LibraryListActivity ui;
    populate(ui);
    ui.shelfFilter = library::ShelfFilter::Favorites;
    ui.selectTab(AUTHOR_TAB, false);
    ui.activateIndex(0);
    ui.openBookOptions(removed);
    ui.returnChild(menu(1));
    EXPECT_FALSE(ui.groupsCollapsed);
    EXPECT_EQ(ui.groupTitle, "Austen, Jane");
    ASSERT_EQ(ui.listCount(), 1);
    EXPECT_EQ(ui.activeNav().selected, 1);
    ui.openSelectedBook();
    EXPECT_EQ(ui.selectedPath, removed == 0 ? "/book3" : "/book1");
  }
}

TEST_F(LibraryUiTest, RemovedLastGroupBookReturnsToAValidDirectoryRowAndBackStillWorks) {
  LibraryListActivity ui;
  populate(ui);
  library::states['2'].favorite = true;
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(1);
  ui.openBookOptions(0);
  ui.returnChild(menu(1));
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  ASSERT_EQ(ui.listCount(), 1);
  EXPECT_EQ(ui.activeNav().selected, 1);
  EXPECT_TRUE(ui.groupTitle.empty());
  ui.activateIndex(0);
  EXPECT_EQ(ui.groupTitle, "Austen, Jane");
  ASSERT_TRUE(ui.collapseGroups(0));
  EXPECT_EQ(ui.activeNav().selected, 1);
}

TEST_F(LibraryUiTest, FilteredSeriesActionKeepsIdentityWhenDisplayNamesCollide) {
  LibraryListActivity ui;
  populate(ui);
  SETTINGS.libraryGroupBySeries = true;
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 2);
  ui.activateIndex(1);
  ui.openBookOptions(0);
  ui.returnChild(menu(2));
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 1);
  ASSERT_EQ(ui.listCount(), 1);
  ui.openSelectedBook();
  EXPECT_EQ(ui.selectedPath, "/book3");
}

TEST_F(LibraryUiTest, RemovingOneSeriesNeverJumpsIntoAnotherWithTheSameDisplayName) {
  LibraryListActivity ui;
  populate(ui);
  SETTINGS.libraryGroupBySeries = true;
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(0);
  ui.openBookOptions(0);
  ui.returnChild(menu(1));
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  ASSERT_EQ(ui.listCount(), 1);
  EXPECT_EQ(ui.activeNav().selected, 1);
  EXPECT_TRUE(ui.selectedPath.empty());
}

TEST_F(LibraryUiTest, DescendingFilteredShelfKeepsNearestSurvivingBook) {
  LibraryListActivity ui;
  populate(ui);
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.sortOrder = library::SortOrder::TitleDesc;
  ui.applyFilter();
  ui.activeNav().selected = 1;
  ui.openBookOptions(0);
  ui.returnChild(menu(1));
  ASSERT_EQ(ui.listCount(), 1);
  EXPECT_EQ(ui.activeNav().selected, 1);
  ui.openSelectedBook();
  EXPECT_EQ(ui.selectedPath, "/book1");
}

TEST_F(LibraryUiTest, TextOnlySearchStateActionDoesNotScanOrReallocateFilterMap) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "jane";
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(0);
  ui.openBookOptions(1);
  const auto* matches = ui.filtered.get();
  const int reads = ui.index.readCalls;
  const unsigned stateReads = library::stateReadCalls;
  failNextAllocation = true;
  ui.returnChild(menu(1));
  EXPECT_TRUE(failNextAllocation);
  EXPECT_EQ(ui.filtered.get(), matches);
  EXPECT_EQ(ui.index.readCalls, reads);
  EXPECT_EQ(library::stateReadCalls, stateReads + 1);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 0);
  EXPECT_FALSE(ui.filterFailed);
}

TEST_F(LibraryUiTest, FailedStateWriteRetainsFilteredChildWithoutASecondScan) {
  LibraryListActivity ui;
  populate(ui);
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(0);
  ui.activeNav().selected = 2;
  ui.openBookOptions(1);
  const auto* matches = ui.filtered.get();
  const int reads = ui.index.readCalls;
  library::stateWriteOk = false;
  ui.returnChild(menu(1));
  EXPECT_TRUE(library::states['3'].favorite);
  EXPECT_EQ(GUI.popups, 1);
  EXPECT_EQ(ui.filtered.get(), matches);
  EXPECT_EQ(ui.index.readCalls, reads);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 0);
  EXPECT_EQ(ui.activeNav().selected, 2);
}

TEST_F(LibraryUiTest, StateRefilterAllocationOrReadFailureCannotRestoreStaleChild) {
  for (const bool failAllocation : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    ui.shelfFilter = library::ShelfFilter::Favorites;
    ui.selectTab(AUTHOR_TAB, false);
    ui.activateIndex(0);
    ui.openBookOptions(1);
    if (failAllocation)
      failNextAllocation = true;
    else
      ui.index.failOrdinal = 0;
    ui.returnChild(menu(2));
    EXPECT_TRUE(ui.filterFailed);
    EXPECT_TRUE(ui.groupsCollapsed);
    EXPECT_EQ(ui.selectedGroup, -1);
    EXPECT_TRUE(ui.groupTitle.empty());
    EXPECT_EQ(ui.listCount(), 0);
    EXPECT_EQ(ui.activeNav().selected, 0);
    ui.openSelectedBook();
    EXPECT_TRUE(ui.selectedPath.empty());
  }
}

TEST_F(LibraryUiTest, RestoringGroupContextAddsNoReadsBeyondTheRequiredStateFilter) {
  LibraryListActivity baseline;
  LibraryListActivity ui;
  populate(baseline);
  populate(ui);
  for (auto* current : {&baseline, &ui}) {
    current->shelfFilter = library::ShelfFilter::Favorites;
    current->selectTab(AUTHOR_TAB, false);
    current->activateIndex(0);
  }
  library::states['3'].reading = library::ReadingState::Reading;
  baseline.applyFilter();
  ui.refilterAfterBookChange(1);
  EXPECT_EQ(ui.index.readCalls, baseline.index.readCalls);
  EXPECT_EQ(ui.index.pathHashReadCalls, baseline.index.pathHashReadCalls);
  EXPECT_EQ(ui.index.authorReadCalls, baseline.index.authorReadCalls);
  EXPECT_EQ(ui.index.metadataReadCalls, baseline.index.metadataReadCalls);
  EXPECT_EQ(ui.index.seriesRefReadCalls, baseline.index.seriesRefReadCalls);
  EXPECT_EQ(ui.filteredCount, baseline.filteredCount);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.activeNav().selected, 2);
}

TEST_F(LibraryUiTest, StateRefilterRestoresLastSurvivorAtThe4096BookLimit) {
  LibraryListActivity ui;
  ui.activeTabIndex = AUTHOR_TAB;
  ui.sortOrder = library::SortOrder::AuthorAsc;
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.index.books.resize(4096);
  for (size_t i = 0; i < ui.index.books.size(); ++i) {
    auto& book = ui.index.books[i];
    book.title = "Book " + std::to_string(i);
    book.author = "One Author";
    book.path = "/book" + std::to_string(i);
    book.key = i;
    library::states[i].favorite = true;
  }
  ui.applyFilter();
  ui.activateIndex(0);
  library::states[4095].favorite = false;
  ui.refilterAfterBookChange(4095);
  EXPECT_FALSE(ui.filterFailed);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 0);
  EXPECT_EQ(ui.listCount(), 4095);
  EXPECT_EQ(ui.groupCapacity, 4096);
  EXPECT_EQ(ui.activeNav().selected, 4095);
  EXPECT_EQ(ui.rowFor(4094), 4094);
  EXPECT_EQ(ui.rowFor(4095), -1);
}

TEST_F(LibraryUiTest, DegradedSortCannotPublishFalseAuthorOrSeriesGroups) {
  for (const bool series : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    ui.degraded = true;
    SETTINGS.libraryGroupBySeries = series;
    ui.selectTab(AUTHOR_TAB, false);
    EXPECT_TRUE(ui.filterFailed);
    EXPECT_EQ(ui.groupCount, 0);
    EXPECT_EQ(ui.listCount(), 0);
    EXPECT_FALSE(ui.groupStarts);
  }
}

TEST_F(LibraryUiTest, LongPressOpensGroupBeforeOfferingBookActions) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.onRowLongPress(2);
  EXPECT_EQ(ui.selectedGroup, 2);
  EXPECT_FALSE(ui.child);
  ui.onRowLongPress(0);
  ASSERT_TRUE(ui.child);
  EXPECT_EQ(ui.child->title, "Beta");
}

TEST_F(LibraryUiTest, ClosedIndexCannotResolvePreviouslySelectedGroupRows) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(2);
  ui.index.close();
  EXPECT_EQ(ui.bookRowCount(), 0);
  EXPECT_EQ(ui.rowFor(0), -1);
  std::string path, title;
  EXPECT_FALSE(ui.resolveBook(0, path, title));
  ui.openSelectedBook();
  EXPECT_TRUE(ui.selectedPath.empty());
}

TEST_F(LibraryUiTest, SearchFromChildReturnsToMatchingNamesAtRoot) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(2);
  ui.openSearch();
  ASSERT_TRUE(ui.child);
  ui.returnChild({false, KeyboardResult{"jane"}});
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  EXPECT_TRUE(ui.groupTitle.empty());
  ASSERT_EQ(ui.listCount(), 1);
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 1);
  EXPECT_STREQ(ui.winItems[0].label, "Austen, Jane");
  ui.activateIndex(0);
  EXPECT_EQ(ui.listCount(), 2);
}

TEST_F(LibraryUiTest, GroupRenderReadFailureStopsWindowWithoutCompactingLaterRows) {
  for (const bool series : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    SETTINGS.libraryGroupBySeries = series;
    ui.selectTab(AUTHOR_TAB, false);
    ASSERT_EQ(ui.listCount(), 3);
    ui.activeNav().visibleRows = 4;
    if (series)
      ui.index.failSeriesId = 1;
    else
      ui.index.failAuthorOrdinal = 3;
    UiScreen screen;
    ui.buildRows(screen);
    ASSERT_EQ(ui.winItems.size(), 1);
    EXPECT_EQ(ui.winItems[0].actionValue, 0);
    EXPECT_EQ(screen.props.itemsWindowFirst, 0);
    EXPECT_EQ(screen.props.itemsWindowCount, 1);
  }
}

TEST_F(LibraryUiTest, ChildRenderReadFailureStopsWindowWithoutCompactingLaterRows) {
  for (const bool series : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    SETTINGS.libraryGroupBySeries = series;
    // Three book rows make a failure in the middle distinct from a short tail.
    ui.index.books.push_back({"Delta", "Jane Austen", "/book5", "Long series", 0, 300, '5'});
    ui.index.permutations[static_cast<size_t>(library::SortOrder::AuthorAsc)] = {0, 2, 4, 3, 1};
    ui.index.permutations[static_cast<size_t>(library::SortOrder::SeriesAsc)] = {0, 1, 4, 2, 3};
    ui.selectTab(AUTHOR_TAB, false);
    ui.activateIndex(0);
    ASSERT_EQ(ui.listCount(), 3);
    ui.activeNav().visibleRows = 4;
    if (series)
      ui.index.failSeriesRefOrdinal = 1;
    else
      ui.index.failOrdinal = 2;
    UiScreen screen;
    ui.buildRows(screen);
    ASSERT_EQ(ui.winItems.size(), 1);
    EXPECT_EQ(ui.winItems[0].actionValue, 0);
    EXPECT_EQ(screen.props.itemsWindowFirst, 0);
    EXPECT_EQ(screen.props.itemsWindowCount, 1);
  }
}

TEST_F(LibraryUiTest, FirstRowFailureNeverSubmitsEmptyOrStaleItemsToSdkList) {
  for (const bool series : {false, true}) {
    for (const bool child : {false, true}) {
      for (const bool previouslyDrawn : {false, true}) {
        SCOPED_TRACE(testing::Message() << "series=" << series << " child=" << child
                                        << " previouslyDrawn=" << previouslyDrawn);
        LibraryListActivity ui;
        populate(ui);
        SETTINGS.libraryGroupBySeries = series;
        ui.selectTab(AUTHOR_TAB, false);
        if (child) ui.activateIndex(0);
        ui.activeNav().visibleRows = 4;
        if (previouslyDrawn) {
          UiScreen previous;
          ui.buildRows(previous);
          ASSERT_GT(ui.winItems.size(), 0);
          ASSERT_EQ(previous.listCalls, 1);
        }
        if (series) {
          if (child)
            ui.index.failSeriesRefOrdinal = 0;
          else
            ui.index.failSeriesId = 0;
        } else {
          ui.index.failAuthorOrdinal = 0;
        }
        UiScreen failed;
        ui.buildRows(failed);
        EXPECT_TRUE(ui.winItems.empty());
        // A zero-sized window has special meaning to the SDK: do not submit it
        // with the nonzero logical count or stale reserved item storage.
        EXPECT_EQ(failed.listCalls, 0);
        EXPECT_EQ(failed.centeredTextCalls, 1);
        EXPECT_FALSE(failed.centeredMessage.empty());
      }
    }
  }
}

TEST_F(LibraryUiTest, AuthorDirectoryUsesIdentityWithoutReadingHiddenAuthorBlobs) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.failAuthorOrdinal = 3;
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 3);
  EXPECT_EQ(ui.index.authorReadCalls, 0);
  EXPECT_EQ(ui.index.titleReadCalls, 0);
  EXPECT_EQ(ui.index.nameReadCalls, 0);
  EXPECT_FALSE(ui.index.ioFailed());
}

TEST_F(LibraryUiTest, IdenticalTruncatedAuthorLabelsKeepSeparateBookGroups) {
  LibraryListActivity ui;
  const std::string label(128, 'x');
  ui.index.books = {{"Alpha", label, "/book1", ""}, {"Beta", label, "/book2", ""}, {"Gamma", label, "/book3", ""}};
  ui.index.books[0].sourceAuthor = label + " Smith";
  ui.index.books[1].sourceAuthor = label + " Smith";
  ui.index.books[2].sourceAuthor = label + " Tolkien";
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 2);
  ui.activateIndex(0);
  ASSERT_EQ(ui.listCount(), 2);
  std::string path, title;
  ASSERT_TRUE(ui.resolveBook(1, path, title));
  EXPECT_EQ(path, "/book2");
  EXPECT_FALSE(ui.resolveBook(2, path, title));
  ASSERT_TRUE(ui.collapseGroups(0));
  ui.activateIndex(1);
  ASSERT_EQ(ui.listCount(), 1);
  ASSERT_TRUE(ui.resolveBook(0, path, title));
  EXPECT_EQ(path, "/book3");
}

TEST_F(LibraryUiTest, InitialsOnlyAuthorsKeepSeparateLabels) {
  LibraryListActivity ui;
  ui.index.books = {{"Alpha", "A. B.", "/book1", ""},
                    {"Beta", "C. D.", "/book2", ""},
                    {"Gamma", "", "/book3", ""},
                    {"Delta", "", "/book4", ""}};
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.listCount(), 3);
}

TEST_F(LibraryUiTest, DirtyMarkerRefreshesAnOtherwiseWarmSession) {
  library::librarySession.reconciled(true, library::librarySession.refreshToken());
  library::dirtyIndex = true;
  LibraryListActivity ui;
  populate(ui);
  ui.onEnter();
  EXPECT_EQ(ui.rebuilds, 1);
}

TEST_F(LibraryUiTest, TouchRefreshReusesDispatchLockAndPreservesGroupDirectory) {
  LibraryListActivity ui;
  populate(ui);
  ui.activeTabIndex = AUTHOR_TAB;
  ui.sortOrder = library::SortOrder::AuthorAsc;
  ui.applyFilter();
  ui.expandGroup(0);
  const int before = locksTaken;
  {
    RenderLock dispatch(ui);
    LibraryListActivity::rebuildActionTrampoline({}, &ui);
    EXPECT_EQ(lockDepth, 1);
  }
  EXPECT_EQ(locksTaken, before + 1);
  EXPECT_EQ(ui.rebuilds, 1);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 0);
  EXPECT_EQ(ui.expandedNav.selected, 1);
}

TEST_F(LibraryUiTest, AuthorCountsUseExistingBoundariesWithoutAdditionalMetadataReads) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activeNav().visibleRows = 4;
  ui.index.readCalls = ui.index.authorReadCalls = ui.index.metadataReadCalls = 0;
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 3u);
  EXPECT_STREQ(ui.winItems[0].label, "Austen, Jane");
  EXPECT_STREQ(ui.winItems[0].value, "2");
  EXPECT_STREQ(ui.winItems[1].value, "1");
  EXPECT_STREQ(ui.winItems[2].value, "1");
  EXPECT_EQ(ui.index.readCalls, 3);
  EXPECT_EQ(ui.index.authorReadCalls, 3);
  EXPECT_EQ(ui.index.metadataReadCalls, 0);
  for (const auto& item : ui.winItems) EXPECT_EQ(item.subtitle, nullptr);
  ui.expandGroup(0);
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 2u);
  for (const auto& item : ui.winItems) EXPECT_EQ(item.value, nullptr);
}

TEST_F(LibraryUiTest, GroupCountsReflectActiveFilterInBothDirections) {
  for (bool series : {false, true}) {
    for (bool reverse : {false, true}) {
      SCOPED_TRACE(series);
      SCOPED_TRACE(reverse);
      LibraryListActivity ui;
      populate(ui);
      SETTINGS.libraryGroupBySeries = series;
      ui.shelfFilter = library::ShelfFilter::Favorites;
      ui.selectTab(AUTHOR_TAB, false);
      ui.activeNav().visibleRows = 4;
      if (reverse) ui.toggleSortDirection();
      UiScreen screen;
      ui.buildRows(screen);
      ASSERT_EQ(ui.winItems.size(), series ? 2u : 1u);
      for (const auto& item : ui.winItems) EXPECT_STREQ(item.value, series ? "1" : "2");
    }
  }
}

TEST_F(LibraryUiTest, All4096BooksCanShareOneCountWithoutExtraStorageOrReads) {
  LibraryListActivity ui;
  ui.index.books.reserve(4096);
  for (int i = 0; i < 4096; ++i) ui.index.books.push_back({"Book", "One Author", "/book" + std::to_string(i), ""});
  ui.selectTab(AUTHOR_TAB, false);
  ui.activeNav().visibleRows = 4;
  const int before = ui.index.readCalls;
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 1u);
  EXPECT_STREQ(ui.winItems[0].value, "4096");
  EXPECT_EQ(ui.index.readCalls - before, 1);
  EXPECT_LE(ui.winAuthors.size(), static_cast<size_t>(ui.activeNav().visibleRows + 1));
}

TEST_F(LibraryUiTest, IdenticalSeriesLabelsKeepIndependentCountsIncludingLastGroup) {
  LibraryListActivity ui;
  populate(ui);
  SETTINGS.libraryGroupBySeries = true;
  ui.selectTab(AUTHOR_TAB, false);
  ui.activeNav().visibleRows = 4;
  UiScreen screen;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 3u);
  EXPECT_STREQ(ui.winItems[0].label, ui.winItems[1].label);
  EXPECT_STREQ(ui.winItems[0].value, "2");
  EXPECT_STREQ(ui.winItems[1].value, "1");
  EXPECT_STREQ(ui.winItems[2].value, "1");
  ui.activeNav().top = 2;
  ui.activeNav().selected = 3;
  ui.activeNav().visibleRows = 1;
  ui.activeNav().followOnBuild = false;
  ui.buildRows(screen);
  ASSERT_EQ(ui.winItems.size(), 1u);
  EXPECT_STREQ(ui.winItems[0].value, "1");
}

TEST_F(LibraryUiTest, HeaderBackUsesTheSameGroupSearchAndHomeStagesAsPhysicalBack) {
  for (bool header : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    ui.query = "jane";
    ui.selectTab(AUTHOR_TAB, false);
    ui.activeNav().visibleRows = 4;
    ui.expandGroup(0);
    auto back = [&] {
      if (header) {
        RenderLock dispatch(ui);
        LibraryListActivity::backActionTrampoline({}, &ui);
      } else {
        ui.mappedInput.backReleased = true;
        EXPECT_TRUE(ui.handleButtons());
      }
    };
    back();
    EXPECT_TRUE(ui.groupsCollapsed);
    EXPECT_EQ(ui.selectedGroup, -1);
    EXPECT_EQ(ui.query, "jane");
    back();
    EXPECT_TRUE(ui.query.empty());
    EXPECT_EQ(ui.activeNav().selected, 0);
    EXPECT_EQ(ui.homes, 0);
    back();
    EXPECT_EQ(ui.homes, 1);
    EXPECT_EQ(lockDepth, 0);
  }
}

TEST_F(LibraryUiTest, DetailsAreLazyAndReturnToTheSameAuthorGroupAndSelection) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(0);
  ui.activeNav().selected = 2;
  ui.openBookOptions(1);
  ASSERT_EQ(ui.child->kind, Child::Menu);
  ASSERT_EQ(ui.child->labels.size(), 8);
  EXPECT_EQ(ui.child->labels[6], StrId::STR_LIBRARY_BOOK_DETAILS);
  EXPECT_EQ(ui.child->labels[7], StrId::STR_LIBRARY_GROUPS);
  ui.returnChild(menu(6));
  ASSERT_TRUE(ui.child);
  EXPECT_EQ(ui.child->kind, Child::Details);
  EXPECT_EQ(ui.child->title, "Gamma");
  EXPECT_EQ(ui.child->path, "/book3");
  EXPECT_FALSE(ui.childStartedWithIndexOpen);
  EXPECT_TRUE(ui.selectedPath.empty());
  ui.returnChild({});
  EXPECT_TRUE(ui.index.isOpen());
  EXPECT_EQ(ui.selectedGroup, 0);
  EXPECT_EQ(ui.activeNav().selected, 2);
  EXPECT_TRUE(ui.selectedPath.empty());
}
TEST_F(LibraryUiTest, DetailsOpenUsesResolvedPathAndReleasesIndex) {
  LibraryListActivity ui;
  populate(ui);
  ui.openBookOptions(2);
  ui.returnChild(menu(6));
  ASSERT_EQ(ui.child->kind, Child::Details);
  ui.returnChild(menu(0));
  EXPECT_EQ(ui.selectedPath, "/book3");
  EXPECT_FALSE(ui.index.isOpen());
}
TEST_F(LibraryUiTest, DetailsAllocationFailureKeepsTheLibraryOpen) {
  LibraryListActivity ui;
  populate(ui);
  ui.openBookOptions(0);
  failNextAllocation = true;
  ui.returnChild(menu(6));
  EXPECT_FALSE(ui.child);
  EXPECT_TRUE(ui.index.isOpen());
  EXPECT_GT(GUI.popups, 0);
}

TEST_F(LibraryUiTest, RecentDetailsUseResidentPathAndKeepRemoveActionAvailable) {
  LibraryListActivity ui;
  populate(ui);
  RECENT_BOOKS.books = {{"/recent.epub", "Recent title", "Author"}};
  ui.selectTab(RECENT_TAB, false);
  ui.openBookOptions(0);
  ASSERT_TRUE(ui.child);
  ASSERT_EQ(ui.child->labels.size(), 8);
  EXPECT_EQ(ui.child->labels[7], StrId::STR_REMOVE_FROM_RECENTS);
  ui.returnChild(menu(6));
  ASSERT_TRUE(ui.child);
  EXPECT_EQ(ui.child->kind, Child::Details);
  EXPECT_EQ(ui.child->path, "/recent.epub");
  EXPECT_EQ(ui.child->title, "Recent title");
  ui.returnChild({});
  EXPECT_TRUE(ui.selectedPath.empty());
}

TEST_F(LibraryUiTest, GroupActionStillCollapsesAfterDetailsWasAdded) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.activateIndex(0);
  ASSERT_EQ(ui.selectedGroup, 0);
  ui.openBookOptions(0);
  ui.returnChild(menu(7));
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
}

TEST_F(LibraryUiTest, RefreshFollowsExactBookAcrossOrdinalAndDescendingOrderChanges) {
  for (bool descending : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    ui.sortOrder = descending ? library::SortOrder::TitleDesc : library::SortOrder::TitleAsc;
    ui.applyFilter();
    ui.activeNav().reset(descending ? 3 : 2);  // Beta
    ui.rebuildHook = [](library::Index& index) {
      index.books.insert(index.books.begin(),
                         {"Aardvark", "New", "/new", "", library::CLIX_SERIES_NONE, library::SERIES_INDEX_NONE, '9'});
      index.permutations[static_cast<size_t>(library::SortOrder::TitleDesc)] = {4, 3, 2, 1, 0};
    };
    ui.refreshLibrary();
    EXPECT_EQ(ui.activeNav().selected, 3);
    EXPECT_EQ(ui.index.pathLookups, 1);
    EXPECT_TRUE(ui.selectedPath.empty());
    ui.openSelectedBook();
    EXPECT_EQ(ui.selectedPath, "/book2");
  }
}

TEST_F(LibraryUiTest, RefreshUsesNextBookOrPreviousAtTheEndWhenSelectedBookDisappears) {
  for (bool last : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    ui.applyFilter();
    ui.activeNav().reset(last ? 4 : 2);
    ui.rebuildHook = [last](library::Index& index) { index.books.erase(index.books.begin() + (last ? 3 : 1)); };
    ui.refreshLibrary();
    ui.openSelectedBook();
    EXPECT_EQ(ui.selectedPath, "/book3");
  }
}

TEST_F(LibraryUiTest, RefreshFollowsBookIntoItsRenamedAuthorGroup) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.expandGroup(0);
  ui.activeNav().reset(2);  // Gamma
  ui.rebuildHook = [](library::Index& index) {
    index.books[2].author = "Zoe Writer";
    index.permutations[static_cast<size_t>(library::SortOrder::AuthorAsc)] = {0, 3, 1, 2};
  };
  ui.refreshLibrary();
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 3);
  EXPECT_EQ(ui.expandedNav.selected, 4);
  EXPECT_EQ(ui.bookRowCount(), 1);
  EXPECT_EQ(ui.activeNav().selected, 1);
  EXPECT_NE(ui.groupTitle.find("Writer"), std::string::npos);
  ui.openSelectedBook();
  EXPECT_EQ(ui.selectedPath, "/book3");
}

TEST_F(LibraryUiTest, RefreshCollapsedAuthorAndSeriesViewsKeepTheirSelectedGroup) {
  for (bool series : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    SETTINGS.libraryGroupBySeries = series;
    ui.selectTab(AUTHOR_TAB, false);
    ui.activeNav().reset(2);
    const int oldRow = ui.groupStarts[1];
    const auto ordinal = ui.index.ordinalForRow(ui.sortOrder, oldRow);
    const auto path = ui.index.books[ordinal].path;
    ui.rebuildHook = [series](library::Index& index) {
      // Move the selected group to the front without changing a book's identity.
      index.permutations[static_cast<size_t>(series ? library::SortOrder::SeriesAsc : library::SortOrder::AuthorAsc)] =
          series ? std::vector<uint16_t>{2, 0, 1, 3} : std::vector<uint16_t>{3, 0, 2, 1};
    };
    ui.refreshLibrary();
    EXPECT_TRUE(ui.groupsCollapsed);
    EXPECT_EQ(ui.selectedGroup, -1);
    EXPECT_EQ(ui.activeNav().selected, 1);
    ui.expandGroup(0);
    ui.openSelectedBook();
    EXPECT_EQ(ui.selectedPath, path);
  }
}

TEST_F(LibraryUiTest, RefreshExpandedGroupKeepsTabFocusAndDoesNotOpenBooks) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.expandGroup(1);
  ui.activeNav().reset(0);
  ui.refreshLibrary();
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 1);
  EXPECT_EQ(ui.activeNav().selected, 0);
  EXPECT_TRUE(ui.selectedPath.empty());
}

TEST_F(LibraryUiTest, RefreshAtRootTabFocusDoesNotReadOrLookupSelectionPaths) {
  for (bool groups : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    if (groups)
      ui.selectTab(AUTHOR_TAB, false);
    else
      ui.applyFilter();
    ui.activeNav().reset(0);
    const auto hashes = ui.index.pathHashReadCalls;
    ui.refreshLibrary();
    EXPECT_EQ(ui.activeNav().selected, 0);
    EXPECT_EQ(ui.index.pathLookups, 0);
    EXPECT_EQ(ui.index.pathHashReadCalls, hashes);
  }
}

TEST_F(LibraryUiTest, RefreshKeepsRecentSelectionWithoutIndexIdentityReads) {
  LibraryListActivity ui;
  populate(ui);
  RECENT_BOOKS.books = {{"/a", "A", "A"}, {"/b", "B", "B"}};
  ui.selectTab(RECENT_TAB, false);
  ui.activeNav().reset(2);
  const auto hashes = ui.index.pathHashReadCalls;
  ui.refreshLibrary();
  EXPECT_EQ(ui.activeNav().selected, 2);
  EXPECT_EQ(ui.index.pathLookups, 0);
  EXPECT_EQ(ui.index.pathHashReadCalls, hashes);
  ui.openSelectedBook();
  EXPECT_EQ(ui.selectedPath, "/b");
}

TEST_F(LibraryUiTest, RefreshRetainsQueryAndShelfFilterAndSkipsNewlyExcludedSelection) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "jane";
  ui.shelfFilter = library::ShelfFilter::Favorites;
  ui.applyFilter();
  ui.activeNav().reset(1);
  ui.rebuildHook = [](library::Index&) { library::states['1'].favorite = false; };
  ui.refreshLibrary();
  EXPECT_EQ(ui.query, "jane");
  EXPECT_EQ(ui.shelfFilter, library::ShelfFilter::Favorites);
  EXPECT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.activeNav().selected, 1);
  ui.openSelectedBook();
  EXPECT_EQ(ui.selectedPath, "/book3");
}

TEST_F(LibraryUiTest, RefreshNeverCrossesAuthorGroupForItsNeighborFallback) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.expandGroup(0);
  ui.activeNav().reset(2);  // Last Austen book: fallback is first Austen book.
  ui.rebuildHook = [](library::Index& index) {
    index.books.erase(index.books.begin() + 2);
    index.permutations[static_cast<size_t>(library::SortOrder::AuthorAsc)] = {0, 2, 1};
  };
  ui.refreshLibrary();
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 0);
  ui.openSelectedBook();
  EXPECT_EQ(ui.selectedPath, "/book1");
}

TEST_F(LibraryUiTest, RefreshMissingGroupAnchorsFallsBackToValidDirectoryOrEmptyTabs) {
  for (bool empty : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    ui.selectTab(AUTHOR_TAB, false);
    ui.expandGroup(0);
    ui.rebuildHook = [empty](library::Index& index) {
      const auto survivor = index.books.back();
      index.books.clear();
      if (!empty) index.books.push_back(survivor);
      for (auto& permutation : index.permutations) permutation.clear();
    };
    ui.refreshLibrary();
    EXPECT_TRUE(ui.groupsCollapsed);
    EXPECT_EQ(ui.selectedGroup, -1);
    EXPECT_EQ(ui.activeNav().selected, empty ? 0 : 1);
    EXPECT_TRUE(ui.selectedPath.empty());
  }
}

TEST_F(LibraryUiTest, RefreshFailedRebuildKeepsOldSelectionAndRemainsRetryable) {
  LibraryListActivity ui;
  populate(ui);
  ui.applyFilter();
  ui.activeNav().reset(3);
  ui.rebuildOk = false;
  ui.refreshLibrary();
  EXPECT_TRUE(ui.refreshFailed);
  EXPECT_TRUE(library::librarySession.needsRefresh(true, false));
  EXPECT_EQ(ui.activeNav().selected, 3);
  ui.openSelectedBook();
  EXPECT_EQ(ui.selectedPath, "/book3");
}

TEST_F(LibraryUiTest, RefreshReadAndAllocationFailuresLeaveOnlyValidNavigation) {
  for (int fault = 0; fault < 5; ++fault) {
    LibraryListActivity ui;
    populate(ui);
    if (fault == 2) ui.query = "jane";
    ui.selectTab(AUTHOR_TAB, false);
    ui.expandGroup(0);
    ui.rebuildHook = [fault](library::Index& index) {
      if (fault == 0) index.pathLookupOk = false;
      if (fault == 1) index.openOk = false;
      if (fault == 2) failNextAllocation = true;
      if (fault == 3) index.failOrdinal = 0;
      if (fault == 4) index.degraded = true;
    };
    ui.refreshLibrary();
    EXPECT_TRUE(ui.groupsCollapsed) << fault;
    EXPECT_EQ(ui.selectedGroup, -1) << fault;
    EXPECT_GE(ui.activeNav().selected, 0);
    EXPECT_LE(ui.activeNav().selected, ui.listCount());
    EXPECT_TRUE(ui.selectedPath.empty());
    EXPECT_FALSE(failNextAllocation);
  }
}

TEST_F(LibraryUiTest, RefreshOptionalTitleGroupsPreserveSelectedLetter) {
  LibraryListActivity ui;
  populate(ui);
  ui.applyFilter();
  ASSERT_TRUE(ui.collapseGroups(2));
  ASSERT_EQ(ui.activeNav().selected, 3);
  ui.rebuildHook = [](library::Index& index) { index.books.erase(index.books.begin()); };
  ui.refreshLibrary();
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.activeNav().selected, 2);
  ui.expandGroup(1);
  ui.openSelectedBook();
  EXPECT_EQ(ui.selectedPath, "/book3");
}

TEST_F(LibraryUiTest, SearchPreservesLeadingWordsInsteadOfBroadeningTheQuery) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.books[0].title = "The Apple";
  ui.index.books[1].title = "Apple";
  ui.index.books[2].title = "I, Robot";
  ui.index.books[3].title = "Robot";
  ui.query = "the apple";
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.rowFor(0), 0);
  ui.query = "i robot";
  ui.applyFilter();
  ASSERT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.rowFor(0), 2);
  ui.query = "apple";
  ui.applyFilter();
  EXPECT_EQ(ui.filteredCount, 2);
}

TEST_F(LibraryUiTest, LargeSeriesGroupingReadsOneSharedEntryPerGroup) {
  for (bool descending : {false, true}) {
    for (bool filtered : {false, true}) {
      LibraryListActivity ui;
      ui.activeTabIndex = AUTHOR_TAB;
      ui.sortOrder = descending ? library::SortOrder::SeriesDesc : library::SortOrder::SeriesAsc;
      ui.index.books.reserve(4096);
      auto& order = ui.index.permutations[static_cast<size_t>(ui.sortOrder)];
      order.reserve(4096);
      for (uint16_t i = 0; i < 4096; ++i) {
        // Colliding labels must not merge distinct identities.
        ui.index.books.push_back({"Volume", "Author", "/book", "Same label", static_cast<uint16_t>(i / 64), i});
        order.push_back(descending ? 4095 - i : i);
      }
      if (filtered) {
        ui.query = "Volume";
        ui.filtered = std::make_unique<uint16_t[]>(2048);
        ui.filteredCount = 2048;
        for (uint16_t i = 0; i < 2048; ++i) ui.filtered[i] = 2 * i + 1;
      }
      ASSERT_TRUE(ui.buildGroupStarts());
      ASSERT_EQ(ui.groupCount, 64);
      for (uint16_t group = 0; group < 64; ++group) EXPECT_EQ(ui.groupStarts[group], group * (filtered ? 32 : 64));
      EXPECT_EQ(ui.index.seriesRefReadCalls, filtered ? 2048 : 4096);
      EXPECT_EQ(ui.index.seriesEntryReadCalls, 64);
      EXPECT_EQ(ui.index.readCalls, 0);
      RecordProperty(std::string(descending ? "descending" : "ascending") + (filtered ? "_filtered" : "_all") +
                         "_series_entry_reads",
                     ui.index.seriesEntryReadCalls);
    }
  }
}

TEST_F(LibraryUiTest, SeriesGroupingChecksEveryReferenceInsideAnExistingGroup) {
  for (bool failRank : {false, true}) {
    LibraryListActivity ui;
    populate(ui);
    ui.sortOrder = library::SortOrder::SeriesAsc;
    if (failRank)
      ui.index.failRankRow = 1;
    else
      ui.index.failSeriesRefOrdinal = 1;
    EXPECT_FALSE(ui.buildGroupStarts());
    EXPECT_EQ(ui.groupCount, 0);
    EXPECT_EQ(ui.index.seriesEntryReadCalls, 1);
    EXPECT_EQ(ui.index.seriesRefReadCalls, failRank ? 1 : 2);
  }
}

TEST_F(LibraryUiTest, SeriesGroupingRevalidatesSharedEntriesOnEveryRebuild) {
  LibraryListActivity ui;
  populate(ui);
  ui.sortOrder = library::SortOrder::SeriesAsc;
  ASSERT_TRUE(ui.buildGroupStarts());
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 2);
  ASSERT_TRUE(ui.buildGroupStarts());
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 4);
  ui.index.failSeriesId = 1;
  EXPECT_FALSE(ui.buildGroupStarts());
  EXPECT_EQ(ui.groupCount, 0);
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 6);
}

TEST_F(LibraryUiTest, SeriesGroupingStandaloneAndRepeatedIdentityRunsStayDistinct) {
  LibraryListActivity ui;
  populate(ui);
  ui.sortOrder = library::SortOrder::SeriesAsc;
  // Defensive handling of disjoint runs cannot reuse a previous entry validation.
  ui.index.permutations[static_cast<size_t>(ui.sortOrder)] = {0, 2, 1, 3};
  ASSERT_TRUE(ui.buildGroupStarts());
  EXPECT_EQ(ui.groupCount, 4);
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 3);
  EXPECT_EQ(ui.index.seriesRefReadCalls, 4);
  for (auto& book : ui.index.books) {
    book.seriesId = library::CLIX_SERIES_NONE;
    book.position = library::SERIES_INDEX_NONE;
  }
  ui.index.seriesEntryReadCalls = ui.index.seriesRefReadCalls = 0;
  ASSERT_TRUE(ui.buildGroupStarts());
  EXPECT_EQ(ui.groupCount, 1);
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 0);
  EXPECT_EQ(ui.index.seriesRefReadCalls, 4);
}

namespace {
std::string referenceAuthorHeading(const std::string& author) {
  std::string out = author.empty() ? std::string(tr(STR_LIBRARY_UNKNOWN_AUTHOR)) : author;
  if (author.empty()) return out;
  const auto lastSpace = out.find_last_of(' ');
  if (lastSpace != std::string::npos && lastSpace + 1 < out.size()) {
    out = out.substr(lastSpace + 1) + ", " + out.substr(0, lastSpace);
  }
  return out;
}
}  // namespace

TEST_F(LibraryUiTest, AuthorHeadingPreservesUpstreamText) {
  LibraryListActivity ui;
  std::string out;
  const std::string names[] = {"",
                               "Austen",
                               "Jane Austen",
                               "J. R. R. Tolkien",
                               " Jane Austen",
                               "Jane  Austen",
                               "Jane Austen ",
                               " ",
                               "  ",
                               "Jean-Paul Sartre",
                               "Gabriel García Márquez",
                               "王 小明",
                               "إبراهيم نصر الله",
                               std::string(126, 'a') + " " + std::string(128, 'b')};
  for (const auto& name : names) {
    ui.formatAuthorHeading(name, out);
    EXPECT_EQ(out, referenceAuthorHeading(name)) << name;
  }
}

TEST_F(LibraryUiTest, AuthorHeadingMatchesAllShortSpacingAndUtf8Combinations) {
  LibraryListActivity ui;
  std::string out;
  static constexpr const char* TOKENS[] = {"a", " ", "é", "王"};
  for (size_t length = 0, combinations = 1; length <= 5; ++length, combinations *= 4) {
    for (size_t code = 0; code < combinations; ++code) {
      std::string name;
      for (size_t n = 0, value = code; n < length; ++n, value /= 4) name += TOKENS[value % 4];
      ui.formatAuthorHeading(name, out);
      EXPECT_EQ(out, referenceAuthorHeading(name)) << name;
    }
  }
}

TEST_F(LibraryUiTest, AuthorHeadingAllowsAliasedInputAndOutput) {
  LibraryListActivity ui;
  const std::string names[] = {"",
                               "Single",
                               "Jane Austen",
                               " Jane  Austen",
                               "Jane Austen ",
                               "Gabriel García Márquez",
                               std::string(127, 'a') + " " + std::string(127, 'b')};
  for (auto name : names) {
    const auto expected = referenceAuthorHeading(name);
    ui.formatAuthorHeading(name, name);
    EXPECT_EQ(name, expected);
  }
}

TEST_F(LibraryUiTest, AuthorHeadingReusesWarmBufferWithoutAllocating) {
  LibraryListActivity ui;
  const std::string longName = std::string(126, 'a') + " " + std::string(128, 'b');
  const std::string shorterName = std::string(60, 'c') + " " + std::string(60, 'd');
  const auto expected = referenceAuthorHeading(longName);
  std::string out;
  ui.formatAuthorHeading(longName, out);
  const auto* buffer = out.data();
  heapcap::reset(SIZE_MAX);
  for (size_t i = 0; i < 1000; ++i) {
    ui.formatAuthorHeading(shorterName, out);
    ui.formatAuthorHeading(longName, out);
  }
  heapcap::stop();
  const auto allocations = heapcap::allocationCalls();
  EXPECT_EQ(out, expected);
  EXPECT_EQ(allocations, 0u);
  EXPECT_EQ(out.data(), buffer);
}

TEST_F(LibraryUiTest, AuthorHeadingColdBufferOnlyAllocatesOutputStorage) {
  LibraryListActivity ui;
  const std::string name = std::string(126, 'a') + " " + std::string(128, 'b');
  const auto expected = referenceAuthorHeading(name);
  std::string out;
  ASSERT_LT(out.capacity(), name.size());
  heapcap::reset(SIZE_MAX);
  ui.formatAuthorHeading(name, out);
  heapcap::stop();
  const auto allocations = heapcap::allocationCalls();
  EXPECT_EQ(out, expected);
  EXPECT_EQ(allocations, 1u);
}

TEST_F(LibraryUiTest, MalformedAuthorCannotMasqueradeAsUnknownAuthor) {
  LibraryListActivity ui;
  populate(ui);
  ui.index.malformedAuthorOrdinal = 0;
  std::string author = "stale";
  EXPECT_FALSE(ui.authorFor(0, author));
  EXPECT_TRUE(author.empty());
  EXPECT_FALSE(ui.index.ioFailed());
  EXPECT_EQ(ui.index.authorReadCalls, 1);
  ui.index.malformedAuthorOrdinal = -1;
  EXPECT_TRUE(ui.authorFor(0, author));
  EXPECT_EQ(author, "Jane Austen");
}

TEST_F(LibraryUiTest, MalformedAuthorHeadingDoesNotRenderUnknownGroup) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ASSERT_EQ(ui.groupCount, 3);
  ui.index.malformedAuthorOrdinal = 0;
  UiScreen screen;
  ui.buildRows(screen);
  EXPECT_TRUE(ui.winItems.empty());
  EXPECT_FALSE(ui.index.ioFailed());
  ui.index.malformedAuthorOrdinal = -1;
  ui.buildRows(screen);
  ASSERT_FALSE(ui.winItems.empty());
  EXPECT_STREQ(ui.winItems[0].label, "Austen, Jane");
}

TEST_F(LibraryUiTest, MalformedAuthorCannotChangeDrillDownScope) {
  LibraryListActivity ui;
  populate(ui);
  ui.selectTab(AUTHOR_TAB, false);
  ui.index.malformedAuthorOrdinal = 0;
  ui.activateIndex(0);
  EXPECT_TRUE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, -1);
  EXPECT_TRUE(ui.groupTitle.empty());
  ui.index.malformedAuthorOrdinal = -1;
  ui.activateIndex(0);
  EXPECT_FALSE(ui.groupsCollapsed);
  EXPECT_EQ(ui.selectedGroup, 0);
  EXPECT_EQ(ui.groupTitle, "Austen, Jane");
}

TEST_F(LibraryUiTest, MalformedAuthorSearchDiscardsPartialResultsAndRetries) {
  for (const char* query : {"jane", "long"}) {
    LibraryListActivity ui;
    populate(ui);
    ui.query = query;
    ui.index.malformedAuthorOrdinal = 2;
    ui.applyFilter();
    EXPECT_TRUE(ui.filterFailed) << query;
    EXPECT_FALSE(ui.filtered) << query;
    EXPECT_EQ(ui.filteredCount, 0) << query;
    EXPECT_EQ(ui.bookRowCount(), 0) << query;
    EXPECT_FALSE(ui.index.ioFailed());
    ui.index.malformedAuthorOrdinal = -1;
    ui.applyFilter();
    EXPECT_FALSE(ui.filterFailed);
    EXPECT_EQ(ui.filteredCount, query[0] == 'j' ? 2 : 3);
  }
}

TEST_F(LibraryUiTest, ValidEmptyAuthorAllowsSeriesSearchWithoutExtraReads) {
  LibraryListActivity ui;
  populate(ui);
  for (auto& book : ui.index.books) book.author.clear();
  ui.query = "long";
  ui.applyFilter();
  EXPECT_FALSE(ui.filterFailed);
  ASSERT_EQ(ui.filteredCount, 3);
  EXPECT_EQ(ui.index.authorReadCalls, 4);
  EXPECT_EQ(ui.index.seriesRefReadCalls, 4);
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 2);
  EXPECT_EQ(ui.index.titleReadCalls, 0);
}

TEST_F(LibraryUiTest, LargeSeriesSearchReadsOncePerRunAndPreservesEveryResult) {
  for (const bool descending : {false, true}) {
    LibraryListActivity ui;
    ui.activeTabIndex = TITLE_TAB;
    ui.sortOrder = descending ? library::SortOrder::SeriesDesc : library::SortOrder::SeriesAsc;
    ui.query = "needle";
    ui.index.books.reserve(4096);
    auto& order = ui.index.permutations[static_cast<size_t>(ui.sortOrder)];
    order.reserve(4096);
    for (uint16_t i = 0; i < 4096; ++i) {
      const uint16_t group = i / 64;
      ui.index.books.push_back({"Volume", "Writer", "/book", group % 2 ? "Other saga" : "Needle saga", group, i});
      order.push_back(descending ? 4095 - i : i);
    }
    ui.filterBooks();
    ASSERT_FALSE(ui.filterFailed);
    ASSERT_EQ(ui.filteredCount, 2048);
    size_t match = 0;
    for (uint16_t row = 0; row < 4096; ++row) {
      if ((order[row] / 64) % 2 == 0) EXPECT_EQ(ui.filtered[match++], row);
    }
    EXPECT_EQ(ui.index.readCalls, 4096);
    EXPECT_EQ(ui.index.authorReadCalls, 4096);
    EXPECT_EQ(ui.index.seriesRefReadCalls, 4096);
    EXPECT_EQ(ui.index.seriesEntryReadCalls, 64);
    std::printf("SERIES_SEARCH descending=%d books=4096 results=%u series_reads=%d\n", descending, ui.filteredCount,
                ui.index.seriesEntryReadCalls);
  }
}

TEST_F(LibraryUiTest, SeriesSearchMemoIsResetForEveryQueryAndShelfPredicateStillApplies) {
  LibraryListActivity ui;
  populate(ui);
  ui.query = "long";
  ui.filterBooks();
  ASSERT_EQ(ui.filteredCount, 3);
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 2);
  ui.query = "absent";
  ui.index.seriesEntryReadCalls = 0;
  ui.filterBooks();
  EXPECT_EQ(ui.filteredCount, 0);
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 2);
  ui.query = "long";
  ui.shelfFilter = library::ShelfFilter::Reading;
  ui.index.seriesEntryReadCalls = 0;
  ui.filterBooks();
  ASSERT_EQ(ui.filteredCount, 1);
  EXPECT_EQ(ui.filtered[0], 1);
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 2);
  EXPECT_EQ(library::stateReadCalls, 3u);
}

TEST_F(LibraryUiTest, SeriesSearchHandlesInterleavedRunsAndIndependentTitleAuthorMatches) {
  LibraryListActivity ui;
  ui.activeTabIndex = TITLE_TAB;
  ui.sortOrder = library::SortOrder::TitleAsc;
  ui.query = "needle";
  ui.index.books = {
      {"Alpha", "Writer", "/a", "Needle saga", 0},
      {"Needle title", "Writer", "/b", "Other saga", 1},
      {"Beta", "Needle author", "/c", "Other saga", 1},
      {"Gamma", "Writer", "/d", "Needle saga", 0},
      {"Delta", "Writer", "/e", "Other saga", 1},
      {"Epsilon", "Writer", "/f", "Other saga", 1},
      {"Zeta", "Writer", "/g", "", library::CLIX_SERIES_NONE},
      {"Eta", "Writer", "/h", "Needle saga", 0},
  };
  ui.filterBooks();
  ASSERT_FALSE(ui.filterFailed);
  ASSERT_EQ(ui.filteredCount, 5);
  const uint16_t expected[] = {0, 1, 2, 3, 7};
  for (size_t i = 0; i < 5; ++i) EXPECT_EQ(ui.filtered[i], expected[i]);
  EXPECT_EQ(ui.index.seriesRefReadCalls, 6);
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 3);
}

TEST_F(LibraryUiTest, SearchFoldReusesStorageAcrossLongFields) {
  const std::string longest = std::string(249, 'A') + " Café";
  const std::string shorter = std::string(129, 'B') + " Å";
  ASSERT_EQ(longest.size(), 255u);
  heapcap::reset(SIZE_MAX);
  for (size_t i = 0; i < 2000; ++i) {
    const auto folded = library::fold(i % 2 ? shorter : longest);
    heapcap::observeByte(folded.data());
  }
  heapcap::stop();
  const auto returnedAllocations = heapcap::allocationCalls();
  std::string output;
  heapcap::reset(SIZE_MAX);
  for (size_t i = 0; i < 2000; ++i) {
    library::foldInto(i % 2 ? shorter : longest, output);
    heapcap::observeByte(output.data());
  }
  heapcap::stop();
  const auto reusedAllocations = heapcap::allocationCalls();
  EXPECT_EQ(returnedAllocations, 2000u);
  EXPECT_EQ(reusedAllocations, 1u);
  EXPECT_EQ(output, std::string(129, 'b') + " a");
  std::printf("SEARCH_FOLD fields=2000 returned_allocations=%zu reused_allocations=%zu\n", returnedAllocations,
              reusedAllocations);
}

TEST_F(LibraryUiTest, ShortSearchFoldStaysAllocationFree) {
  std::string output;
  heapcap::reset(SIZE_MAX);
  for (size_t i = 0; i < 2000; ++i) {
    library::foldInto(i % 2 ? "" : "Café", output);
    heapcap::observeByte(output.data());
  }
  heapcap::stop();
  EXPECT_EQ(heapcap::allocationCalls(), 0u);
  EXPECT_TRUE(output.empty());
}

TEST_F(LibraryUiTest, SearchReusesFoldBufferAcrossTitleAuthorSeriesAndFilename) {
  LibraryListActivity ui;
  ui.activeTabIndex = TITLE_TAB;
  ui.sortOrder = library::SortOrder::TitleAsc;
  ui.query = "café";
  ui.index.books = {
      {std::string(200, 'x') + " Café", "", "/a", "", library::CLIX_SERIES_NONE},
      {"Short", std::string(100, 'y') + " CAFÉ", "/b", "", library::CLIX_SERIES_NONE},
      {"Plain", "", "/c", std::string(30, 'z') + " Café", 0},
      {"No match", "", "/d", "", library::CLIX_SERIES_NONE},
      {"CAFÉ direct", "", "/e", "", library::CLIX_SERIES_NONE},
      {"", "", "/f", "", library::CLIX_SERIES_NONE},
  };
  ui.index.books.back().hasTitleMetadata = false;
  ui.index.books.back().fileName = std::string(128, 'x') + " Café.epub";
  ui.filterBooks();
  ASSERT_FALSE(ui.filterFailed);
  ASSERT_EQ(ui.filteredCount, 5);
  const uint16_t expected[] = {0, 1, 2, 4, 5};
  for (size_t i = 0; i < 5; ++i) EXPECT_EQ(ui.filtered[i], expected[i]);
  EXPECT_EQ(ui.index.readCalls, 6);
  EXPECT_EQ(ui.index.titleReadCalls, 2);
  EXPECT_EQ(ui.index.nameReadCalls, 1);
  EXPECT_EQ(ui.index.authorReadCalls, 3);
  EXPECT_EQ(ui.index.seriesRefReadCalls, 2);
  EXPECT_EQ(ui.index.seriesEntryReadCalls, 1);
}
