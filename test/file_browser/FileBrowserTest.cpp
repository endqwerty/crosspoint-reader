#include <gtest/gtest.h>

#include <cstdio>
#include <functional>

#include "ClippingStore.h"
#include "FileBrowserFixture.h"

class FileBrowserTest : public testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    SETTINGS = {};
    RECENT_BOOKS = {};
    APP_STATE = {};
    library::dirtyCalls = 0;
    lockDepth = 0;
    UITheme::getInstance().icons = false;
  }
  static void drain(FileBrowserActivity& ui) {
    for (unsigned i = 0; i < 1000 && ui.searching; ++i) ui.advanceSearch();
    ASSERT_FALSE(ui.searching);
  }
  static std::string bytes(const std::string& path) {
    const auto found = fake::files.find(path);
    if (found == fake::files.end()) return {};
    return {found->second->bytes.begin(), found->second->bytes.end()};
  }
  static void expectState(const std::string& path, bool favorite, library::ReadingState reading) {
    library::BookState state;
    ASSERT_TRUE(library::readBookState(library::bookStateKey(path), state));
    EXPECT_EQ(state.favorite, favorite);
    EXPECT_EQ(state.reading, reading);
  }
  static std::string statePath(const std::string& path) {
    char result[80];
    snprintf(result, sizeof(result), "/.crosspoint/library-state/%016llx.bin",
             static_cast<unsigned long long>(library::bookStateKey(path)));
    return result;
  }
};

TEST_F(FileBrowserTest, LazyRowsKeepSearchSentinelAndFirmwareIndices) {
  FileBrowserActivity ui;
  ui.files = {"nested/Cafe\xcc\x81.epub", "Folder/"};
  ui.searchQuery = "cafe";
  fui::ListItem item;
  FileBrowserActivity::provideRow(&ui, 0, item);
  EXPECT_STREQ(item.subtitle, "cafe");
  EXPECT_EQ(item.actionValue, 0);
  FileBrowserActivity::provideRow(&ui, 1, item);
  EXPECT_STREQ(item.label, "nested/Caf\xc3\xa9");
  EXPECT_STREQ(item.value, ".epub");
  EXPECT_EQ(item.actionValue, 1);
  EXPECT_EQ(ui.files[0], "nested/Cafe\xcc\x81.epub");
  FileBrowserActivity::provideRow(&ui, 2, item);
  EXPECT_STREQ(item.label, "[Folder]");
  EXPECT_EQ(item.value, nullptr);
  FileBrowserActivity::provideRow(&ui, 3, item);
  EXPECT_EQ(item.label, nullptr);
  ui.mode = FileBrowserActivity::Mode::PickFirmware;
  ui.files = {"firmware.bin"};
  FileBrowserActivity::provideRow(&ui, 0, item);
  EXPECT_STREQ(item.label, "firmware");
  EXPECT_EQ(item.actionValue, 0);
}

TEST_F(FileBrowserTest, PrewarmIsBoundedAndIncludesSearchAndPath) {
  FileBrowserActivity ui;
  ui.basepath = "/Books";
  ui.searchQuery = "query";
  for (unsigned i = 0; i < 1000; ++i) ui.files.push_back("Book" + std::to_string(i) + ".epub");
  ui.prewarmRowGlyphs(0);
  EXPECT_EQ(ui.renderer.prewarmed.size(), 25u);
  EXPECT_EQ(ui.renderer.prewarmed.front(), "query");
  EXPECT_EQ(ui.renderer.prewarmed.back(), "/Books");
  ui.prewarmRowGlyphs(0);
  EXPECT_EQ(ui.renderer.calls, 1u);
  ui.prewarmRowGlyphs(999);
  EXPECT_EQ(ui.renderer.prewarmed[23], "Book999");
  EXPECT_EQ(ui.renderer.calls, 2u);
}

TEST_F(FileBrowserTest, RecursiveSearchPublishesLazyRowsAndOpensRawNestedPath) {
  fake::add("/Books/nested/Cafe\xcc\x81.epub");
  fake::add("/Books/Other.epub");
  FileBrowserActivity ui;
  ui.basepath = "/Books";
  ui.searchQuery = "cafe";
  ui.foldedQuery = library::fold(ui.searchQuery);
  ui.loadFiles();
  ASSERT_TRUE(ui.searching);
  EXPECT_EQ(ui.listCount(), 1);
  drain(ui);
  ASSERT_EQ(ui.files.size(), 1u);
  EXPECT_EQ(ui.findEntry(ui.files.front()), 1u);
  EXPECT_EQ(ui.nav.selected, 1);
  ui.activateSelected();
  EXPECT_EQ(ui.selectedBook, "/Books/nested/Cafe\xcc\x81.epub");
  ui.clearSearch();
  EXPECT_FALSE(ui.folderSearch.active());
  EXPECT_TRUE(ui.searchQuery.empty());
  EXPECT_EQ(ui.nav.selected, 0);
}

