#include "RealFontPage.h"

#include <EpdFont.h>
#include <Utf8.h>
#include <builtinFonts/notoserif_14_bold.h>
#include <builtinFonts/notoserif_14_bolditalic.h>
#include <builtinFonts/notoserif_14_italic.h>
#include <builtinFonts/notoserif_14_regular.h>

#include <algorithm>

namespace page_test {
namespace {
const EpdFont regular(&notoserif_14_regular);
const EpdFont bold(&notoserif_14_bold);
const EpdFont italic(&notoserif_14_italic);
const EpdFont boldItalic(&notoserif_14_bolditalic);
const EpdFontFamily family(&regular, &bold, &italic, &boldItalic);
constexpr int BINARY_FONT_ID = 2;
constexpr uint8_t BINARY_BITMAP[] = {0xab, 0x75, 0x6e, 0xd8, 0xe9, 0xa0};
constexpr EpdGlyph BINARY_GLYPH[] = {{7, 6, 8 * 64, -1, 5, sizeof(BINARY_BITMAP), 0}};
constexpr EpdUnicodeInterval BINARY_INTERVALS[] = {{'A', 'A', 0}};
constexpr EpdFontData binaryData() {
  EpdFontData data{};
  data.bitmap = BINARY_BITMAP;
  data.glyph = BINARY_GLYPH;
  data.intervals = BINARY_INTERVALS;
  data.intervalCount = 1;
  data.advanceY = 8;
  data.ascender = 5;
  return data;
}
constexpr EpdFontData BINARY_DATA = binaryData();
const EpdFont binaryFont(&BINARY_DATA);

constexpr const char* PROSE[] = {
    "The quiet reader turned a page.",
    "A fine office, a swift flight.",
    "AVATAR: To read, to remember.",
    "Soft light fell across the room.",
};
constexpr const char* MULTILINGUAL[] = {
    "Café, déjà vu, naïve façade.",
    "Zażółć gęślą jaźń. Łódź.",
    "Tiếng Việt: đọc sách mỗi ngày.",
    "Читатель открывает книгу.",
};
constexpr const char* OVERLAP = "A\u0301 o\u0308 ffi AV";
}  // namespace

RealFontPage::RealFontPage() : renderer(display), cache(renderer.getFontMap(), noSdFonts, renderer.getTtfFonts()) {
  renderer.begin();
  renderer.insertFont(FONT_ID, family);
  renderer.insertFont(BINARY_FONT_ID, EpdFontFamily(&binaryFont));
  decompressor.init();
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
}

bool RealFontPage::hasFixtureGlyphs() const {
  for (const auto& lines : {PROSE, MULTILINGUAL}) {
    for (int line = 0; line < 4; ++line) {
      const auto* cursor = reinterpret_cast<const uint8_t*>(lines[line]);
      while (const uint32_t cp = utf8NextCodepoint(&cursor)) {
        if (!family.hasCodepoint(cp, static_cast<EpdFontFamily::Style>(line))) return false;
      }
    }
  }
  const auto* cursor = reinterpret_cast<const uint8_t*>(OVERLAP);
  while (const uint32_t cp = utf8NextCodepoint(&cursor)) {
    if (!family.hasCodepoint(cp)) return false;
  }
  return true;
}

uint32_t RealFontPage::drawScene(const Scene scene) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  uint32_t calls = 0;
  const auto draw = [&](int x, int y, const char* text, bool black, EpdFontFamily::Style style) {
    renderer.drawText(FONT_ID, x, y, text, black, style);
    ++calls;
  };
  const auto* lines = scene == Scene::Multilingual ? MULTILINGUAL : PROSE;
  int line = 0;
  for (int y = 4; y + 40 <= height; y += 39, ++line) {
    for (int x = 12; x < width - 100; x += 470) {
      draw(x, y, lines[line % 4], true, static_cast<EpdFontFamily::Style>(line % 4));
    }
  }
  if (scene == Scene::EdgeCases) {
    // Exercise transparent overlap, combining marks, superscript/subscript,
    // white-on-black text, clipped glyphs and the renderer's decoration primitives.
    draw(-15, -16, OVERLAP, true, EpdFontFamily::ITALIC);
    draw(width - 47, height - 19, OVERLAP, true, EpdFontFamily::BOLD_ITALIC);
    draw(width / 2 - 30, height / 2 - 30, OVERLAP, true, EpdFontFamily::REGULAR);
    draw(width / 2 - 25, height / 2 - 28, OVERLAP, true, EpdFontFamily::ITALIC);
    draw(20, 77, "2", true, static_cast<EpdFontFamily::Style>(EpdFontFamily::SUP | EpdFontFamily::BOLD));
    draw(40, 81, "2", true, static_cast<EpdFontFamily::Style>(EpdFontFamily::SUB | EpdFontFamily::ITALIC));
    renderer.fillRect(20, height - 72, 280, 44, true);
    draw(24, height - 70, "Reverse: office", false, EpdFontFamily::BOLD);
    renderer.drawLine(-5, 79, width + 4, 79, true);
    renderer.drawLine(10, height / 2, width - 10, height / 2, 2, true);
    renderer.drawRect(-2, -2, width + 4, height + 4, 3, true);
    renderer.drawTextRotated90CW(FONT_ID, width - 35, height - 30, "Rotated ffi", true);
    ++calls;
    for (const int x : {-2, 24, width - 4}) {
      renderer.drawText(BINARY_FONT_ID, x, 23, "AAAA", true);
      renderer.drawText(BINARY_FONT_ID, x + 2, 25, "AAAA", false);
      calls += 2;
    }
  }
  return calls;
}

