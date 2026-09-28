#include <gtest/gtest.h>

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
  const auto oldBookmarks = BookmarkUtil::getBookmarkPath(oldPath);
  const auto newBookmarks = BookmarkUtil::getBookmarkPath(newPath);
  fake::add(oldBookmarks, "primary");
  fake::add(oldBookmarks + ".bak", "backup");
  fake::add(oldBookmarks + ".new", "stage");
  fake::add(getBookCachePath(oldPath), "cache");
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(oldPath), {true, library::ReadingState::Finished}));
  ASSERT_TRUE(library::writeBookState(library::bookStateKey(newPath), {false, library::ReadingState::Reading}));
  fake::blockedRenames.push_back({oldPath, newPath});
  FileBrowserActivity ui;
  ui.renameSelectedFile(oldPath, "Old.epub", "New", ".epub");
  EXPECT_TRUE(Storage.exists(oldPath.c_str()));
  EXPECT_FALSE(Storage.exists(newPath.c_str()));
  EXPECT_EQ(bytes(oldBookmarks), "primary");
  EXPECT_EQ(bytes(oldBookmarks + ".bak"), "backup");
  EXPECT_EQ(bytes(oldBookmarks + ".new"), "stage");
  EXPECT_FALSE(Storage.exists(newBookmarks.c_str()));
  EXPECT_FALSE(Storage.exists((newBookmarks + ".bak").c_str()));
  EXPECT_FALSE(Storage.exists((newBookmarks + ".new").c_str()));
  EXPECT_EQ(bytes(getBookCachePath(oldPath)), "cache");
  expectState(oldPath, true, library::ReadingState::Finished);
  expectState(newPath, false, library::ReadingState::Reading);
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
