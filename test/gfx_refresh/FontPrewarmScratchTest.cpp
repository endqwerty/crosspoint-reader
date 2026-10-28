// Prewarm scratch allocation contract for the production compressed-font decoder,
// using the real Noto Sans 14 regular font and a counting fiFontMalloc/fiFontFree.
#include <FontAlloc.h>
#include <FontDecompressor.h>
#include <Utf8.h>
#include <builtinFonts/notosans_14_regular.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

namespace {
struct AllocTracker {
  std::map<void*, size_t> live;
  size_t liveBytes = 0;
  size_t peakBytes = 0;
  uint32_t mallocs = 0;
  std::set<size_t> failSizes;

  void reset() {
    mallocs = 0;
    peakBytes = liveBytes;
    failSizes.clear();
  }
};
AllocTracker tracker;
}  // namespace

extern "C" {
void* fiFontMalloc(size_t size) {
  tracker.mallocs++;
  if (tracker.failSizes.count(size)) return nullptr;
  void* p = malloc(size);
  if (!p) return nullptr;
  tracker.live[p] = size;
  tracker.liveBytes += size;
  tracker.peakBytes = std::max(tracker.peakBytes, tracker.liveBytes);
  return p;
}
void* fiFontRealloc(void*, size_t) {
  ADD_FAILURE() << "FontDecompressor must not realloc";
  return nullptr;
}
void fiFontFree(void* p) {
  if (!p) return;
  auto it = tracker.live.find(p);
  if (it == tracker.live.end()) {
    ADD_FAILURE() << "free of unowned pointer";
    return;
  }
  tracker.liveBytes -= it->second;
  tracker.live.erase(it);
  free(p);
}
}

namespace {
// Ligature outputs are dropped so the needed-glyph set is exactly the text's glyphs.
EpdFontData withoutLigatures(EpdFontData data) {
  data.ligaturePairs = nullptr;
  data.ligaturePairCount = 0;
  return data;
}
const EpdFontData font = withoutLigatures(notosans_14_regular);
// Latin, Polish, Vietnamese and Cyrillic: spans several groups of different sizes.
constexpr const char* TEXT = "Café, déjà vu. Zażółć gęślą jaźń. Tiếng Việt: đọc sách. Читатель открывает книгу.";

int32_t glyphIndexOf(const EpdFontData& data, uint32_t cp) {
  for (uint32_t i = 0; i < data.intervalCount; i++) {
    const auto& in = data.intervals[i];
    if (cp >= in.first && cp <= in.last) return static_cast<int32_t>(in.offset + (cp - in.first));
  }
  return -1;
}

uint16_t groupOf(const EpdFontData& data, uint32_t glyphIndex) {
  for (uint16_t g = 0; g < data.groupCount; g++) {
    const auto& group = data.groups[g];
    if (glyphIndex >= group.firstGlyphIndex && glyphIndex < group.firstGlyphIndex + group.glyphCount) return g;
  }
  return data.groupCount;
}

std::vector<uint32_t> neededGlyphs(const EpdFontData& data, const char* text) {
  std::set<uint32_t> out;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    const int32_t idx = glyphIndexOf(data, cp);
    if (idx >= 0) out.insert(static_cast<uint32_t>(idx));
  }
  return {out.begin(), out.end()};
}

std::set<uint16_t> groupsFor(const EpdFontData& data, const std::vector<uint32_t>& glyphs) {
  std::set<uint16_t> groups;
  for (uint32_t g : glyphs) groups.insert(groupOf(data, g));
  return groups;
}

uint32_t largestGroup(const EpdFontData& data, const std::set<uint16_t>& groups) {
  uint32_t largest = 0;
  for (uint16_t g : groups) largest = std::max(largest, data.groups[g].uncompressedSize);
  return largest;
}

// Packed glyph bytes from the non-prewarmed hot-group path of a separate decoder.
std::vector<uint8_t> referenceBitmap(const EpdFontData& data, uint32_t glyphIndex) {
  FontDecompressor reference;
  reference.init();
  const EpdGlyph& glyph = data.glyph[glyphIndex];
  const uint8_t* bits = reference.getBitmap(&data, &glyph, glyphIndex);
  if (!bits) return {};
  return {bits, bits + glyph.dataLength};
}

class FontPrewarmScratch : public testing::Test {
 protected:
  void SetUp() override {
    glyphs = neededGlyphs(font, TEXT);
    groups = groupsFor(font, glyphs);
    ASSERT_GE(groups.size(), 3u);
    for (uint32_t g : glyphs) pageBytes += font.glyph[g].dataLength;
    tracker.reset();
  }
  void TearDown() override { tracker.failSizes.clear(); }

  std::vector<uint32_t> glyphs;
  std::set<uint16_t> groups;
  size_t pageBytes = 0;
};

TEST_F(FontPrewarmScratch, OnePrewarmAllocatesOneScratchForAllGroups) {
  FontDecompressor fdc;
  fdc.init();
  const size_t before = tracker.liveBytes;
  tracker.reset();

  ASSERT_EQ(fdc.prewarmCache(&font, TEXT), 0);

  // Page buffer, lookup table and one scratch buffer, whatever the group count.
  EXPECT_EQ(tracker.mallocs, 3u) << groups.size() << " groups";
  const auto& stats = fdc.getStats();
  EXPECT_EQ(stats.uniqueGroupsAccessed, groups.size());
  EXPECT_EQ(stats.pageBufferBytes, pageBytes);
  EXPECT_EQ(stats.peakTempBytes, largestGroup(font, groups));
  // Scratch is released before return; only the page slot remains.
  EXPECT_EQ(tracker.liveBytes - before, stats.pageBufferBytes + stats.pageGlyphsBytes);
  // Peak is unchanged from one-group-at-a-time: page slot plus the largest group.
  EXPECT_EQ(tracker.peakBytes - before, stats.pageBufferBytes + stats.pageGlyphsBytes + stats.peakTempBytes);
}