TEST_F(FileBrowserTest, SearchSentinelDoesNotOfferRenameOrDelete) {
  fake::add("/book.epub");
  FileBrowserActivity ui;
  ui.files = {"book.epub"};
  ui.nav.selected = 0;
  ui.showEntryActions();
  EXPECT_FALSE(ui.optionPopup.active);
  ui.startRename();
  EXPECT_EQ(ui.child, nullptr);
  ui.deleteSelected();
  EXPECT_EQ(ui.child, nullptr);
  EXPECT_TRUE(Storage.exists("/book.epub"));
  ui.activateSelected();
  EXPECT_NE(ui.child, nullptr);
}

TEST_F(FileBrowserTest, OpeningHoldReleaseDoesNotActivatePopupDefault) {
  FileBrowserActivity ui;
  ui.files = {"book.epub"};
  ui.nav.selected = 1;
  ui.mappedInput.confirmPressed = true;
  ui.mappedInput.confirmLongPressed = true;
  ASSERT_TRUE(ui.handleButtons());
  ASSERT_TRUE(ui.optionPopup.active);
  ui.mappedInput.confirmPressed = false;
  ui.mappedInput.confirmReleased = true;
  ASSERT_TRUE(ui.handleCustomInput());
  EXPECT_TRUE(ui.optionPopup.active);
  EXPECT_TRUE(ui.selectedBook.empty());
}

TEST_F(FileBrowserTest, RenameKeyboardUsesBasenameForNestedSearchResult) {
  FileBrowserActivity ui;
  ui.basepath = "/Books";
  ui.files = {"nested/Cafe\xcc\x81.epub"};
  ui.nav.selected = 1;
  ui.startRename();
  ASSERT_NE(ui.child, nullptr);
  EXPECT_EQ(ui.child->initial, "Caf\xc3\xa9");
}

TEST_F(FileBrowserTest, RenamePreservesBookmarkRecoveryAndReadingState) {
  const std::string oldPath = "/Books/nested/Old.epub", newPath = "/Books/nested/New.epub";
  fake::add(oldPath);
  const auto oldBookmarks = BookmarkUtil::getBookmarkPath(oldPath);
  const auto newBookmarks = BookmarkUtil::getBookmarkPath(newPath);
  fake::add(oldBookmarks, "uncommitted-primary");
  fake::add(oldBookmarks + ".bak", "committed-backup");
  fake::add(oldBookmarks + ".new", "staged");
  const auto oldCache = getBookCachePath(oldPath), newCache = getBookCachePath(newPath);
  fake::add(oldCache, "cache");
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
  APP_STATE.openEpubPath = oldPath;
  FileBrowserActivity ui;
  ui.basepath = "/Books";
  ui.searchQuery = "old";
  ui.foldedQuery = "old";
  ui.renameSelectedFile(oldPath, "nested/Old.epub", "New", ".epub");
  EXPECT_FALSE(Storage.exists(oldPath.c_str()));
  EXPECT_TRUE(Storage.exists(newPath.c_str()));
  EXPECT_EQ(bytes(newBookmarks), "uncommitted-primary");
  EXPECT_EQ(bytes(newBookmarks + ".bak"), "committed-backup");
  EXPECT_EQ(bytes(newBookmarks + ".new"), "staged");
  EXPECT_EQ(bytes(newCache), "cache");
  expectState(newPath, true, library::ReadingState::Finished);
  EXPECT_EQ(RECENT_BOOKS.newPath, newPath);
  EXPECT_EQ(APP_STATE.openEpubPath, newPath);
  EXPECT_EQ(library::dirtyCalls, 1u);
  EXPECT_TRUE(ui.searching);
  EXPECT_EQ(ui.nav.selected, 0);
  drain(ui);
  EXPECT_TRUE(ui.files.empty());
}

TEST_F(FileBrowserTest, BackupOnlyBookmarksSurviveRename) {
  fake::add("/Old.epub");
  const auto oldBackup = BookmarkUtil::getBookmarkPath("/Old.epub") + ".bak";
  const auto newBackup = BookmarkUtil::getBookmarkPath("/New.epub") + ".bak";
  fake::add(oldBackup, "committed");
  FileBrowserActivity ui;
  ui.renameSelectedFile("/Old.epub", "Old.epub", "New", ".epub");
  EXPECT_EQ(bytes(newBackup), "committed");
  EXPECT_FALSE(Storage.exists(oldBackup.c_str()));
}

