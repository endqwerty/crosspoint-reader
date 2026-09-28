#include <HalStorage.h>
#include <SdCardFont.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "FontFixture.h"
#include "HostAllocations.h"

namespace {
using namespace sd_font_fixture;

// Six Latin glyphs 'A'..'F' plus U+FFFD, with left classes for 'A' and 'C',
// right classes for 'B' and 'D', and a class matrix whose top-left 2×2 corner
// holds their pairs. `extraEntries` appends entries for U+0100 onwards to both
// class tables, so the tables span several read blocks. They use `extraClass`,
// or classes 3..classCount in turn when it is 0. A `classCount` of 0 writes no
// kern data. `ligature` adds one pair, E+F -> A.
void makeKerningFont(uint16_t extraEntries = 0, uint8_t classCount = 2, uint8_t extraClass = 2, bool ligature = false) {
  constexpr uint32_t KERN_GLYPHS = 7;
  constexpr size_t GLYPH_OFFSET = 64 + 24;
  constexpr size_t KERN_OFFSET = GLYPH_OFFSET + KERN_GLYPHS * sizeof(EpdGlyph);
  constexpr uint8_t LEFT[][2] = {{'A', 1}, {'C', 2}};
  constexpr uint8_t RIGHT[][2] = {{'B', 1}, {'D', 2}};
  constexpr int8_t MATRIX[2][2] = {{-3, 0}, {4, -5}};
  const uint16_t entries = classCount ? 2 + extraEntries : 0;
  const size_t matrixBytes = static_cast<size_t>(classCount) * classCount;
  const size_t ligatureOffset = KERN_OFFSET + entries * 3 * 2 + matrixBytes;
  const size_t bitmapOffset = ligatureOffset + (ligature ? 8 : 0);
  sdFontTestFile.assign(bitmapOffset + KERN_GLYPHS * BITMAP_BYTES, 0);
  std::memcpy(sdFontTestFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  sdFontTestFile[12] = 1;
  put32(36, 2);
  put32(40, KERN_GLYPHS);
  sdFontTestFile[44] = 32;
  put16(45, 32);
  put16(49, entries);  // left class entries
  put16(51, entries);  // right class entries
  sdFontTestFile[53] = classCount;
  sdFontTestFile[54] = classCount;
  sdFontTestFile[55] = ligature ? 1 : 0;
  put32(56, 64);
  put32(64, 'A');
  put32(68, 'F');
  put32(76, 0xFFFD);
  put32(80, 0xFFFD);
  put32(84, KERN_GLYPHS - 1);
  for (uint32_t i = 0; i < KERN_GLYPHS; ++i) {
    EpdGlyph glyph{};
    glyph.width = 32;
    glyph.height = 32;
    glyph.advanceX = 32 << 4;
    glyph.top = 32;
    glyph.dataLength = BITMAP_BYTES;
    glyph.dataOffset = i * BITMAP_BYTES;
    std::memcpy(sdFontTestFile.data() + GLYPH_OFFSET + i * sizeof(glyph), &glyph, sizeof(glyph));
  }
  size_t at = KERN_OFFSET;
  for (const auto* table : {LEFT, RIGHT}) {
    if (classCount == 0) break;
    for (size_t i = 0; i < 2; ++i, at += 3) {
      put16(at, table[i][0]);
      sdFontTestFile[at + 2] = table[i][1];
    }
    for (uint16_t i = 0; i < extraEntries; ++i, at += 3) {
      put16(at, 0x100 + i);
      sdFontTestFile[at + 2] = extraClass ? extraClass : 3 + i % (classCount - 2);
    }
  }
  for (size_t row = 0; classCount > 0 && row < 2; ++row) {
    std::memcpy(sdFontTestFile.data() + at + row * classCount, MATRIX[row], sizeof(MATRIX[row]));
  }
  if (ligature) {
    put32(ligatureOffset, 'E' << 16 | 'F');
    put32(ligatureOffset + 4, 'A');
  }
}

std::string latinPage(const char* ascii, uint32_t first, uint32_t count) {
  std::string text = ascii;
  for (uint32_t cp = first; cp < first + count; ++cp) {
    text.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    text.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  return text;
}

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

TEST(SdCardFontTest, PagesKernWithTheFontsClassMatrix) {
  makeKerningFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, true, false));
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(0, epd->getKerning('A', 'D'));
  EXPECT_EQ(4, epd->getKerning('C', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
  EXPECT_EQ(0, epd->getKerning('B', 'D'));
  EXPECT_EQ(0, epd->getKerning('E', 'B'));
}

TEST(SdCardFontTest, KernRequestsServedFromAKernFreeMiniStillKern) {
  makeKerningFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, false, false));  // kern-free prewarm, e.g. a UI string
  struct Step {
    const char* text;
    uint32_t left, right;
    int8_t kern;
  };
  // A subset without kerning pairs, then subsets whose pairs the earlier ones did not cover.
  for (const Step& step : {Step{"EF", 'E', 'F', 0}, Step{"AB", 'A', 'B', -3}, Step{"CD", 'C', 'D', -5}}) {
    ASSERT_EQ(0, font.prewarm(step.text, 1, false, true, false));
    EXPECT_EQ(step.kern, font.getEpdFont()->getKerning(step.left, step.right)) << step.text;
  }
}

TEST(SdCardFontTest, RedrawsAfterAKernFreeRebuildStillKern) {
  makeKerningFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm("ABCD", 1, false, true, false));
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, false, false));  // kern-free rebuild, e.g. a UI string
  ASSERT_EQ(0, font.prewarm("ABCD", 1, false, true, false));     // the page again, served from that cache
  EXPECT_EQ(-3, font.getEpdFont()->getKerning('A', 'B'));
  EXPECT_EQ(-5, font.getEpdFont()->getKerning('C', 'D'));
}

