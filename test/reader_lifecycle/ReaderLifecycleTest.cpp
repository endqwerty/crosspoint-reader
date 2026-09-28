#include <gtest/gtest.h>

#include "ReaderLifecycleFixture.h"
class ReaderLifecycleTest : public testing::Test {
 protected:
  ReaderActivity reader;
  void SetUp() override {
    APP_STATE = {};
    RECENT_BOOKS = {};
    Storage = {};
    pluginevents::openedBooks.clear();
  }
};
TEST_F(ReaderLifecycleTest, ClearsRememberedBookBeforeParsingAnUnreadableFile) {
  APP_STATE.openEpubPath = reader.bookPath;
  reader.loadSucceeds = false;
  reader.onEnter();
  EXPECT_FALSE(reader.rememberedDuringLoad);
  EXPECT_EQ(APP_STATE.savedPaths, std::vector<std::string>{""});
  EXPECT_EQ(reader.finishes, 1);
  EXPECT_EQ(reader.updates, 0);
  reader.rememberBookOnceRendered();
  EXPECT_TRUE(APP_STATE.openEpubPath.empty());
  EXPECT_EQ(RECENT_BOOKS.additions, 0);
}
TEST_F(ReaderLifecycleTest, LoadedMetadataDoesNotMakeAnUnrenderedBookReopenOnWake) {
  reader.onEnter();
  EXPECT_EQ(reader.updates, 1);
  for (int i = 0; i < 100; ++i) reader.rememberBookOnceRendered();
  EXPECT_TRUE(APP_STATE.savedPaths.empty());
  EXPECT_EQ(RECENT_BOOKS.additions, 0);
}
TEST_F(ReaderLifecycleTest, SuccessfulPagePublishesIdentityExactlyOnce) {
  reader.onEnter();
  reader.markPageRendered();
  for (int i = 0; i < 100; ++i) reader.rememberBookOnceRendered();
  EXPECT_EQ(APP_STATE.savedPaths, std::vector<std::string>{reader.bookPath});
  EXPECT_EQ(RECENT_BOOKS.additions, 1);
  EXPECT_EQ(RECENT_BOOKS.path, reader.bookPath);
  EXPECT_EQ(RECENT_BOOKS.title, "A title");
  EXPECT_EQ(RECENT_BOOKS.author, "An author");
  EXPECT_EQ(RECENT_BOOKS.thumb, "/thumb.bmp");
  EXPECT_EQ(pluginevents::openedBooks, std::vector<std::string>{reader.bookPath});
}
TEST_F(ReaderLifecycleTest, HandledLoadFailureKeepsTheReaderOpenWithoutRememberingTheBook) {
  reader.loadSucceeds = false;
  reader.loadFailureHandled = true;
  reader.onEnter();
  EXPECT_EQ(reader.finishes, 0);
  EXPECT_EQ(reader.updates, 0);
  reader.rememberBookOnceRendered();
  EXPECT_EQ(RECENT_BOOKS.additions, 0);
  EXPECT_TRUE(pluginevents::openedBooks.empty());
}
TEST_F(ReaderLifecycleTest, EveryRenderReportsTimeAndProgressToTheReadingSession) {
  reader.onEnter();
  reader.render(RenderLock{});
  reader.end = true;
  reader.render(RenderLock{});
  EXPECT_EQ(reader.readerSession.renders, 2);
  EXPECT_EQ(reader.readerSession.lastMs, 4321u);
  EXPECT_EQ(reader.readerSession.lastEpoch, 1700000000);
  EXPECT_EQ(reader.readerSession.lastProgressBp, 2500);
}
TEST_F(ReaderLifecycleTest, SwitchingBooksClearsPreviousThenRemembersRenderedReplacement) {
  APP_STATE.openEpubPath = "/books/previous.epub";
  reader.onEnter();
  EXPECT_TRUE(APP_STATE.openEpubPath.empty());
  reader.markPageRendered();
  reader.rememberBookOnceRendered();
  EXPECT_EQ(APP_STATE.savedPaths, (std::vector<std::string>{"", reader.bookPath}));
}
TEST_F(ReaderLifecycleTest, MissingFileFinishesWithoutAttemptingLoad) {
  Storage.present = false;
  reader.onEnter();
  EXPECT_EQ(reader.loads, 0);
  EXPECT_EQ(reader.finishes, 1);
  EXPECT_EQ(reader.updates, 0);
}
TEST_F(ReaderLifecycleTest, EndOfBookIsRememberedOnlyAfterDisplayCommits) {
  reader.onEnter();
  reader.end = true;
  reader.renderer.committed = false;
  reader.render(RenderLock{});
  reader.rememberBookOnceRendered();
  EXPECT_FALSE(reader.pageRendered.load());
  EXPECT_TRUE(APP_STATE.openEpubPath.empty());
  reader.renderer.committed = true;
  reader.render(RenderLock{});
  reader.rememberBookOnceRendered();
  EXPECT_TRUE(reader.pageRendered.load());
  EXPECT_EQ(APP_STATE.openEpubPath, reader.bookPath);
  EXPECT_EQ(RECENT_BOOKS.additions, 1);
}