TEST_F(FileBrowserTest, RenameFailureRollsBackAllMovedFilesAndTargetState) {
  const std::string oldPath = "/Old.epub", newPath = "/New.epub";
  fake::add(oldPath);
  fake::add(oldPath + ".key", "key");
  fake::add(oldPath + ".rights", "rights");
  const auto oldBookmarks = BookmarkUtil::getBookmarkPath(oldPath);
  const auto newBookmarks = BookmarkUtil::getBookmarkPath(newPath);
  fake::add(oldBookmarks, "primary");
  fake::add(oldBookmarks + ".bak", "backup");
  fake::add(oldBookmarks + ".new", "stage");
  fake::add(getBookCachePath(oldPath), "cache");
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(newPath), {false, library::ReadingState::Reading}));
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(newPath), {}));
  fake::blockedRenames.push_back({oldPath, newPath});
  FileBrowserActivity ui;
  ui.renameSelectedFile(oldPath, "Old.epub", "New", ".epub");
  ASSERT_TRUE(fake::failureTriggered);
  EXPECT_TRUE(Storage.exists(oldPath.c_str()));
  EXPECT_FALSE(Storage.exists(newPath.c_str()));
  EXPECT_EQ(bytes(oldBookmarks), "primary");
  EXPECT_EQ(bytes(oldBookmarks + ".bak"), "backup");
  EXPECT_EQ(bytes(oldBookmarks + ".new"), "stage");
  EXPECT_FALSE(Storage.exists(newBookmarks.c_str()));
  EXPECT_FALSE(Storage.exists((newBookmarks + ".bak").c_str()));
  EXPECT_FALSE(Storage.exists((newBookmarks + ".new").c_str()));
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  EXPECT_FALSE(Storage.exists(getBookCachePath(newPath).c_str()));
  EXPECT_EQ(bytes(oldPath + ".key"), "key");
  EXPECT_EQ(bytes(oldPath + ".rights"), "rights");
  EXPECT_FALSE(Storage.exists((newPath + ".key").c_str()));
  EXPECT_FALSE(Storage.exists((newPath + ".rights").c_str()));
  expectState(oldPath, true, library::ReadingState::Finished);
  expectState(newPath, false, library::ReadingState::Unread);
  EXPECT_EQ(RECENT_BOOKS.changes, 0u);
  EXPECT_EQ(library::dirtyCalls, 0u);
}

TEST_F(FileBrowserTest, RenameRejectsStaleTargetBackupAndPreservesSource) {
  fake::add("/Old.epub");
  const auto backup = BookmarkUtil::getBookmarkPath("/New.epub") + ".bak";
  fake::add(backup, "existing");
  FileBrowserActivity ui;
  ui.renameSelectedFile("/Old.epub", "Old.epub", "New", ".epub");
  EXPECT_TRUE(Storage.exists("/Old.epub"));
  EXPECT_FALSE(Storage.exists("/New.epub"));
  EXPECT_EQ(bytes(backup), "existing");
}

TEST_F(FileBrowserTest, RenameRejectsAllocationOrStateWriteFailureBeforeBookMutation) {
  fake::add("/Old.epub");
  ASSERT_TRUE(library::writeBookState(library::bookStateKey("/Old.epub"), {true, library::ReadingState::Reading}));
  FileBrowserActivity ui;
  fake::failAlloc = 0;
  ui.renameSelectedFile("/Old.epub", "Old.epub", "New", ".epub");
  EXPECT_TRUE(Storage.exists("/Old.epub"));
  EXPECT_FALSE(Storage.exists("/New.epub"));
  fake::failWrite = 0;
  ui.renameSelectedFile("/Old.epub", "Old.epub", "New", ".epub");
  EXPECT_TRUE(Storage.exists("/Old.epub"));
  EXPECT_FALSE(Storage.exists("/New.epub"));
  EXPECT_EQ(library::dirtyCalls, 0u);
}

TEST_F(FileBrowserTest, MoveIntoFolderCarriesBookmarksCacheAndReadingState) {
  // The reader's finished-book move to /read uses the same helper as rename.
  const std::string oldPath = "/Books/Done.epub", newPath = "/read/Done.epub";
  fake::add(oldPath);
  fake::add(oldPath + ".key", "key");
  fake::add(oldPath + ".rights", "rights");
  fake::add(BookmarkUtil::getBookmarkPath(oldPath), "marks");
  fake::add(getBookCachePath(oldPath), "cache");
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
  ASSERT_TRUE(isBookPathFree(newPath));
  ASSERT_TRUE(moveBookWithState(oldPath, newPath));
  EXPECT_FALSE(Storage.exists(oldPath.c_str()));
  EXPECT_TRUE(Storage.exists(newPath.c_str()));
  EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(newPath)), "marks");
  EXPECT_EQ(bytes(getBookCachePath(newPath)), "cache");
  EXPECT_EQ(bytes(newPath + ".key"), "key");
  EXPECT_EQ(bytes(newPath + ".rights"), "rights");
  EXPECT_FALSE(Storage.exists((oldPath + ".key").c_str()));
  EXPECT_FALSE(Storage.exists((oldPath + ".rights").c_str()));
  expectState(newPath, true, library::ReadingState::Finished);
  EXPECT_FALSE(isBookPathFree(newPath));
  // A book added later at the old path starts without the moved book's marks.
  expectState(oldPath, false, library::ReadingState::Unread);
}

