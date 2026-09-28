#include <LibraryBookDetailsActivity.h>
#include <gtest/gtest.h>

#include "ScopedAllocationFailure.h"
#include "components/UITheme.h"
class LibraryDetailsTest : public testing::Test {
 protected:
  GfxRenderer renderer;
  MappedInputManager input;
  void SetUp() override {
    GUI = {};
    themeInstance = {};
    ButtonNavigator::input = &input;
    errors = 0;
  }
  std::string drawn() const {
    std::string value;
    for (const auto& line : renderer.lines) value += line.text;
    return value;
  }
};
TEST_F(LibraryDetailsTest, LongTitleAndPathAreReachableWithoutTruncationInEveryOrientation) {
  for (auto orientation :
       {GfxRenderer::Orientation::Portrait, GfxRenderer::Orientation::PortraitInverted,
        GfxRenderer::Orientation::LandscapeClockwise, GfxRenderer::Orientation::LandscapeCounterClockwise}) {
    renderer.orientation = orientation;
    renderer.width =
        (orientation == GfxRenderer::Orientation::Portrait || orientation == GfxRenderer::Orientation::PortraitInverted)
            ? 240
            : 400;
    renderer.height = renderer.width == 240 ? 400 : 240;
    LibraryBookDetailsActivity activity(renderer, input);
    const std::string title = std::string(219, 'a') + "é中😀" + std::string(400, 'b');
    const std::string path = "/" + std::string(1500, 'x') + "/the final book.epub";
    ASSERT_TRUE(activity.setBook(title, path));
    activity.onEnter();
    std::string all;
    for (int page = 0; page < 100; ++page) {
      activity.render(RenderLock{});
      all += drawn();
      auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      for (const auto& line : renderer.lines) {
        EXPECT_GE(line.rect.x, safe.x);
        EXPECT_GE(line.rect.y, safe.y + themeInstance.metrics.topPadding + themeInstance.metrics.headerHeight);
        EXPECT_LE(line.rect.right(), safe.x + safe.width);
        EXPECT_LE(line.rect.bottom(), safe.y + safe.height);
        EXPECT_LE(line.text.size(), 220u);
        if (!line.text.empty()) EXPECT_NE(static_cast<unsigned char>(line.text[0]) & 0xc0, 0x80);
      }
      const int before = activity.updates;
      input.swipe = MappedInputManager::SwipeDir::Up;
      activity.loop();
      if (activity.updates == before) break;
    }
    EXPECT_EQ(all, "Title" + title + "File path" + path);
  }
}
TEST_F(LibraryDetailsTest, PageEdgesDoNotRepaintAndBackDoesNotOpen) {
  LibraryBookDetailsActivity activity(renderer, input);
  ASSERT_TRUE(activity.setBook("Small", "/book.epub"));
  activity.onEnter();
  activity.render(RenderLock{});
  const int before = activity.updates;
  input.previous = true;
  activity.loop();
  input.next = true;
  activity.loop();
  EXPECT_EQ(activity.updates, before);
  input.back = true;
  activity.loop();
  EXPECT_EQ(activity.finishes, 1);
  EXPECT_EQ(activity.result, -1);
}
TEST_F(LibraryDetailsTest, ButtonsAndSwipesReachSamePagesAndConfirmOpens) {
  LibraryBookDetailsActivity activity(renderer, input);
  ASSERT_TRUE(activity.setBook(std::string(1000, 'a'), "/book.epub"));
  activity.onEnter();
  activity.render(RenderLock{});
  const auto first = drawn();
  input.next = true;
  activity.loop();
  activity.render(RenderLock{});
  EXPECT_NE(drawn(), first);
  input.swipe = MappedInputManager::SwipeDir::Down;
  activity.loop();
  activity.render(RenderLock{});
  EXPECT_EQ(drawn(), first);
  input.confirm = true;
  activity.loop();
  EXPECT_EQ(activity.finishes, 1);
  EXPECT_EQ(activity.result, 0);
}
TEST_F(LibraryDetailsTest, BufferAllocationFailureLeavesPreviousDetailsUsable) {
  LibraryBookDetailsActivity activity(renderer, input);
  ASSERT_TRUE(activity.setBook("Original", "/old.epub"));
  {
    parser_test::ScopedAllocationFailure fault(parser_test::ScopedAllocationFailure::Kind::Array);
    EXPECT_FALSE(activity.setBook("New", "/new.epub"));
    EXPECT_EQ(fault.failures(), 1u);
  }
  activity.onEnter();
  activity.render(RenderLock{});
  EXPECT_EQ(drawn(), "TitleOriginalFile path/old.epub");
  EXPECT_GT(errors, 0);
}
TEST_F(LibraryDetailsTest, RejectsOversizeAndEmbeddedNulWithoutSilentShortening) {
  LibraryBookDetailsActivity activity(renderer, input);
  EXPECT_FALSE(activity.setBook(std::string(8193, 'a'), "/book"));
  EXPECT_FALSE(activity.setBook("book", std::string("/a\0b", 4)));
  EXPECT_FALSE(activity.setBook(std::string(5000, 'a'), std::string(5000, 'b')));
  EXPECT_GE(errors, 3);
}
