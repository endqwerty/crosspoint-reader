#include <HalStorage.h>
#include <SdCardFont.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "FontFixture.h"
#include "HostAllocations.h"

namespace {
using namespace sd_font_fixture;

void expectPageBitmaps(SdCardFont& font, uint32_t first, uint32_t count) {
  const auto* data = font.getEpdFont()->data;
  for (uint32_t cp = first; cp < first + count; ++cp) {
    const EpdGlyph* glyph = nullptr;
    for (uint32_t i = 0; i < data->intervalCount; ++i) {
      const auto& interval = data->intervals[i];
      if (cp >= interval.first && cp <= interval.last) glyph = data->glyph + interval.offset + cp - interval.first;
    }
    ASSERT_NE(nullptr, glyph) << cp;
    ASSERT_EQ(BITMAP_BYTES, glyph->dataLength);
    for (uint16_t i = 0; i < BITMAP_BYTES; ++i) {
      ASSERT_EQ((cp - FIRST) % 251, data->bitmap[glyph->dataOffset + i]);
    }
  }
}
}  // namespace

TEST(SdCardFontTest, CompletePagesReplaceEarlierGlyphs) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  for (uint32_t offset : {0U, 100U, 200U}) {
    const uint32_t first = FIRST + offset;
    const auto text = page(first, 100);
    font.clearCache();
    ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
    EXPECT_EQ(101U, residentCount(font));  // includes replacement glyph
    expectPageBitmaps(font, first, 100);
  }
}

TEST(SdCardFontTest, IncrementalUiStringsStillAccumulate) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm(page(FIRST, 5).c_str(), 1));
  ASSERT_EQ(0, font.prewarm(page(FIRST + 5, 5).c_str(), 1));
  EXPECT_EQ(11U, residentCount(font));
  expectPageBitmaps(font, FIRST, 10);
}

TEST(SdCardFontTest, UnderusedBuffersKeepTheFreshlyPrewarmedPageUntilItIsDrawn) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm(page(FIRST, 200).c_str(), 1, false, false, false));
  font.clearCache();
  for (uint32_t offset : {300U, 400U, 200U, 0U}) {
    const auto text = page(FIRST + offset, 100);
    font.clearCache();
    ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
    font.clearCache();  // idle prewarm scope closes
    font.clearCache();  // actual page render scope opens
    sdFontTestReads = 0;
    ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
    EXPECT_EQ(0U, sdFontTestReads);
    expectPageBitmaps(font, FIRST + offset, 100);
  }
}

TEST(SdCardFontTest, BitmapGrowthPreservesReadOrderAndAllGlyphData) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  for (uint32_t count : {50U, 100U, 200U, 400U}) {
    font.clearCache();
    ASSERT_EQ(0, font.prewarm(page(FIRST, count).c_str(), 1, false, false, false));
    EXPECT_EQ(count + 1, residentCount(font));
    expectPageBitmaps(font, FIRST, count);
  }
}

TEST(SdCardFontTest, FragmentedBitmapGrowthRebuildsMetadataAndKeepsThePrefetch) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm(page(FIRST, 50).c_str(), 1, false, false, false));
  struct HeapReportGuard {
    ~HeapReportGuard() { ESP.largestBlock = 200 * 1024; }
  } guard;
  ESP.largestBlock = 8 * 1024;
  const auto text = page(FIRST, 200);
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  expectPageBitmaps(font, FIRST, 200);
  font.clearCache();
  sdFontTestReads = 0;
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  EXPECT_EQ(0U, sdFontTestReads);
}

TEST(SdCardFontTest, BitmapAllocationRetriesAfterEvictingRebuildableAdvances) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto text = page(FIRST, 200);
  ASSERT_EQ(0, font.buildAdvanceTable(text.c_str(), 1));
  ASSERT_TRUE(font.hasAdvanceTable());
  ASSERT_EQ(0, font.prewarm(page(FIRST, 50).c_str(), 1, false, false, false));
  struct AllocationGuard {
    ~AllocationGuard() {
      ESP.largestBlock = 200 * 1024;
      failNextArraySize = 0;
    }
  } guard;
  ESP.largestBlock = 8 * 1024;
  failNextArraySize = 201 * BITMAP_BYTES;
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  EXPECT_EQ(0U, failNextArraySize);
  EXPECT_FALSE(font.hasAdvanceTable());
  expectPageBitmaps(font, FIRST, 200);

  ASSERT_EQ(0, font.buildAdvanceTable(text.c_str(), 1));
  for (uint32_t cp = FIRST; cp < FIRST + 200; ++cp) {
    EXPECT_EQ(32 << 4, font.getAdvance(cp, 0));
  }
}

