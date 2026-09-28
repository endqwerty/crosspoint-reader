#include <gtest/gtest.h>

#include <algorithm>
#include <tuple>

#include "RealFontPage.h"

namespace {
using namespace page_test;
using Case = std::tuple<GfxRenderer::Orientation, HalDisplay::GrayscaleMode, Scene, bool>;
class RealFontPlanes : public testing::TestWithParam<Case> {};

TEST_P(RealFontPlanes, FullHeightMatchesLegacyPlanesAndPreservesBw) {
  const auto [orientation, grayMode, scene, prewarm] = GetParam();
  RealFontPage page;
  ASSERT_TRUE(page.hasFixtureGlyphs());
  page.renderer.setOrientation(orientation);
  page.prepare(scene, prewarm);
  ASSERT_TRUE(page.renderer.displayGrayscaleBase(grayMode, HalDisplay::FAST_REFRESH));
  ASSERT_EQ(page.renderer.grayPlanesAreAbsolute(), grayMode == HalDisplay::GrayscaleMode::Absolute);
  const auto oldStats = page.compose(scene, LEGACY_STRIP_ROWS);
  const auto oldLsb = page.lsb;
  const auto oldMsb = page.msb;
  EXPECT_EQ(page.display.frame, page.bwBeforeComposition);

  page.prepare(scene, prewarm);
  ASSERT_TRUE(page.renderer.displayGrayscaleBase(grayMode, HalDisplay::FAST_REFRESH));
  const auto newStats = page.compose(scene, FULL_HEIGHT_ROWS);
  EXPECT_EQ(page.lsb, oldLsb);
  EXPECT_EQ(page.msb, oldMsb);
  EXPECT_EQ(page.display.frame, page.bwBeforeComposition);
  EXPECT_TRUE(std::any_of(page.lsb.begin(), page.lsb.end(), [](uint8_t v) { return v != 0; }));
  EXPECT_TRUE(std::any_of(page.msb.begin(), page.msb.end(), [](uint8_t v) { return v != 0; }));
  if (grayMode == HalDisplay::GrayscaleMode::Overlay) {
    EXPECT_NE(page.lsb, page.msb);
  } else {
    EXPECT_EQ(page.lsb, page.msb);
  }
  EXPECT_EQ(oldStats.pageTraversals, 12u);
  EXPECT_EQ(newStats.pageTraversals, 2u);
  EXPECT_EQ(oldStats.textDrawCalls, 6 * newStats.textDrawCalls);
  EXPECT_GT(newStats.bitmapCalls, 0u);
  EXPECT_LE(newStats.bitmapCalls, oldStats.bitmapCalls);
  if (prewarm) {
    EXPECT_EQ(oldStats.cacheMisses, 0u);
    EXPECT_EQ(newStats.cacheMisses, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(
    RealCompressedFonts, RealFontPlanes,
    testing::Combine(testing::Values(GfxRenderer::Portrait, GfxRenderer::PortraitInverted,
                                     GfxRenderer::LandscapeClockwise, GfxRenderer::LandscapeCounterClockwise),
                     testing::Values(HalDisplay::GrayscaleMode::Overlay, HalDisplay::GrayscaleMode::Absolute),
                     testing::Values(Scene::Prose, Scene::Multilingual, Scene::EdgeCases), testing::Bool()));
}  // namespace