TEST(SdCardFontTest, ClassIdsPastTheMatrixAreUnkerned) {
  makeKerningFont(1, 2, 250);  // U+0100 claims class 250 in a 2×2 matrix
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(1, font.prewarm(latinPage("ABCD", 0x100, 1).c_str(), 1, false, true, false));  // U+0100 has no glyph
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(0, epd->getKerning('A', 0x100));
  EXPECT_EQ(0, epd->getKerning(0x100, 'B'));
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
}

TEST(SdCardFontTest, PagesCanUseAll255KernClasses) {
  makeKerningFont(253, 255, 0);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(253, font.prewarm(latinPage("ABCD", 0x100, 253).c_str(), 1, false, true, false));
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
  EXPECT_EQ(0, epd->getKerning(0x100, 0x1FC));
}

TEST(SdCardFontTest, AFailedKernBuildKeepsTheLigatures) {
  makeKerningFont(253, 255, 0, true);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  failNextArraySize = 255 * 255;  // the mini kern matrix
  ASSERT_EQ(253, font.prewarm(latinPage("ABCDEF", 0x100, 253).c_str(), 1, false, true, false));
  EXPECT_EQ(0U, failNextArraySize);
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(static_cast<uint32_t>('A'), epd->getLigature('E', 'F'));
  EXPECT_EQ(0, epd->getKerning('A', 'B'));
}

TEST(SdCardFontTest, LigatureRequestsServedFromAKernFreeMiniGetLigatures) {
  makeKerningFont(0, 0, 2, true);  // ligatures, no kern classes
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm("AEF", 1, false, false, false));  // kern-free prewarm, e.g. a UI string
  ASSERT_EQ(0, font.prewarm("EF", 1, false, true, false));
  EXPECT_EQ(static_cast<uint32_t>('A'), font.getEpdFont()->getLigature('E', 'F'));
}

TEST(SdCardFontTest, RedrawingAPrewarmedPageReadsNothing) {
  makeKerningFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  for (const char* text : {"ABCD", "DEF"}) {  // with and without kerning pairs
    font.clearCache();
    ASSERT_EQ(0, font.prewarm(text, 1, false, true, false));
    font.clearCache();
    sdFontTestReads = 0;
    ASSERT_EQ(0, font.prewarm(text, 1, false, true, false));
    EXPECT_EQ(0U, sdFontTestReads) << text;
  }
}

