#include <TtfEpdFont.h>
#include <esp_heap_caps.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
struct GlyphSnapshot {
  EpdGlyph metrics{};
  std::vector<uint8_t> bitmap;
};

class TtfEpdFontTest : public ::testing::Test {
 protected:
  std::vector<uint8_t> source;
  TtfEpdFont font;

  void SetUp() override {
    ttfTestAvailableHeap = 8 * 1024 * 1024;
    std::ifstream file(TTF_FIXTURE_PATH, std::ios::binary);
    ASSERT_TRUE(file.good());
    source.assign(std::istreambuf_iterator<char>(file), {});
    ASSERT_FALSE(source.empty());
    font.addResidentSource(TtfEpdFont::Regular, source.data(), static_cast<uint32_t>(source.size()));
    ASSERT_TRUE(font.load(14));
  }
  void TearDown() override { ttfTestAvailableHeap = 8 * 1024 * 1024; }

  GlyphSnapshot capture(uint32_t cp, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
    const auto family = font.family();
    const auto* glyph = family.getGlyph(cp, style);
    EXPECT_NE(nullptr, glyph);
    if (!glyph) return {};
    const auto* data = family.getData(style);
    EXPECT_NE(nullptr, data->vectorBitmapHandler);
    GlyphSnapshot result{*glyph, {}};
    const auto* bitmap = data->vectorBitmapHandler(data->glyphMissCtx, glyph);
    if (glyph->dataLength) {
      EXPECT_NE(nullptr, bitmap);
      if (bitmap) result.bitmap.assign(bitmap, bitmap + glyph->dataLength);
    } else {
      EXPECT_EQ(nullptr, bitmap);
    }
    return result;
  }

  static void expectSame(const GlyphSnapshot& a, const GlyphSnapshot& b) {
    EXPECT_EQ(a.metrics.width, b.metrics.width);
    EXPECT_EQ(a.metrics.height, b.metrics.height);
    EXPECT_EQ(a.metrics.advanceX, b.metrics.advanceX);
    EXPECT_EQ(a.metrics.left, b.metrics.left);
    EXPECT_EQ(a.metrics.top, b.metrics.top);
    EXPECT_EQ(a.bitmap, b.bitmap);
  }
};

TEST_F(TtfEpdFontTest, AllLazyStylesHaveMetricsAndPackedTwoBitInk) {
  std::vector<GlyphSnapshot> styles;
  styles.reserve(4);
  for (int i = 0; i < 4; ++i) {
    styles.push_back(capture('A', static_cast<EpdFontFamily::Style>(i)));
    const auto& glyph = styles.back();
    EXPECT_GT(glyph.metrics.advanceX, 0);
    EXPECT_GT(glyph.metrics.width, 0);
    EXPECT_GT(glyph.metrics.height, 0);
    EXPECT_EQ((glyph.metrics.width * glyph.metrics.height + 3U) / 4U, glyph.bitmap.size());
    EXPECT_TRUE(std::any_of(glyph.bitmap.begin(), glyph.bitmap.end(), [](uint8_t b) { return b != 0; }));
  }
  EXPECT_NE(styles[0].bitmap, styles[1].bitmap);
  EXPECT_NE(styles[0].bitmap, styles[2].bitmap);
}

TEST_F(TtfEpdFontTest, ClearAndReleaseCachesPreserveAllStyleMetricsAndBitmaps) {
  std::vector<GlyphSnapshot> expected;
  expected.reserve(4);
  for (int i = 0; i < 4; ++i) expected.push_back(capture('W', static_cast<EpdFontFamily::Style>(i)));
  const auto kern = font.family().getKerning('A', 'V');
  font.clearCache();
  for (int i = 0; i < 4; ++i) expectSame(expected[i], capture('W', static_cast<EpdFontFamily::Style>(i)));
  font.releaseResidentCaches();
  EXPECT_TRUE(font.family().hasCodepoint('W'));
  for (int i = 0; i < 4; ++i) expectSame(expected[i], capture('W', static_cast<EpdFontFamily::Style>(i)));
  EXPECT_EQ(kern, font.family().getKerning('A', 'V'));
}

TEST_F(TtfEpdFontTest, ReloadAtNewSizeDiscardsOldGlyphMetrics) {
  const auto small = capture('M');
  ASSERT_TRUE(font.load(24));
  const auto large = capture('M');
  EXPECT_GT(large.metrics.advanceX, small.metrics.advanceX);
  EXPECT_GT(large.metrics.height, small.metrics.height);
  ASSERT_TRUE(font.load(14));
  expectSame(small, capture('M'));
}

TEST_F(TtfEpdFontTest, OneBitModeAndSpaceRespectBitmapContract) {
  ASSERT_TRUE(font.load(14, false));
  const auto glyph = capture('A');
  EXPECT_EQ((glyph.metrics.width * glyph.metrics.height + 7U) / 8U, glyph.bitmap.size());
  EXPECT_FALSE(font.family().getData()->is2Bit);
  const auto space = capture(' ');
  EXPECT_GT(space.metrics.advanceX, 0);
  EXPECT_TRUE(space.bitmap.empty());
}

TEST_F(TtfEpdFontTest, FailedHeapPreflightCanRecoverWithoutReload) {
  font.releaseResidentCaches();
  ttfTestAvailableHeap = 0;
  EXPECT_EQ(nullptr, font.family().getGlyph('A'));
  ttfTestAvailableHeap = 8 * 1024 * 1024;
  const auto recovered = capture('A');
  EXPECT_GT(recovered.metrics.advanceX, 0);
  EXPECT_FALSE(recovered.bitmap.empty());
}

unsigned long readSource(void* context, unsigned long offset, unsigned char* output, unsigned long count) {
  const auto& bytes = *static_cast<const std::vector<uint8_t>*>(context);
  if (offset > bytes.size()) return 0;
  const size_t length = std::min<size_t>(count, bytes.size() - offset);
  if (length) std::memcpy(output, bytes.data() + offset, length);
  return length;
}

TEST_F(TtfEpdFontTest, StreamedSourceSurvivesCacheRelease) {
  TtfEpdFont streamed;
  streamed.addStreamSource(TtfEpdFont::Regular, readSource, &source, source.size());
  ASSERT_TRUE(streamed.load(14));
  const auto expected = capture('A');
  for (int pass = 0; pass < 2; ++pass) {
    const auto family = streamed.family();
    const auto* glyph = family.getGlyph('A');
    ASSERT_NE(nullptr, glyph);
    EXPECT_EQ(expected.metrics.advanceX, glyph->advanceX);
    const auto* data = family.getData();
    const auto* bits = data->vectorBitmapHandler(data->glyphMissCtx, glyph);
    ASSERT_NE(nullptr, bits);
    EXPECT_EQ(expected.bitmap, std::vector<uint8_t>(bits, bits + glyph->dataLength));
    streamed.releaseResidentCaches();
  }
}
}  // namespace
