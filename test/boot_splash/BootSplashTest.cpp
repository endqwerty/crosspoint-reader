#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <builtinFonts/notosans_8_regular.h>
#include <builtinFonts/ubuntu_10_bold.h>
#include <builtinFonts/ubuntu_10_regular.h>

#include <bit>
#include <tuple>

#include "BootSplashFixture.h"

HalDisplay display;
namespace {
const EpdFont regular(&ubuntu_10_regular);
const EpdFont bold(&ubuntu_10_bold);
const EpdFont small(&notosans_8_regular);
const EpdFontFamily uiFamily(&regular, &bold);
const EpdFontFamily smallFamily(&small);

class BootSplash : public testing::TestWithParam<std::tuple<GfxRenderer::Orientation, bool, bool>> {
 protected:
  GfxRenderer renderer{display};
  FontDecompressor decompressor;
  const std::map<int, SdCardFont*> noSdFonts;
  FontCacheManager cache{renderer.getFontMap(), noSdFonts, renderer.getTtfFonts()};

  void SetUp() override {
    display = {};
    display.setInverted(std::get<1>(GetParam()));
    SETTINGS.screenInverted = std::get<2>(GetParam());
    RenderLock::held = RenderLock::acquisitions = 0;
    renderer.begin();
    renderer.setOrientation(std::get<0>(GetParam()));
    renderer.insertFont(UI_10_FONT_ID, uiFamily);
    renderer.insertFont(SMALL_FONT_ID, smallFamily);
    ASSERT_TRUE(decompressor.init());
    cache.setFontDecompressor(&decompressor);
    renderer.setFontCacheManager(&cache);
  }

  size_t whitePixels() const {
    size_t count = 0;
    for (const auto byte : display.frame) {
      const auto physical = static_cast<uint8_t>(display.lastInverted ? ~byte : byte);
      count += std::popcount(physical);
    }
    return count;
  }
};

TEST_P(BootSplash, AlwaysDarkWithOneSubmissionAndUnchangedTheme) {
  BootActivity boot(renderer);
  boot.onEnter();
  EXPECT_EQ(boot.enters, 1u);
  EXPECT_EQ(RenderLock::acquisitions, 1u);
  EXPECT_EQ(RenderLock::held, 0u);
  EXPECT_EQ(display.refreshCount, 1u);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
  EXPECT_FALSE(display.lastInverted);
  EXPECT_EQ(SETTINGS.screenInverted, std::get<2>(GetParam()));
  EXPECT_EQ(renderer.getOrientation(), std::get<0>(GetParam()));
  EXPECT_EQ(display.frame.front(), 0u);
  EXPECT_EQ(display.frame.back(), 0u);
  // The actual logo and built-in UI fonts must remain visible on a mostly black canvas.
  size_t logoInkPixels = 0;
  for (const auto byte : Logo120) logoInkPixels += 8 - std::popcount(byte);
  EXPECT_GT(whitePixels(), logoInkPixels);
  EXPECT_LT(whitePixels(), HalDisplay::BUFFER_SIZE * 8 / 10);
}

TEST_P(BootSplash, FollowingScreenCanUseItsSelectedTheme) {
  BootActivity boot(renderer);
  boot.onEnter();
  // ActivityManager applies this setting before rendering the next activity.
  display.setInverted(SETTINGS.screenInverted != 0);
  renderer.clearScreen();
  renderer.displayBuffer();
  EXPECT_EQ(display.refreshCount, 2u);
  EXPECT_EQ(display.lastInverted, std::get<2>(GetParam()));
  EXPECT_EQ(whitePixels(), std::get<2>(GetParam()) ? 0u : HalDisplay::BUFFER_SIZE * 8);
}

INSTANTIATE_TEST_SUITE_P(AllThemesAndOrientations, BootSplash,
                         testing::Combine(testing::Values(GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                                                          GfxRenderer::PortraitInverted,
                                                          GfxRenderer::LandscapeCounterClockwise),
                                          testing::Bool(), testing::Bool()));
}  // namespace
