#include <EpdFont.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <tuple>

#include "RealFontPage.h"

namespace {
constexpr int BINARY_FONT = 1;
constexpr int GRAY_FONT = 2;
constexpr int MIXED_FONT = 3;
constexpr uint8_t BINARY_BITMAP[] = {0xff, 0xff};
constexpr uint8_t GRAY_BITMAP[] = {0x1b, 0x1b, 0x1b, 0x1b};  // white, light, dark, black
constexpr EpdGlyph BINARY_GLYPH[] = {{4, 4, 64, 0, 4, 2, 0}};
constexpr EpdGlyph GRAY_GLYPH[] = {{4, 4, 64, 0, 4, 4, 0}};
// Each singleton interval intentionally reuses the same real bitmap glyph.
constexpr EpdUnicodeInterval BINARY_INTERVALS[] = {{'A', 'A', 0}, {0x0301, 0x0301, 0}, {0xfffd, 0xfffd, 0}};
constexpr EpdUnicodeInterval GRAY_INTERVALS[] = {
    {'A', 'A', 0}, {0x0301, 0x0301, 0}, {0x4e2d, 0x4e2d, 0}, {0xfffd, 0xfffd, 0}};

constexpr EpdFontData makeFontData(bool gray) {
  EpdFontData data{};
  data.bitmap = gray ? GRAY_BITMAP : BINARY_BITMAP;
  data.glyph = gray ? GRAY_GLYPH : BINARY_GLYPH;
  data.intervals = gray ? GRAY_INTERVALS : BINARY_INTERVALS;
  data.intervalCount = gray ? 4 : 3;
  data.advanceY = 8;
  data.ascender = 4;
  data.is2Bit = gray;
  return data;
}
constexpr EpdFontData BINARY_DATA = makeFontData(false);
constexpr EpdFontData GRAY_DATA = makeFontData(true);
const EpdFont binaryFont(&BINARY_DATA);
const EpdFont grayFont(&GRAY_DATA);

class TextGrayscaleTracking : public testing::Test {
 protected:
  HalDisplay display;
  GfxRenderer renderer{display};
  FontCacheManager cache{renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts()};

  void SetUp() override {
    renderer.begin();
    renderer.insertFont(BINARY_FONT, EpdFontFamily(&binaryFont));
    renderer.insertFont(GRAY_FONT, EpdFontFamily(&grayFont));
    renderer.insertFont(MIXED_FONT, EpdFontFamily(&binaryFont, &grayFont));
    renderer.setFontCacheManager(&cache);
    renderer.clearScreen();
  }
};

TEST_F(TextGrayscaleTracking, EmptyPageAndEmptyStringsNeedNoGray) {
  renderer.beginTextGrayscaleTracking();
  renderer.drawText(GRAY_FONT, 10, 10, nullptr);
  renderer.drawText(GRAY_FONT, 10, 10, "");
  renderer.drawTextRotated90CW(GRAY_FONT, 10, 10, "");
  EXPECT_FALSE(renderer.endTextGrayscaleTracking());
  EXPECT_TRUE(std::all_of(display.frame.begin(), display.frame.end(), [](uint8_t value) { return value == 0xff; }));
}

TEST_F(TextGrayscaleTracking, UsesResolvedStyleIncludingMissingStyleFallback) {
  for (auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC}) {
    renderer.beginTextGrayscaleTracking();
    renderer.drawText(BINARY_FONT, 10, 10, "A", true, style);
    EXPECT_FALSE(renderer.endTextGrayscaleTracking());

    renderer.beginTextGrayscaleTracking();
    renderer.drawText(MIXED_FONT, 10, 10, "A", true, style);
    EXPECT_EQ(renderer.endTextGrayscaleTracking(), style == EpdFontFamily::BOLD || style == EpdFontFamily::BOLD_ITALIC);
  }
}

TEST_F(TextGrayscaleTracking, ResolvesCjkFallbackForNormalAndRotatedText) {
  renderer.setFallbackFont(BINARY_FONT, GRAY_FONT);
  for (bool rotated : {false, true}) {
    renderer.beginTextGrayscaleTracking();
    if (rotated) {
      renderer.drawTextRotated90CW(BINARY_FONT, 20, 20, "中");
    } else {
      renderer.drawText(BINARY_FONT, 20, 20, "中");
    }
    EXPECT_TRUE(renderer.endTextGrayscaleTracking());
  }
}

TEST_F(TextGrayscaleTracking, MissingGlyphUsesTheResolvedFontsReplacement) {
  for (int fontId : {BINARY_FONT, GRAY_FONT}) {
    renderer.beginTextGrayscaleTracking();
    renderer.drawText(fontId, 20, 20, "?");
    EXPECT_EQ(renderer.endTextGrayscaleTracking(), fontId == GRAY_FONT);
  }
}

