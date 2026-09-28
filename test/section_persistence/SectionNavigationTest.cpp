#include <gtest/gtest.h>

#include "HostAllocations.h"
#include "SectionPersistenceFixture.h"

namespace {
using namespace epub_page_test;
class SectionNavigation : public testing::Test {
 protected:
  Section section;
  std::string target = "target-" + std::string(128, 'x');
  static constexpr size_t COUNT = SectionPageReader::HEADER_SIZE - 22;
  static constexpr size_t OFFSETS = COUNT + 2;
  uint32_t tables[5]{};
  uint16_t pages = 0;
  void SetUp() override {
    section.build_.reset();
    install(34);
  }
  void TearDown() override {
    captureAllocations = false;
    faults = {};
    io = {};
  }
  template <typename T>
  void patch(size_t at, T value) {
    ASSERT_LE(at + sizeof(value), files[0].size());
    std::memcpy(files[0].data() + at, &value, sizeof(value));
  }
  template <typename T>
  void append(T value) {
    const auto at = files[0].size();
    files[0].resize(at + sizeof(value));
    patch(at, value);
  }
  void install(uint16_t count, bool partial = false) {
    pages = count;
    files[0].assign(SectionPageReader::HEADER_SIZE, 0);
    files[0][0] = partial ? section_test::partialVersion() : section_test::completeVersion();
    patch(COUNT, count);
    tables[0] = files[0].size();
    for (uint32_t i = 0; i < count; ++i) append<uint32_t>(1);
    tables[1] = files[0].size();
    append<uint16_t>(count ? 2 : 0);
    if (count) {
      for (const auto& key : {std::string("skip"), target}) {
        append<uint32_t>(key.size());
        files[0].insert(files[0].end(), key.begin(), key.end());
        append<uint16_t>(key == target ? count - 1 : 0);
      }
    }
    tables[2] = files[0].size();
    append<uint16_t>(count);
    for (uint32_t i = 0; i < count; ++i) append<uint16_t>(i);
    tables[3] = files[0].size();
    for (uint32_t i = 0; i < count; ++i) append<uint16_t>(i);
    tables[4] = files[0].size();
    for (uint32_t i = 0; i < count; ++i) append<uint32_t>(i);
    if (partial) {
      append<uint32_t>(100);
      append<uint32_t>(200);
    }
    for (size_t i = 0; i < 5; ++i) patch(OFFSETS + i * 4, tables[i]);
    fileExists[0] = true;
    faults = {};
    io = {};
  }
  std::optional<uint16_t> lookup(int kind) {
    switch (kind) {
      case 0:
        return section.getCachedPageCount();
      case 1:
        return section.getPageForAnchor(target);
      case 2:
        return section.getPageForParagraphIndex(UINT16_MAX);
      case 3:
        return section.getParagraphIndexForPage(pages - 1);
      default:
        return section.getPageForListItemIndex(UINT16_MAX);
    }
  }
};

TEST_F(SectionNavigation, LongParagraphAndListScansBatchReads) {
  for (int kind : {2, 4}) {
    install(1024);
    ASSERT_EQ(1023, lookup(kind));
    EXPECT_LE(io.reads, 68u);
    RecordProperty(kind == 2 ? "paragraph_reads" : "list_reads", static_cast<int>(io.reads));
  }
}

TEST_F(SectionNavigation, AnchorAndNumericLookupsAllocateNoMemory) {
  for (int kind = 0; kind < 5; ++kind) {
    install(34);
    allocations = {};
    captureAllocations = true;
    auto result = lookup(kind);
    captureAllocations = false;
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(kind == 0 ? 34 : 33, *result);
    EXPECT_EQ(0u, allocations.calls) << kind;
  }
}

TEST_F(SectionNavigation, EveryFailedShortReadAndSeekReturnsNoPosition) {
  for (int kind = 0; kind < 5; ++kind) {
    install(34);
    ASSERT_TRUE(lookup(kind));
    const auto count = io;
    for (int failure = 0; failure < 3; ++failure) {
      const auto calls = failure == 2 ? count.seeks : count.reads;
      for (size_t call = 1; call <= calls; ++call) {
        install(34);
        if (failure == 0)
          faults.failReadCall = call;
        else if (failure == 1)
          faults.shortReadCall = call;
        else
          faults.failSeekCall = call;
        EXPECT_EQ(std::nullopt, lookup(kind)) << kind << ':' << failure << ':' << call;
        EXPECT_EQ(1u, io.injectedFailures);
      }
    }
    install(34);
    faults.failOpen = true;
    EXPECT_EQ(std::nullopt, lookup(kind));
  }
}

TEST_F(SectionNavigation, RejectsOldIncompleteAndShortHeaders) {
  for (int kind = 0; kind < 5; ++kind) {
    for (uint8_t version : {uint8_t(0), uint8_t(1), uint8_t(section_test::completeVersion() - 1)}) {
      install(34);
      files[0][0] = version;
      EXPECT_EQ(std::nullopt, lookup(kind)) << kind << ':' << int(version);
    }
    install(34);
    files[0].resize(SectionPageReader::HEADER_SIZE - 1);
    EXPECT_EQ(std::nullopt, lookup(kind));
  }
}

TEST_F(SectionNavigation, ValidPartialAllowsNavigationButNotChapterTotal) {
  install(34, true);
  EXPECT_EQ(std::nullopt, section.getCachedPageCount());
  for (int kind = 1; kind < 5; ++kind) EXPECT_EQ(33, lookup(kind));
  files[0].pop_back();
  for (int kind = 0; kind < 5; ++kind) EXPECT_EQ(std::nullopt, lookup(kind));
}

TEST_F(SectionNavigation, RejectsTruncatedOverlappingAndOverflowingTables) {
  for (int kind = 0; kind < 5; ++kind) {
    for (size_t table = 0; table < 5; ++table) {
      for (uint32_t offset : {0u, uint32_t(SectionPageReader::HEADER_SIZE - 1), UINT32_MAX - 1}) {
        install(34);
        patch(OFFSETS + table * 4, offset);
        EXPECT_EQ(std::nullopt, lookup(kind)) << kind << ':' << table << ':' << offset;
      }
    }
    install(34);
    files[0].pop_back();
    EXPECT_EQ(std::nullopt, lookup(kind));
  }
  for (int kind : {2, 3, 4}) {
    install(34);
    patch<uint16_t>(tables[2], 35);
    EXPECT_EQ(std::nullopt, lookup(kind));
  }
}

TEST_F(SectionNavigation, ParagraphAndListKeepFirstDuplicateAndLastFallback) {
  install(34);
  for (uint16_t i = 0; i < 34; ++i) {
    patch<uint16_t>(tables[2] + 2 + i * 2, i < 1 ? 0 : i < 33 ? 5 : 9);
    patch<uint16_t>(tables[3] + i * 2, i < 1 ? 0 : i < 33 ? 5 : 9);
  }
  for (uint16_t value : {0, 1, 5, 6, 9, 10, 65535}) {
    const uint16_t expected = value == 0 ? 0 : value <= 5 ? 1 : 33;
    EXPECT_EQ(expected, section.getPageForParagraphIndex(value));
    EXPECT_EQ(expected, section.getPageForListItemIndex(value));
  }
  EXPECT_EQ(5, section.getParagraphIndexForPage(16));
  EXPECT_EQ(std::nullopt, section.getParagraphIndexForPage(34));
}

TEST_F(SectionNavigation, EmptyAndMaximumPageCountsRemainBounded) {
  install(0);
  EXPECT_EQ(0, section.getCachedPageCount());
  for (int kind = 1; kind < 5; ++kind) EXPECT_EQ(std::nullopt, lookup(kind));
  install(UINT16_MAX);
  for (int kind = 0; kind < 5; ++kind) EXPECT_EQ(kind == 0 ? 65535 : 65534, lookup(kind));
}

TEST_F(SectionNavigation, AnchorBoundsAndPageNumbersCannotEscapeTheirTables) {
  for (uint32_t length : {4096u, UINT32_MAX}) {
    install(34);
    patch(tables[1] + 2, length);
    allocations = {};
    captureAllocations = true;
    const auto result = section.getPageForAnchor(target);
    captureAllocations = false;
    EXPECT_EQ(std::nullopt, result);
    EXPECT_EQ(0u, allocations.calls);
  }
  install(34);
  patch<uint16_t>(tables[1], UINT16_MAX);
  EXPECT_EQ(std::nullopt, section.getPageForAnchor(target));
  install(34);
  patch<uint16_t>(tables[2] - 2, pages);
  EXPECT_EQ(std::nullopt, section.getPageForAnchor(target));
  install(34);
  EXPECT_EQ(std::nullopt, section.getPageForAnchor("missing"));
  std::string wrong = target;
  wrong[70] = 'y';
  EXPECT_EQ(std::nullopt, section.getPageForAnchor(wrong));
  target.clear();
  install(34);
  EXPECT_EQ(33, section.getPageForAnchor(target));
  target = "skip";
  install(34);
  patch<uint16_t>(tables[1] + 2 + 4 + target.size(), 0);
  EXPECT_EQ(0, section.getPageForAnchor(target));
}
}  // namespace
