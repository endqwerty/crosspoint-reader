#include <Epub.h>
#include <gtest/gtest.h>

#include "activities/reader/EpubSearchActivity.h"

namespace {
class SearchActivityTest : public ::testing::Test {
 protected:
  GfxRenderer renderer;
  MappedInputManager input;
  std::shared_ptr<Epub> book = std::make_shared<Epub>();
  std::unique_ptr<EpubSearchActivity> activity;
  void SetUp() override {
    Activity::finished = false;
    Activity::homeRequests = 0;
    Activity::paints = 0;
    SETTINGS = {};
    GfxRenderer::loans = 0;
    ASSERT_FALSE(GfxRenderer::loanActive);
    book->chapters = {"<html><body>First needle here</body></html>", "<html><body>Second needle here</body></html>"};
    activity = std::make_unique<EpubSearchActivity>(renderer, input, book);
    activity->onEnter();
  }
  void query(const std::string& text) {
    auto callback = std::move(activity->keyboardCallback);
    callback(ActivityResult{KeyboardResult{text}});
  }
  UiListActivity& list() { return *activity; }
};
}  // namespace

TEST_F(SearchActivityTest, DoesNotSearchBeforeTheQueryIsSubmitted) {
  for (int i = 0; i < 50; i++) activity->loop();
  EXPECT_EQ(book->reads, 0u);
  EXPECT_FALSE(activity->preventAutoSleep());
  EXPECT_EQ(GfxRenderer::loans, 0u);
}

TEST_F(SearchActivityTest, SearchesOnlyWhileOpenAndReturnsExistingOffsetResult) {
  query("needle");
  ASSERT_TRUE(activity->preventAutoSleep());
  activity->loop();
  EXPECT_EQ(book->reads, 1u);
  activity->loop();
  ASSERT_EQ(book->reads, 2u);
  ASSERT_FALSE(activity->preventAutoSleep());
  ASSERT_EQ(list().listCount(), 2);
  for (int i = 0; i < 100; i++) activity->loop();
  EXPECT_EQ(book->reads, 2u);
  EXPECT_EQ(input.updates, 2u);
  EXPECT_EQ(Activity::paints, 2u);  // One search screen, then complete results.
  list().activateIndex(1);
  ASSERT_TRUE(Activity::finished);
  ASSERT_TRUE(std::holds_alternative<ProgressChangeResult>(activity->result.data));
  const auto& result = std::get<ProgressChangeResult>(activity->result.data);
  EXPECT_TRUE(result.hasVisibleTextOffset);
  EXPECT_EQ(result.spineIndex, 1);
  EXPECT_EQ(result.visibleTextOffset, 7u);
  activity->onExit();
  EXPECT_EQ(book.use_count(), 1u);
  EXPECT_EQ(list().listCount(), 0);
}

TEST_F(SearchActivityTest, CancelledKeyboardNeverStartsSearch) {
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  auto callback = std::move(activity->keyboardCallback);
  callback(cancelled);
  EXPECT_TRUE(Activity::finished);
  EXPECT_TRUE(activity->result.isCancelled);
  EXPECT_EQ(book->reads, 0u);
}

TEST_F(SearchActivityTest, InvalidQueryDoesNotScanOrAllocateResultRows) {
  query(" \t ");
  activity->loop();
  EXPECT_FALSE(activity->preventAutoSleep());
  EXPECT_EQ(book->reads, 0u);
  EXPECT_EQ(list().listCount(), 0);
}

TEST_F(SearchActivityTest, CancellationUnwindsBeforeFinishingAndSwallowsHeldBack) {
  book->chapters[0] = "<html><body>" + std::string(100000, 'x') + "</body></html>";
  query("needle");
  input.cancelAt = 3;
  activity->loop();
  EXPECT_EQ(book->reads, 1u);
  EXPECT_FALSE(GfxRenderer::loanActive);
  EXPECT_EQ(input.updates, 3u);
  EXPECT_FALSE(Activity::finished);  // Do not leak the held Back release into the reader.
  activity->loop();
  EXPECT_EQ(book->reads, 1u);
  input.releaseBack = true;
  activity->loop();
  EXPECT_TRUE(Activity::finished);
  EXPECT_TRUE(activity->result.isCancelled);
  EXPECT_EQ(Activity::homeRequests, 0u);
  activity->onExit();
  EXPECT_EQ(list().listCount(), 0);
}