namespace {
void expectIo(size_t opens, size_t seeks, size_t reads, size_t bytes) {
  EXPECT_EQ(opens, sdFontTestIo.opens);
  EXPECT_EQ(seeks, sdFontTestIo.seeks);
  EXPECT_EQ(reads, sdFontTestIo.reads);
  EXPECT_EQ(bytes, sdFontTestIo.bytes);
}

struct FaultGuard {
  ~FaultGuard() {
    sdFontTestFailOpen = 0;
    sdFontTestFailSeek = 0;
    sdFontTestShortRead = 0;
    ESP.freeHeap = 200 * 1024;
  }
};
}  // namespace

TEST(SdCardFontTest, CompletePageIoDoesNotGrowWithPreviouslyReadPages) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  for (uint32_t offset : {0U, 100U, 200U, 300U, 400U, 0U}) {
    const auto text = page(FIRST + offset, 100);
    font.clearCache();
    sdFontTestIo = {};
    ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
    // 100 requested glyphs + replacement: metadata and bitmap once each.
    // The requested run and isolated replacement each need two seeks.
    expectIo(1, 4, 202, 101 * (sizeof(EpdGlyph) + BITMAP_BYTES));
    EXPECT_EQ(101U, residentCount(font));
    ASSERT_TRUE(pageIntact(font, FIRST + offset, 100));
    font.clearCache();  // idle prewarm scope closes
    font.clearCache();  // foreground draw scope opens
    sdFontTestIo = {};
    ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
    expectIo(0, 0, 0, 0);
    ASSERT_TRUE(pageIntact(font, FIRST + offset, 100));
  }
}

TEST(SdCardFontTest, FourStylesRetainIndependentGlyphDataAndNeedNoIoOnTurn) {
  makeFont(4);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(4, font.styleCount());
  for (uint32_t offset : {0U, 100U, 200U}) {
    const auto text = page(FIRST + offset, 100);
    sdFontTestIo = {};
    ASSERT_EQ(0, font.prewarm(text.c_str(), 0x0F, false, false, false));
    expectIo(4, 16, 808, 4 * 101 * (sizeof(EpdGlyph) + BITMAP_BYTES));
    ASSERT_TRUE(pageIntact(font, FIRST + offset, 100, 4));
    font.clearCache();
    font.clearCache();
    sdFontTestIo = {};
    ASSERT_EQ(0, font.prewarm(text.c_str(), 0x0F, false, false, false));
    expectIo(0, 0, 0, 0);
    ASSERT_TRUE(pageIntact(font, FIRST + offset, 100, 4));
  }
}

TEST(SdCardFontTest, ScatteredGlyphsHaveBoundedSeeksAndBecomeIoFree) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto text = page(FIRST, 100, 4);
  sdFontTestIo = {};
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  expectIo(1, 202, 202, 101 * (sizeof(EpdGlyph) + BITMAP_BYTES));
  ASSERT_TRUE(pageIntact(font, FIRST, 100, 1, 4));
  font.clearCache();
  sdFontTestIo = {};
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  expectIo(0, 0, 0, 0);
}

TEST(SdCardFontTest, MetadataOnlyCannotSatisfyABitmapPrewarm) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto text = page(FIRST, 100);
  sdFontTestIo = {};
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, true, false, false));
  expectIo(1, 2, 101, 101 * sizeof(EpdGlyph));
  font.clearCache();
  sdFontTestIo = {};
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  expectIo(1, 4, 202, 101 * (sizeof(EpdGlyph) + BITMAP_BYTES));
  ASSERT_TRUE(pageIntact(font, FIRST, 100));
}

TEST(SdCardFontTest, MemoryPressureReleasesPrefetchAndRebuildsCorrectly) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto text = page(FIRST, 100);
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  FaultGuard guard;
  ESP.freeHeap = 39 * 1024;
  font.clearCache();
  EXPECT_EQ(0U, residentCount(font));
  sdFontTestIo = {};
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  expectIo(1, 4, 202, 101 * (sizeof(EpdGlyph) + BITMAP_BYTES));
  ASSERT_TRUE(pageIntact(font, FIRST, 100));
}