TEST_F(FontPrewarmScratch, ScratchLifetimeEndsWithEachPrewarm) {
  FontDecompressor fdc;
  fdc.init();
  const size_t before = tracker.liveBytes;
  ASSERT_EQ(fdc.prewarmCache(&font, TEXT), 0);
  const size_t afterFirst = tracker.liveBytes;
  tracker.reset();
  ASSERT_EQ(fdc.prewarmCache(&font, "Zażółć"), 0);
  EXPECT_EQ(tracker.mallocs, 3u);
  const auto& stats = fdc.getStats();
  EXPECT_EQ(tracker.liveBytes - before, stats.pageBufferBytes + stats.pageGlyphsBytes);
  EXPECT_GT(tracker.liveBytes, afterFirst);
  fdc.clearCache();
  EXPECT_EQ(tracker.liveBytes, before);
}

TEST_F(FontPrewarmScratch, PrewarmedBitmapsMatchHotGroupBytes) {
  FontDecompressor fdc;
  fdc.init();
  ASSERT_EQ(fdc.prewarmCache(&font, TEXT), 0);
  fdc.resetStats();
  for (uint32_t g : glyphs) {
    const EpdGlyph& glyph = font.glyph[g];
    const uint8_t* bits = fdc.getBitmap(&font, &glyph, g);
    ASSERT_NE(bits, nullptr) << g;
    const auto expected = referenceBitmap(font, g);
    ASSERT_EQ(expected.size(), glyph.dataLength);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), bits)) << "glyph " << g;
  }
  EXPECT_EQ(fdc.getStats().cacheMisses, 0u);
}

// When the largest group cannot be allocated, smaller groups are still extracted,
// as they were when each group had its own allocation.
TEST_F(FontPrewarmScratch, LargestGroupAllocationFailureKeepsSmallerGroups) {
  const uint32_t largest = largestGroup(font, groups);
  ASSERT_NE(largest, pageBytes);
  ASSERT_GT(largest, glyphs.size() * 3 * sizeof(uint32_t));  // not the lookup table size
  uint32_t failedGroups = 0;
  for (uint16_t g : groups) failedGroups += font.groups[g].uncompressedSize == largest;

  FontDecompressor fdc;
  fdc.init();
  const size_t before = tracker.liveBytes;
  tracker.reset();
  tracker.failSizes.insert(largest);
  EXPECT_EQ(fdc.prewarmCache(&font, TEXT), static_cast<int>(failedGroups));
  const auto& stats = fdc.getStats();
  EXPECT_EQ(tracker.liveBytes - before, stats.pageBufferBytes + stats.pageGlyphsBytes);

  size_t extracted = 0;
  for (uint32_t g : glyphs) {
    const EpdGlyph& glyph = font.glyph[g];
    const bool inFailed = font.groups[groupOf(font, g)].uncompressedSize == largest;
    const uint8_t* bits = fdc.getBitmap(&font, &glyph, g);
    if (inFailed) {
      // The hot-group fallback needs the same allocation, so it fails too.
      EXPECT_EQ(bits, nullptr) << g;
      continue;
    }
    ASSERT_NE(bits, nullptr) << g;
    tracker.failSizes.clear();
    const auto expected = referenceBitmap(font, g);
    tracker.failSizes.insert(largest);
    ASSERT_EQ(expected.size(), glyph.dataLength);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), bits)) << "glyph " << g;
    extracted++;
  }
  EXPECT_GT(extracted, 0u);
  fdc.clearCache();
  EXPECT_EQ(tracker.liveBytes, before);
}

TEST_F(FontPrewarmScratch, PageBufferAllocationFailureAllocatesNoScratch) {
  FontDecompressor fdc;
  fdc.init();
  const size_t before = tracker.liveBytes;
  tracker.reset();
  tracker.failSizes.insert(pageBytes);
  EXPECT_EQ(fdc.prewarmCache(&font, TEXT), static_cast<int>(glyphs.size()));
  EXPECT_EQ(tracker.mallocs, 2u);
  EXPECT_EQ(tracker.liveBytes, before);
}

// A group whose stream fails to inflate is counted as missed; the shared scratch
// still serves later groups and is released.
TEST_F(FontPrewarmScratch, DecompressionFailureKeepsOtherGroupsAndFreesScratch) {
  std::vector<EpdFontGroup> brokenGroups(font.groups, font.groups + font.groupCount);
  const uint16_t broken = *groups.begin();
  brokenGroups[broken].compressedSize = 1;
  EpdFontData data = font;
  data.groups = brokenGroups.data();

  FontDecompressor fdc;
  fdc.init();
  const size_t before = tracker.liveBytes;
  tracker.reset();
  EXPECT_EQ(fdc.prewarmCache(&data, TEXT), 1);
  EXPECT_EQ(tracker.mallocs, 3u);
  const auto& stats = fdc.getStats();
  EXPECT_EQ(tracker.liveBytes - before, stats.pageBufferBytes + stats.pageGlyphsBytes);

  fdc.resetStats();
  for (uint32_t g : glyphs) {
    if (groupOf(font, g) == broken) continue;
    const EpdGlyph& glyph = font.glyph[g];
    const uint8_t* bits = fdc.getBitmap(&data, &glyph, g);
    ASSERT_NE(bits, nullptr) << g;
    const auto expected = referenceBitmap(font, g);
    ASSERT_EQ(expected.size(), glyph.dataLength);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), bits)) << "glyph " << g;
  }
  EXPECT_EQ(fdc.getStats().cacheMisses, 0u);
}
}  // namespace