TEST_F(TextGrayscaleTracking, PrewarmStatusBarAndPreviousPageDoNotLeakIntoBody) {
  renderer.beginTextGrayscaleTracking();
  {
    auto scope = cache.createPrewarmScope();
    renderer.drawText(GRAY_FONT, 20, 20, "A");
    renderer.drawTextRotated90CW(GRAY_FONT, 20, 20, "A");
    scope.endScanAndPrewarm();
  }
  EXPECT_FALSE(renderer.endTextGrayscaleTracking());
  renderer.beginTextGrayscaleTracking();
  renderer.drawText(BINARY_FONT, 20, 20, "A");
  EXPECT_FALSE(renderer.endTextGrayscaleTracking());
  renderer.drawText(GRAY_FONT, 20, 40, "A");  // status bar, outside the body scope
  EXPECT_FALSE(renderer.endTextGrayscaleTracking());

  renderer.beginTextGrayscaleTracking();
  renderer.drawText(GRAY_FONT, 20, 20, "A");
  EXPECT_TRUE(renderer.endTextGrayscaleTracking());
  renderer.beginTextGrayscaleTracking();
  EXPECT_FALSE(renderer.endTextGrayscaleTracking());
  EXPECT_EQ(display.refreshCount, 0u);
}

TEST_F(TextGrayscaleTracking, GrayCompositionDoesNotChangeTheBwClassification) {
  renderer.beginTextGrayscaleTracking();
  for (auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.setRenderMode(mode);
    renderer.drawText(GRAY_FONT, 20, 20, "A");
  }
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.drawText(BINARY_FONT, 20, 20, "A");
  EXPECT_FALSE(renderer.endTextGrayscaleTracking());
}

TEST_F(TextGrayscaleTracking, ClippedTwoBitTextConservativelyKeepsGray) {
  renderer.beginTextGrayscaleTracking();
  renderer.drawText(GRAY_FONT, -100, -100, "A");
  EXPECT_TRUE(renderer.endTextGrayscaleTracking());
  EXPECT_TRUE(std::all_of(display.frame.begin(), display.frame.end(), [](uint8_t value) { return value == 0xff; }));
}

enum class DrawPath { Normal, Rotated, Superscript, Subscript };
using PixelCase = std::tuple<GfxRenderer::Orientation, DrawPath, bool>;
class TextGrayscalePixels : public TextGrayscaleTracking, public testing::WithParamInterface<PixelCase> {};

TEST_P(TextGrayscalePixels, BinaryBlackTextHasEmptyMasksAndGrayTextIsRetained) {
  const auto [orientation, path, black] = GetParam();
  renderer.setOrientation(orientation);
  auto draw = [&](int fontId) {
    if (path == DrawPath::Rotated) {
      renderer.drawTextRotated90CW(fontId, 40, 40, "A\u0301A", black);
    } else {
      const auto style = path == DrawPath::Superscript ? EpdFontFamily::SUP
                         : path == DrawPath::Subscript ? EpdFontFamily::SUB
                                                       : EpdFontFamily::REGULAR;
      renderer.drawText(fontId, 40, 40, "A\u0301A", black, style);
    }
  };
  for (int fontId : {BINARY_FONT, GRAY_FONT}) {
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen(black ? 0xff : 0);
    draw(fontId);
    const auto withoutTracking = display.frame;
    renderer.clearScreen(black ? 0xff : 0);
    renderer.beginTextGrayscaleTracking();
    draw(fontId);
    const bool needsGray = renderer.endTextGrayscaleTracking();
    EXPECT_EQ(display.frame, withoutTracking);
    EXPECT_EQ(needsGray, fontId == GRAY_FONT || !black);

    for (auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      renderer.setRenderMode(mode);
      renderer.clearScreen(0);
      draw(fontId);
      const bool maskNonempty = std::any_of(display.frame.begin(), display.frame.end(), [](uint8_t v) { return v; });
      // A false detector result must never omit a mask write. Scaled 2bpp
      // black glyphs may also have empty masks; retaining those is deliberate.
      EXPECT_TRUE(needsGray || !maskNonempty);
      if (fontId == BINARY_FONT) EXPECT_EQ(maskNonempty, !black);
      if (fontId == GRAY_FONT && (path == DrawPath::Normal || path == DrawPath::Rotated)) EXPECT_TRUE(maskNonempty);
    }
  }
  EXPECT_EQ(display.refreshCount, 0u);
}

INSTANTIATE_TEST_SUITE_P(
    AllGlyphPaths, TextGrayscalePixels,
    testing::Combine(testing::Values(GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                                     GfxRenderer::PortraitInverted, GfxRenderer::LandscapeCounterClockwise),
                     testing::Values(DrawPath::Normal, DrawPath::Rotated, DrawPath::Superscript, DrawPath::Subscript),
                     testing::Bool()));

TEST(TextGrayscaleRealFonts, BuiltinCompressedTextKeepsGrayWithoutChangingBw) {
  page_test::RealFontPage page;
  page.prepare(page_test::Scene::Multilingual, true);
  page.renderer.setRenderMode(GfxRenderer::BW);
  page.renderer.clearScreen();
  page.renderer.drawText(page_test::FONT_ID, 20, 20, "A\u0301 ffi café Читатель", true, EpdFontFamily::BOLD);
  const auto withoutTracking = page.display.frame;
  page.renderer.clearScreen();
  page.renderer.beginTextGrayscaleTracking();
  page.renderer.drawText(page_test::FONT_ID, 20, 20, "A\u0301 ffi café Читатель", true, EpdFontFamily::BOLD);
  EXPECT_TRUE(page.renderer.endTextGrayscaleTracking());
  EXPECT_EQ(page.display.frame, withoutTracking);
  EXPECT_EQ(page.display.refreshCount, 0u);
}
}  // namespace