TEST(SdCardFontTest, FailedOpenSeekOrReadNeverPublishesPartialGlyphData) {
  makeFont();
  for (uint8_t fault = 0; fault < 4; ++fault) {
    SCOPED_TRACE(fault);
    SdCardFont font;
    ASSERT_TRUE(font.load("fixture"));
    const auto text = page(FIRST, 100);
    FaultGuard guard;
    sdFontTestIo = {};
    if (fault == 0) sdFontTestFailOpen = 1;
    if (fault == 1) sdFontTestFailSeek = 1;
    if (fault == 2) sdFontTestShortRead = 1;    // metadata
    if (fault == 3) sdFontTestShortRead = 102;  // bitmap
    EXPECT_EQ(101, font.prewarm(text.c_str(), 1, false, false, false));
    EXPECT_EQ(0U, residentCount(font));
    sdFontTestFailOpen = sdFontTestFailSeek = sdFontTestShortRead = 0;
    sdFontTestIo = {};
    ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
    expectIo(1, 4, 202, 101 * (sizeof(EpdGlyph) + BITMAP_BYTES));
    ASSERT_TRUE(pageIntact(font, FIRST, 100));
  }
}

TEST(SdCardFontTest, WarmPageAllocatesOnlyTheBoundedCodepointScratch) {
  makeFont(4);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto text = page(FIRST, 100);
  ASSERT_EQ(0, font.prewarm(text.c_str(), 0x0F, false, false, false));
  font.clearCache();
  sdFontTestAllocations = {};
  sdFontTestIo = {};
  ASSERT_EQ(0, font.prewarm(text.c_str(), 0x0F, false, false, false));
  EXPECT_EQ(1U, sdFontTestAllocations.attempts);
  EXPECT_EQ(SdCardFont::MAX_PAGE_GLYPHS * sizeof(uint32_t), sdFontTestAllocations.requestedBytes);
  expectIo(0, 0, 0, 0);
  ASSERT_TRUE(pageIntact(font, FIRST, 100, 4));
}

TEST(SdCardFontTest, ScratchAllocationFailurePreservesTheResidentPage) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto oldPage = page(FIRST, 100);
  const auto newPage = page(FIRST + 100, 100);
  ASSERT_EQ(0, font.prewarm(oldPage.c_str(), 1, false, false, false));
  failNextArraySize = SdCardFont::MAX_PAGE_GLYPHS * sizeof(uint32_t);
  sdFontTestIo = {};
  EXPECT_EQ(-1, font.prewarm(newPage.c_str(), 1, false, false, false));
  EXPECT_EQ(0U, failNextArraySize);
  expectIo(0, 0, 0, 0);
  ASSERT_TRUE(pageIntact(font, FIRST, 100));
  ASSERT_EQ(0, font.prewarm(newPage.c_str(), 1, false, false, false));
  ASSERT_TRUE(pageIntact(font, FIRST + 100, 100));
}

TEST(SdCardFontTest, IdenticalStyleIntervalsShareOneCheckedAllocation) {
  makeFont(4);
  SdCardFont font;
  sdFontTestAllocations = {};
  ASSERT_TRUE(font.load("fixture"));
  EXPECT_EQ(1U, sdFontTestAllocations.attempts);
  EXPECT_EQ(12U, sdFontTestAllocations.requestedBytes);  // two compact 6-byte intervals
  ASSERT_EQ(0, font.prewarm(page(FIRST, 5).c_str(), 0x0f, false, false, false));
  EXPECT_TRUE(pageIntact(font, FIRST, 5, 4));
  font.releaseResidentCaches();
  ASSERT_EQ(0, font.prewarm(page(FIRST + 10, 5).c_str(), 0x0f, false, false, false));
  EXPECT_TRUE(pageIntact(font, FIRST + 10, 5, 4));
}

TEST(SdCardFontTest, InvalidLaterStyleReleasesSharedIntervalsAndCanReload) {
  makeFont(4);
  const size_t thirdStyleToc = 32 + 2 * 32;
  // An out-of-range table rejects the load after style1 has borrowed style0's table.
  put32(thirdStyleToc + 24, static_cast<uint32_t>(sdFontTestFile.size() + 1));
  SdCardFont font;
  EXPECT_FALSE(font.load("fixture"));
  makeFont(4);
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm(page(FIRST, 5).c_str(), 0x0f, false, false, false));
  EXPECT_TRUE(pageIntact(font, FIRST, 5, 4));
}