// Where the clipping store keeps a book's data; the hash of the book path names the file.
static std::string clippingFile(const std::string& bookPath, const char* suffix = "") {
  return "/.crosspoint/clippings/epub_" + std::to_string(std::hash<std::string>{}(bookPath)) + ".bin" + suffix;
}

TEST_F(FileBrowserTest, BookPathIsTakenByLeftoverClippings) {
  const std::string path = "/read/Done.epub";
  EXPECT_TRUE(isBookPathFree(path));
  fake::add(clippingFile(path, ".deleted"), "ids");
  EXPECT_FALSE(isBookPathFree(path));
}

TEST_F(FileBrowserTest, BookPathIsTakenByLeftoverStateWithoutTheBook) {
  const std::string path = "/read/Done.epub";
  EXPECT_TRUE(isBookPathFree(path));
  fake::add(getBookCachePath(path), "stale");
  EXPECT_FALSE(isBookPathFree(path));
  fake::reset();
  fake::add(BookmarkUtil::getBookmarkPath(path) + ".new", "stale");
  EXPECT_FALSE(isBookPathFree(path));
  fake::reset();
  // Only reflowable books keep bookmark files, so an XTC path ignores them.
  fake::add(BookmarkUtil::getBookmarkPath("/read/Done.xtc"), "unrelated");
  EXPECT_TRUE(isBookPathFree("/read/Done.xtc"));
}

TEST_F(FileBrowserTest, ExistingDestinationBookRejectsMoveBeforeStateWrites) {
  const std::string oldPath = "/Old.epub", newPath = "/New.epub";
  fake::add(oldPath, "source book");
  fake::add(newPath, "destination book");
  fake::add(oldPath + ".key", "key");
  fake::add(oldPath + ".rights", "rights");
  fake::add(getBookCachePath(oldPath), "cache");
  fake::add(BookmarkUtil::getBookmarkPath(oldPath), "marks");
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
  const auto writes = fake::writesByPath;
  EXPECT_FALSE(moveBookWithState(oldPath, newPath));
  EXPECT_EQ(bytes(oldPath), "source book");
  EXPECT_EQ(bytes(newPath), "destination book");
  EXPECT_EQ(bytes(oldPath + ".key"), "key");
  EXPECT_EQ(bytes(oldPath + ".rights"), "rights");
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(oldPath)), "marks");
  EXPECT_FALSE(Storage.exists((newPath + ".key").c_str()));
  EXPECT_FALSE(Storage.exists((newPath + ".rights").c_str()));
  EXPECT_FALSE(Storage.exists(getBookCachePath(newPath).c_str()));
  EXPECT_FALSE(Storage.exists(BookmarkUtil::getBookmarkPath(newPath).c_str()));
  EXPECT_EQ(fake::writesByPath, writes);
  expectState(oldPath, true, library::ReadingState::Finished);
  expectState(newPath, false, library::ReadingState::Unread);
}

TEST_F(FileBrowserTest, MissingSourceBookLeavesOrphanedStateAndSidecarsUntouched) {
  const std::string oldPath = "/Old.epub", newPath = "/New.epub";
  fake::add(oldPath + ".key", "key");
  fake::add(oldPath + ".rights", "rights");
  fake::add(getBookCachePath(oldPath), "cache");
  fake::add(BookmarkUtil::getBookmarkPath(oldPath), "marks");
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
  const auto writes = fake::writesByPath;
  EXPECT_FALSE(moveBookWithState(oldPath, newPath));
  EXPECT_EQ(bytes(oldPath + ".key"), "key");
  EXPECT_EQ(bytes(oldPath + ".rights"), "rights");
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(oldPath)), "marks");
  EXPECT_FALSE(Storage.exists(oldPath.c_str()));
  EXPECT_FALSE(Storage.exists(newPath.c_str()));
  EXPECT_FALSE(Storage.exists((newPath + ".key").c_str()));
  EXPECT_FALSE(Storage.exists((newPath + ".rights").c_str()));
  EXPECT_FALSE(Storage.exists(getBookCachePath(newPath).c_str()));
  EXPECT_FALSE(Storage.exists(BookmarkUtil::getBookmarkPath(newPath).c_str()));
  EXPECT_EQ(fake::writesByPath, writes);
  expectState(oldPath, true, library::ReadingState::Finished);
  expectState(newPath, false, library::ReadingState::Unread);
}