TEST_F(SearchActivityTest, HomeDuringScanUnwindsBeforeLeavingAndClearsDeferredAction) {
  book->chapters[0] = "<html><body>" + std::string(100000, 'x') + "</body></html>";
  query("needle");
  input.gpio.tapAt = 3;
  activity->loop();
  EXPECT_EQ(book->reads, 1u);
  EXPECT_EQ(input.updates, 3u);
  EXPECT_EQ(Activity::homeRequests, 1u);
  EXPECT_FALSE(Activity::finished);
  EXPECT_FALSE(GfxRenderer::loanActive);
  EXPECT_FALSE(searchTestReadActive);
  EXPECT_FALSE(searchTestRenderLockHeld);
  input.update();
  EXPECT_EQ(input.homeButtonAction(), HomeButtonAction::Ignore);
}

TEST_F(SearchActivityTest, HomeCancellationTakesPrecedenceOverHeldBack) {
  book->chapters[0] = "<html><body>" + std::string(100000, 'x') + "</body></html>";
  query("needle");
  input.cancelAt = input.gpio.tapAt = 3;
  activity->loop();
  ASSERT_TRUE(input.isPressed(MappedInputManager::Button::Back));
  EXPECT_EQ(Activity::homeRequests, 1u);
  EXPECT_FALSE(Activity::finished);
  EXPECT_EQ(book->reads, 1u);
}

TEST_F(SearchActivityTest, FirstNonHomeActionSurvivesScanPollsAndIsDeliveredOnce) {
  book->chapters[0] = "<html><body>" + std::string(10000, 'x') + "</body></html>";
  SETTINGS.homeButtonTapAction = HomeButtonAction::Refresh;
  query("needle");
  input.gpio.tapAt = 2;
  input.gpio.holdAt = 4;
  activity->loop();
  ASSERT_GT(input.updates, 4u);
  EXPECT_EQ(Activity::homeRequests, 0u);
  EXPECT_FALSE(Activity::finished);
  EXPECT_EQ(book->reads, 1u);
  EXPECT_EQ(input.homeButtonAction(), HomeButtonAction::Ignore);
  input.update();
  EXPECT_EQ(input.homeButtonAction(), HomeButtonAction::Refresh);
  input.update();
  EXPECT_EQ(input.homeButtonAction(), HomeButtonAction::Ignore);
}

TEST_F(SearchActivityTest, ReturnsFrameBufferBeforeResultsAndReadFailureHandling) {
  query("needle");
  activity->loop();
  EXPECT_FALSE(GfxRenderer::loanActive);
  EXPECT_EQ(GfxRenderer::loans, 1u);
  book->readFailure = true;
  activity->loop();
  EXPECT_FALSE(GfxRenderer::loanActive);
  EXPECT_EQ(GfxRenderer::loans, 2u);
  EXPECT_EQ(list().listCount(), 1);
  UiScreen screen;
  list().buildScreen(screen);
  EXPECT_EQ(screen.lastCount, 1);
  EXPECT_EQ(screen.selectionOffset, 0);
}

TEST_F(SearchActivityTest, PartialChapterFailureKeepsSuccessfulOtherChapters) {
  book->chapters[0] = "<html><body>needle</broken>";
  query("needle");
  activity->loop();
  activity->loop();
  ASSERT_EQ(list().listCount(), 1);
  list().activateIndex(0);
  EXPECT_EQ(std::get<ProgressChangeResult>(activity->result.data).spineIndex, 1);
}

TEST_F(SearchActivityTest, ResultCapStopsBeforeReadingRemainingChapters) {
  book->chapters[0] = "<html><body>" + std::string(100000, 'a') + "</body></html>";
  query("a");
  activity->loop();
  ASSERT_EQ(list().listCount(), 32);
  ASSERT_FALSE(activity->preventAutoSleep());
  EXPECT_EQ(book->reads, 1u);
  activity->loop();
  EXPECT_EQ(book->reads, 1u);
}

TEST_F(SearchActivityTest, PartialResultsHaveASeparateHeaderWithoutOverlappingTheList) {
  book->chapters[0] = "<html><body>needle</broken>";
  query("needle");
  activity->loop();
  activity->loop();
  UiScreen screen;
  list().buildScreen(screen);
  EXPECT_EQ(screen.lastCount, 1);
  EXPECT_EQ(screen.selectionOffset, 0);
  EXPECT_EQ(screen.centeredCalls, 0);
  EXPECT_STREQ(list().headerTitle(), "STR_BOOK_SEARCH_PARTIAL_TITLE");
}