void RealFontPage::prepare(const Scene scene, const bool prewarm) {
  prewarmScope.reset();
  cache.clearCache();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  if (prewarm) {
    prewarmScope.emplace(cache);
    drawScene(scene);
    prewarmScope->endScanAndPrewarm();
  }
  drawScene(scene);
  bwBeforeComposition = display.frame;
  if (!prewarm) decompressor.clearCache();
}

CompositionStats RealFontPage::compose(const Scene scene, const int rows) {
  CompositionStats result;
  decompressor.resetStats();
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    auto& target = mode == GfxRenderer::GRAYSCALE_LSB ? lsb : msb;
    renderer.setRenderMode(mode);
    for (int y = 0; y < FULL_HEIGHT_ROWS; y += rows) {
      const int stripRows = std::min(rows, FULL_HEIGHT_ROWS - y);
      renderer.beginStripTarget(target.data() + y * HalDisplay::DISPLAY_WIDTH_BYTES, y, stripRows);
      renderer.clearScreen(renderer.grayPlanesAreAbsolute() ? 0xff : 0);
      result.textDrawCalls += drawScene(scene);
      ++result.pageTraversals;
      renderer.endStripTarget();
    }
  }
  renderer.setRenderMode(GfxRenderer::BW);
  result.bitmapCalls = decompressor.getStats().getBitmapCalls;
  result.cacheMisses = decompressor.getStats().cacheMisses;
  return result;
}

const char* sceneName(const Scene scene) {
  switch (scene) {
    case Scene::Prose:
      return "prose";
    case Scene::Multilingual:
      return "multilingual";
    case Scene::EdgeCases:
      return "clipping-overlap";
  }
  return "unknown";
}

const char* orientationName(const GfxRenderer::Orientation orientation) {
  switch (orientation) {
    case GfxRenderer::Portrait:
      return "portrait";
    case GfxRenderer::PortraitInverted:
      return "portrait-inverted";
    case GfxRenderer::LandscapeClockwise:
      return "landscape-cw";
    case GfxRenderer::LandscapeCounterClockwise:
      return "landscape-ccw";
  }
  return "unknown";
}
}  // namespace page_test