TEST_F(FileBrowserTest, DestinationProtectionSidecarsRejectMovesBeforeStateWrites) {
  const std::string oldPath = "/Old.epub", newPath = "/New.epub";
  for (const char* suffix : {".key", ".rights"}) {
    SCOPED_TRACE(suffix);
    fake::reset();
    fake::add(oldPath, "book");
    fake::add(oldPath + ".key", "old key");
    fake::add(oldPath + ".rights", "old rights");
    fake::add(newPath + suffix, "destination sidecar");
    ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
    const auto writes = fake::writesByPath;
    EXPECT_FALSE(isBookPathFree(newPath));
    EXPECT_FALSE(moveBookWithState(oldPath, newPath));
    EXPECT_EQ(bytes(oldPath), "book");
    EXPECT_EQ(bytes(oldPath + ".key"), "old key");
    EXPECT_EQ(bytes(oldPath + ".rights"), "old rights");
    EXPECT_EQ(bytes(newPath + suffix), "destination sidecar");
    EXPECT_FALSE(Storage.exists(newPath.c_str()));
    EXPECT_EQ(fake::writesByPath, writes);
    expectState(oldPath, true, library::ReadingState::Finished);
    expectState(newPath, false, library::ReadingState::Unread);
  }
}

TEST_F(FileBrowserTest, SidecarFailureRollsBackWithoutDependingOnOldStateRemoval) {
  const std::string oldPath = "/Old.epub", newPath = "/New.epub";
  for (const char* suffix : {".key", ".rights"}) {
    for (const bool failRemove : {false, true}) {
      SCOPED_TRACE(suffix);
      SCOPED_TRACE(failRemove);
      fake::reset();
      fake::add(oldPath, "book");
      fake::add(oldPath + ".key", "key");
      fake::add(oldPath + ".rights", "rights");
      fake::add(getBookCachePath(oldPath), "cache");
      fake::add(BookmarkUtil::getBookmarkPath(oldPath), "marks");
      ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
      const auto originalState = bytes(statePath(oldPath));
      fake::blockedRenames.push_back({oldPath + suffix, newPath + suffix});
      fake::failRemove = failRemove ? 0 : -1;
      EXPECT_FALSE(moveBookWithState(oldPath, newPath));
      ASSERT_TRUE(fake::failureTriggered);
      EXPECT_EQ(bytes(oldPath), "book");
      EXPECT_EQ(bytes(oldPath + ".key"), "key");
      EXPECT_EQ(bytes(oldPath + ".rights"), "rights");
      EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
      EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(oldPath)), "marks");
      EXPECT_EQ(bytes(statePath(oldPath)), originalState);
      EXPECT_FALSE(Storage.exists(newPath.c_str()));
      EXPECT_FALSE(Storage.exists((newPath + ".key").c_str()));
      EXPECT_FALSE(Storage.exists((newPath + ".rights").c_str()));
      EXPECT_FALSE(Storage.exists(getBookCachePath(newPath).c_str()));
      EXPECT_FALSE(Storage.exists(BookmarkUtil::getBookmarkPath(newPath).c_str()));
      expectState(newPath, false, library::ReadingState::Unread);
    }
  }
}

TEST_F(FileBrowserTest, FileBrowserRenameCarriesProtectionSidecars) {
  fake::add("/Old.epub", "book");
  fake::add("/Old.epub.key", "key");
  fake::add("/Old.epub.rights", "rights");
  FileBrowserActivity ui;
  ui.renameSelectedFile("/Old.epub", "Old.epub", "New", ".epub");
  EXPECT_EQ(bytes("/New.epub"), "book");
  EXPECT_EQ(bytes("/New.epub.key"), "key");
  EXPECT_EQ(bytes("/New.epub.rights"), "rights");
  EXPECT_FALSE(Storage.exists("/Old.epub.key"));
  EXPECT_FALSE(Storage.exists("/Old.epub.rights"));
}