TEST(SdCardFontTest, LaterPagesReadOnlyTheKernClassBlocksTheyUse) {
  makeKerningFont(300);  // five 64-entry blocks per class table
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  sdFontTestReads = 0;
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, true, false));
  const size_t firstReads = sdFontTestReads;
  EXPECT_EQ(-5, font.getEpdFont()->getKerning('C', 'D'));

  font.releaseResidentCaches();
  sdFontTestReads = 0;
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, true, false));
  EXPECT_EQ(firstReads - 8, sdFontTestReads);  // 4 of the 5 blocks skipped in each table
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(4, epd->getKerning('C', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
}

TEST(SdCardFontTest, AdvancesStayCorrectWhenPagesAddCodepointsOutOfOrder) {
  makeFont();
  for (uint32_t i = 0; i < GLYPHS; ++i) {
    put16(64 + 24 + i * sizeof(EpdGlyph) + offsetof(EpdGlyph, advanceX), (20 + i % 13) << 4);
  }
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  std::vector<uint32_t> added;
  for (uint32_t block : {3U, 0U, 5U, 1U, 4U, 2U}) {  // each merge lands before, between or after earlier ones
    ASSERT_EQ(0, font.buildAdvanceTable(page(FIRST + block * 80, 80).c_str(), 1));
    for (uint32_t cp = FIRST + block * 80; cp < FIRST + block * 80 + 80; ++cp) added.push_back(cp);
    for (uint32_t cp : added) {
      ASSERT_EQ((20 + (cp - FIRST) % 13) << 4, font.getAdvance(cp, 0)) << std::hex << cp;
    }
  }
}

TEST(SdCardFontTest, AdvanceMergesPastTheCapKeepTheLowestCodepoints) {
  constexpr uint32_t CACHE_LIMIT = 768;  // SdCardFont::ADVANCE_CACHE_LIMIT
  constexpr uint32_t GLYPH_COUNT = 1001;
  makeFont(1, GLYPH_COUNT);
  for (uint32_t i = 0; i < GLYPH_COUNT; ++i) {
    put16(64 + 24 + i * sizeof(EpdGlyph) + offsetof(EpdGlyph, advanceX), (20 + i % 13) << 4);
  }
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  // Reference: the sorted union of every page, truncated to the cap; a full table takes nothing more.
  std::vector<uint32_t> expected;
  for (uint32_t block : {6U, 1U, 9U, 3U, 0U, 7U, 4U, 8U, 2U, 5U}) {
    ASSERT_GE(font.buildAdvanceTable(page(FIRST + block * 100, 100).c_str(), 1), 0);
    if (expected.size() < CACHE_LIMIT) {
      for (uint32_t cp = FIRST + block * 100; cp < FIRST + block * 100 + 100; ++cp) expected.push_back(cp);
      std::sort(expected.begin(), expected.end());
      if (expected.size() > CACHE_LIMIT) expected.resize(CACHE_LIMIT);
    }
    for (uint32_t cp = FIRST; cp < FIRST + 1000; ++cp) {
      const bool cached = std::binary_search(expected.begin(), expected.end(), cp);
      ASSERT_EQ(cached ? (20 + (cp - FIRST) % 13) << 4 : 0, font.getAdvance(cp, 0)) << block << " " << std::hex << cp;
    }
  }
}

TEST(SdCardFontTest, AnEmptyOrFailedAdvanceBuildLeavesNoTable) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  font.buildAdvanceTable("", 1);
  EXPECT_FALSE(font.hasAdvanceTable());
  failNextArraySize = (4096 + 2) * sizeof(uint32_t);  // the codepoint scratch
  EXPECT_EQ(-1, font.buildAdvanceTable(page(FIRST, 10).c_str(), 1));
  EXPECT_FALSE(font.hasAdvanceTable());
  ASSERT_EQ(0, font.buildAdvanceTable(page(FIRST, 10).c_str(), 1));
  EXPECT_TRUE(font.hasAdvanceTable());
}
