#include <EpdFont.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

namespace {
constexpr int FONT = 7;
constexpr uint8_t BITMAP[] = {0x80};
constexpr EpdGlyph GLYPHS[] = {{1, 1, 64, 0, 1, 1, 0}, {0, 0, 32, 0, 0, 0, 0}, {1, 1, 0, 0, 3, 1, 0}};
constexpr EpdUnicodeInterval INTERVALS[] = {{' ', ' ', 1},   {'A', 'A', 0},       {'B', 'B', 0},      {'C', 'C', 0},
                                            {0xa0, 0xa0, 1}, {0x0301, 0x0301, 2}, {0x3000, 0x3000, 1}};
constexpr EpdFontData fontData() {
  EpdFontData data{};
  data.bitmap = BITMAP;
  data.glyph = GLYPHS;
  data.intervals = INTERVALS;
  data.intervalCount = 7;
  data.advanceY = 8;
  data.ascender = 3;
  return data;
}
constexpr EpdFontData DATA = fontData();
const EpdFont font(&DATA);

class TextSpacing : public testing::Test {
 protected:
  HalDisplay display;
  GfxRenderer renderer{display};
  void SetUp() override {
    renderer.begin();
    renderer.insertFont(FONT, EpdFontFamily(&font));
  }
};

TEST_F(TextSpacing, GlyphPositionsAndAdvanceAgreeWithPositiveAndNegativeTracking) {
  for (auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                           GfxRenderer::LandscapeCounterClockwise}) {
    renderer.setOrientation(orientation);
    for (int8_t tracking : {-1, 0, 2}) {
      renderer.clearScreen();
      renderer.drawText(FONT, 10, 10, "ABC", true, EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::AUTO, tracking);
      const auto actual = display.frame;
      renderer.clearScreen();
      for (int column = 0; column < 3; ++column) renderer.drawPixel(10 + column * (4 + tracking), 12);
      EXPECT_EQ(actual, display.frame);
      for (auto mode : {GfxRenderer::TextMeasureMode::Layout, GfxRenderer::TextMeasureMode::Rendered}) {
        EXPECT_EQ(
            renderer.getTextAdvanceX(FONT, "ABC", EpdFontFamily::REGULAR, tracking, BidiUtils::BidiBaseDir::AUTO, mode),
            12 + 2 * tracking);
      }
    }
  }
}

TEST_F(TextSpacing, SpaceBoundariesDoNotReceiveCharacterTracking) {
  for (const char* text : {"A B", "A\u00a0B", "A\u3000B"}) {
    EXPECT_EQ(renderer.getTextAdvanceX(FONT, text, EpdFontFamily::REGULAR, 3), 10);
    renderer.clearScreen();
    renderer.drawText(FONT, 10, 10, text, true, EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::AUTO, 3);
    const auto actual = display.frame;
    renderer.clearScreen();
    renderer.drawPixel(10, 12);
    renderer.drawPixel(16, 12);
    EXPECT_EQ(actual, display.frame);
  }
  EXPECT_EQ(renderer.getKerning(FONT, 'A', 'B', EpdFontFamily::REGULAR, 3), 3);
  EXPECT_EQ(renderer.getKerning(FONT, 'A', ' ', EpdFontFamily::REGULAR, 3), 0);
}

TEST_F(TextSpacing, CombiningMarksDoNotAddAdvanceOrTracking) {
  EXPECT_EQ(renderer.getTextAdvanceX(FONT, "A\u0301B", EpdFontFamily::REGULAR, 3), 11);
  EXPECT_EQ(renderer.getTextAdvanceX(FONT, "A\u0301B", EpdFontFamily::REGULAR, -1), 7);
}

TEST_F(TextSpacing, SuperscriptScalesGlyphAdvanceButNotConfiguredTracking) {
  for (auto style : {EpdFontFamily::SUP, EpdFontFamily::SUB}) {
    EXPECT_EQ(renderer.getTextAdvanceX(FONT, "ABC", style, 2), 10);
  }
}
}  // namespace