TEST_F(FileBrowserTest, RenameAndMoveNeverOverwriteDestinationReadingState) {
  const std::string oldPath = "/Old.epub", newPath = "/New.epub";
  for (const auto reading :
       {library::ReadingState::Unread, library::ReadingState::Reading, library::ReadingState::Finished}) {
    for (const bool favorite : {false, true}) {
      if (!favorite && reading == library::ReadingState::Unread) continue;
      SCOPED_TRACE(static_cast<int>(reading));
      SCOPED_TRACE(favorite);
      fake::reset();
      fake::add(oldPath, "book");
      fake::add(getBookCachePath(oldPath), "cache");
      fake::add(BookmarkUtil::getBookmarkPath(oldPath), "marks");
      ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
      ASSERT_TRUE(library::writeBookState(library::bookStateKey(newPath), {favorite, reading}));
      const auto writes = fake::writesByPath;
      EXPECT_FALSE(isBookPathFree(newPath));
      EXPECT_FALSE(moveBookWithState(oldPath, newPath));
      FileBrowserActivity ui;
      ui.renameSelectedFile(oldPath, "Old.epub", "New", ".epub");
      EXPECT_EQ(bytes(oldPath), "book");
      EXPECT_FALSE(Storage.exists(newPath.c_str()));
      EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
      EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(oldPath)), "marks");
      EXPECT_FALSE(Storage.exists(getBookCachePath(newPath).c_str()));
      EXPECT_FALSE(Storage.exists(BookmarkUtil::getBookmarkPath(newPath).c_str()));
      expectState(oldPath, true, library::ReadingState::Finished);
      expectState(newPath, favorite, reading);
      EXPECT_EQ(fake::writesByPath, writes);
      EXPECT_EQ(RECENT_BOOKS.changes, 0u);
      EXPECT_EQ(library::dirtyCalls, 0u);
    }
  }
}

TEST_F(FileBrowserTest, BackupOnlyDestinationReadingStateBlocksMove) {
  const std::string oldPath = "/Old.epub", newPath = "/New.epub";
  fake::add(oldPath, "book");
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(newPath), {true, library::ReadingState::Reading}));
  const auto primary = statePath(newPath);
  const auto backup = primary.substr(0, primary.size() - 4) + ".bak";
  ASSERT_TRUE(Storage.rename(primary.c_str(), backup.c_str()));
  const auto committed = bytes(backup);
  EXPECT_FALSE(isBookPathFree(newPath));
  EXPECT_FALSE(moveBookWithState(oldPath, newPath));
  EXPECT_EQ(bytes(oldPath), "book");
  EXPECT_FALSE(Storage.exists(newPath.c_str()));
  EXPECT_EQ(bytes(backup), committed);
  EXPECT_FALSE(Storage.exists(primary.c_str()));
  expectState(newPath, true, library::ReadingState::Reading);
}

TEST_F(FileBrowserTest, UnreadableDestinationReadingStateBlocksMove) {
  const std::string oldPath = "/Old.epub", newPath = "/New.epub";
  fake::add(oldPath, "book");
  fake::add(statePath(newPath), "broken state");
  EXPECT_FALSE(isBookPathFree(newPath));
  EXPECT_FALSE(moveBookWithState(oldPath, newPath));
  EXPECT_EQ(bytes(oldPath), "book");
  EXPECT_FALSE(Storage.exists(newPath.c_str()));
  EXPECT_EQ(bytes(statePath(newPath)), "broken state");
}

TEST_F(FileBrowserTest, PersistedDefaultDestinationStateAllowsMove) {
  const std::string oldPath = "/Old.epub", newPath = "/New.epub";
  fake::add(oldPath, "book");
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(newPath), {false, library::ReadingState::Reading}));
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(newPath), {}));
  ASSERT_TRUE(Storage.exists(statePath(newPath).c_str()));
  EXPECT_TRUE(isBookPathFree(newPath));
  ASSERT_TRUE(moveBookWithState(oldPath, newPath));
  EXPECT_EQ(bytes(newPath), "book");
  EXPECT_FALSE(Storage.exists(oldPath.c_str()));
  expectState(newPath, true, library::ReadingState::Finished);
  expectState(oldPath, false, library::ReadingState::Unread);
}

TEST_F(FileBrowserTest, NormalizedNoOpAndUnsafeNamesNeverChangeRawPaths) {
  fake::add("/Cafe\xcc\x81.epub");
  FileBrowserActivity ui;
  ui.renameSelectedFile("/Cafe\xcc\x81.epub", "Cafe\xcc\x81.epub", "Caf\xc3\xa9", ".epub");
  ui.renameSelectedFile("/Cafe\xcc\x81.epub", "Cafe\xcc\x81.epub", "../escape", ".epub");
  EXPECT_TRUE(Storage.exists("/Cafe\xcc\x81.epub"));
  EXPECT_EQ(RECENT_BOOKS.changes, 0u);
  EXPECT_EQ(library::dirtyCalls, 0u);
}

TEST_F(FileBrowserTest, FailedCountOrFillNeverPublishesPartialFolderAndCanRetry) {
  fake::add("/Books/a.epub");
  fake::add("/Books/b.epub");
  for (int at = 0; at < 6; ++at) {
    FileBrowserActivity ui;
    ui.basepath = "/Books";
    fake::failNext = at;
    fake::failureTriggered = false;
    ui.loadFiles();
    EXPECT_TRUE(fake::failureTriggered) << at;
    EXPECT_TRUE(ui.searchIncomplete) << at;
    EXPECT_TRUE(ui.files.empty()) << at;
    fake::failNext = -1;
    ui.loadFiles();
    EXPECT_FALSE(ui.searchIncomplete);
    EXPECT_EQ(ui.files, (std::vector<std::string>{"a.epub", "b.epub"}));
  }
}

