#include <EpdFont.h>
#include <builtinFonts/libron_14_bold.h>
#include <builtinFonts/libron_14_bolditalic.h>
#include <builtinFonts/libron_14_italic.h>
#include <builtinFonts/libron_14_regular.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "RealFontPage.h"

namespace {
constexpr int LIBRON_TEST_ID = 42;
const EpdFont regular(&libron_14_regular);
const EpdFont bold(&libron_14_bold);
const EpdFont italic(&libron_14_italic);
const EpdFont boldItalic(&libron_14_bolditalic);
const EpdFontFamily family(&regular, &bold, &italic, &boldItalic);
constexpr auto STYLES =
    std::array{EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC};
}  // namespace

TEST(LibronCoverage, RetainsLatinAndFillsCyrillicPunctuationAndReplacementInEveryStyle) {
  constexpr uint32_t codepoints[] = {'A', 'z', 0x00e9, 0x0416, 0x044f, 0x2009, 0x2011, 0x202f, 0xfffd};
  for (const auto style : STYLES) {
    for (const uint32_t cp : codepoints) {
      EXPECT_TRUE(family.hasCodepoint(cp, style)) << cp << " style " << style;
    }
  }
}

TEST(LibronCoverage, UnknownCharactersUseAVisibleReplacement) {
  for (const EpdFont* font : {&regular, &bold, &italic, &boldItalic}) {
    const auto* replacement = font->getGlyph(0xfffd);
    ASSERT_NE(replacement, nullptr);
    EXPECT_GT(replacement->width, 0);
    EXPECT_GT(replacement->height, 0);
    EXPECT_EQ(font->getGlyph(0x10ffff), replacement);
  }
}

TEST(LibronCoverage, MeasuresAndDecompressesCyrillicOnTheNormalReaderPath) {
  page_test::RealFontPage page;
  page.renderer.insertFont(LIBRON_TEST_ID, family);
  for (const auto style : STYLES) {
    page.renderer.clearScreen();
    EXPECT_GT(page.renderer.getTextAdvanceX(LIBRON_TEST_ID, "Журнал", style), 0);
    page.renderer.drawText(LIBRON_TEST_ID, 20, 40, "Журнал", true, style);
    EXPECT_TRUE(
        std::any_of(page.display.frame.begin(), page.display.frame.end(), [](uint8_t value) { return value != 0xff; }));
  }
}