TEST_F(FileBrowserTest, RecursiveEnumerationFailureShowsIncompleteResults) {
  fake::add("/Books/a.epub");
  fake::add("/Books/b.epub");
  FileBrowserActivity ui;
  ui.basepath = "/Books";
  ui.foldedQuery = "epub";
  fake::failNext = 1;
  ui.loadFiles();
  drain(ui);
  EXPECT_TRUE(ui.searchIncomplete);
  EXPECT_EQ(ui.files, (std::vector<std::string>{"a.epub"}));
}

class RelinkBookStateTest : public FileBrowserTest {
 protected:
  const std::string oldPath = "/Author/Old Title/Old.epub";
  const std::string newPath = "/Author/New Title/New.epub";

  void SetUp() override {
    FileBrowserTest::SetUp();
    fake::add(newPath);
    fake::add(BookmarkUtil::getBookmarkPath(oldPath), "primary");
    fake::add(BookmarkUtil::getBookmarkPath(oldPath) + ".bak", "backup");
    fake::add(getBookCachePath(oldPath), "cache");
    ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Reading}));
  }
};

TEST_F(RelinkBookStateTest, MovesCacheBookmarksAndReadingStateAndDropsTheOldState) {
  ASSERT_TRUE(relinkBookState(oldPath, newPath));
  EXPECT_EQ(bytes(getBookCachePath(newPath)), "cache");
  EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(newPath)), "primary");
  EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(newPath) + ".bak"), "backup");
  expectState(newPath, true, library::ReadingState::Reading);
  EXPECT_FALSE(Storage.exists(getBookCachePath(oldPath).c_str()));
  EXPECT_FALSE(Storage.exists(BookmarkUtil::getBookmarkPath(oldPath).c_str()));
  expectState(oldPath, false, library::ReadingState::Unread);
  EXPECT_TRUE(Storage.exists(newPath.c_str()));
}

TEST_F(RelinkBookStateTest, ExternalRelinkLeavesProtectionSidecarsAtBothPathsUntouched) {
  fake::add(oldPath + ".key", "old key");
  fake::add(oldPath + ".rights", "old rights");
  fake::add(newPath + ".key", "new key");
  fake::add(newPath + ".rights", "new rights");
  ASSERT_TRUE(relinkBookState(oldPath, newPath));
  EXPECT_EQ(bytes(oldPath + ".key"), "old key");
  EXPECT_EQ(bytes(oldPath + ".rights"), "old rights");
  EXPECT_EQ(bytes(newPath + ".key"), "new key");
  EXPECT_EQ(bytes(newPath + ".rights"), "new rights");
  expectState(newPath, true, library::ReadingState::Reading);
}

TEST_F(RelinkBookStateTest, NeverOverwritesStateAlreadyBuiltAtTheNewPath) {
  fake::add(getBookCachePath(newPath), "fresh cache");
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  EXPECT_EQ(bytes(getBookCachePath(newPath)), "fresh cache");
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  expectState(oldPath, true, library::ReadingState::Reading);

  fake::files.erase(getBookCachePath(newPath));
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(newPath), {false, library::ReadingState::Finished}));
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  expectState(newPath, false, library::ReadingState::Finished);
  EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(oldPath)), "primary");
}

TEST_F(RelinkBookStateTest, NeedsTheOldBookGoneAndTheNewBookPresent) {
  fake::add(oldPath);
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  fake::files.erase(oldPath);
  fake::files.erase(newPath);
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  EXPECT_FALSE(relinkBookState(oldPath, oldPath));
}

TEST_F(RelinkBookStateTest, BookWithoutStateChangesNothing) {
  fake::files.erase(getBookCachePath(oldPath));
  fake::files.erase(BookmarkUtil::getBookmarkPath(oldPath));
  fake::files.erase(BookmarkUtil::getBookmarkPath(oldPath) + ".bak");
  ASSERT_TRUE(library::removeBookState(library::bookStateKey(oldPath)));
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  EXPECT_FALSE(Storage.exists(getBookCachePath(newPath).c_str()));
}

TEST_F(RelinkBookStateTest, FailureRollsEverythingBackAndKeepsTheOldState) {
  fake::blockedRenames.push_back({BookmarkUtil::getBookmarkPath(oldPath), BookmarkUtil::getBookmarkPath(newPath)});
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  EXPECT_FALSE(Storage.exists(getBookCachePath(newPath).c_str()));
  EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(oldPath)), "primary");
  expectState(oldPath, true, library::ReadingState::Reading);
  expectState(newPath, false, library::ReadingState::Unread);
}

TEST_F(RelinkBookStateTest, MovesEveryClippingStoreFileAndLeavesNoneBehind) {
  const char* suffixes[] = {"", ".bak", ".tmp", ".deleted", ".migrate.bak"};
  for (const char* suffix : suffixes) fake::add(clippingFile(oldPath, suffix), std::string("clip") + suffix);
  ASSERT_TRUE(relinkBookState(oldPath, newPath));
  for (const char* suffix : suffixes) {
    SCOPED_TRACE(suffix);
    EXPECT_EQ(bytes(clippingFile(newPath, suffix)), std::string("clip") + suffix);
    EXPECT_FALSE(Storage.exists(clippingFile(oldPath, suffix).c_str()));
  }
  EXPECT_EQ(bytes(getBookCachePath(newPath)), "cache");
}

TEST_F(RelinkBookStateTest, ClippingsAloneAreStateWorthCarrying) {
  fake::files.erase(getBookCachePath(oldPath));
  fake::files.erase(BookmarkUtil::getBookmarkPath(oldPath));
  fake::files.erase(BookmarkUtil::getBookmarkPath(oldPath) + ".bak");
  ASSERT_TRUE(library::removeBookState(library::bookStateKey(oldPath)));
  fake::add(clippingFile(oldPath), "clip");
  ASSERT_TRUE(relinkBookState(oldPath, newPath));
  EXPECT_EQ(bytes(clippingFile(newPath)), "clip");
  EXPECT_FALSE(Storage.exists(clippingFile(oldPath).c_str()));
}

TEST_F(RelinkBookStateTest, ClippingsAlreadyAtTheNewPathBlockTheRelink) {
  fake::add(clippingFile(oldPath), "old clip");
  fake::add(clippingFile(newPath), "new clip");
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  EXPECT_EQ(bytes(clippingFile(oldPath)), "old clip");
  EXPECT_EQ(bytes(clippingFile(newPath)), "new clip");
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  expectState(oldPath, true, library::ReadingState::Reading);
}

TEST_F(RelinkBookStateTest, FailedClippingMoveRollsBackClippingsCacheBookmarksAndState) {
  fake::add(clippingFile(oldPath), "clip");
  fake::add(clippingFile(oldPath, ".deleted"), "ids");
  fake::blockedRenames.push_back({clippingFile(oldPath, ".deleted"), clippingFile(newPath, ".deleted")});
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  EXPECT_EQ(bytes(clippingFile(oldPath)), "clip");
  EXPECT_EQ(bytes(clippingFile(oldPath, ".deleted")), "ids");
  EXPECT_FALSE(Storage.exists(clippingFile(newPath).c_str()));
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  EXPECT_FALSE(Storage.exists(getBookCachePath(newPath).c_str()));
  EXPECT_EQ(bytes(BookmarkUtil::getBookmarkPath(oldPath)), "primary");
  expectState(oldPath, true, library::ReadingState::Reading);
  expectState(newPath, false, library::ReadingState::Unread);
}

TEST_F(RelinkBookStateTest, FailedClippingRollbackStillRestoresTheOtherStoreFiles) {
  for (const char* suffix : {"", ".bak", ".tmp", ".deleted"}) fake::add(clippingFile(oldPath, suffix), suffix);
  fake::blockedRenames.push_back({clippingFile(oldPath, ".deleted"), clippingFile(newPath, ".deleted")});
  fake::blockedRenames.push_back({clippingFile(newPath), clippingFile(oldPath)});
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  // The blocked file stays behind, but the rollback goes on to the rest.
  EXPECT_TRUE(Storage.exists(clippingFile(newPath).c_str()));
  for (const char* suffix : {".bak", ".tmp", ".deleted"}) {
    SCOPED_TRACE(suffix);
    EXPECT_TRUE(Storage.exists(clippingFile(oldPath, suffix).c_str()));
    EXPECT_FALSE(Storage.exists(clippingFile(newPath, suffix).c_str()));
  }
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  expectState(oldPath, true, library::ReadingState::Reading);
}

TEST_F(RelinkBookStateTest, ClippingsOfTheOpenBookAreNeverMovedUnderIt) {
  ASSERT_TRUE(CLIPPINGS.loadForBook(oldPath, "Old", "Author", "epub"));
  ASSERT_EQ(CLIPPINGS.addClipping(0, 0, 0, 1, 0, 1, 2, "Chapter", 0, "some text", 1), ClippingStore::AddResult::Added);
  ASSERT_TRUE(Storage.exists(clippingFile(oldPath).c_str()));
  EXPECT_FALSE(relinkBookState(oldPath, newPath));
  EXPECT_TRUE(Storage.exists(clippingFile(oldPath).c_str()));
  EXPECT_FALSE(Storage.exists(clippingFile(newPath).c_str()));
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  expectState(oldPath, true, library::ReadingState::Reading);
  CLIPPINGS.unload();
}
