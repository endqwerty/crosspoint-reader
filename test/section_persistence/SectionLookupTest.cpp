#include <gtest/gtest.h>

#include <limits>

#include "HostAllocations.h"
#include "SectionPersistenceFixture.h"

namespace {
using namespace epub_page_test;

class SectionLookup : public ::testing::Test {
 protected:
  Section section;
  static constexpr size_t COUNT_OFFSET = SectionPageReader::HEADER_SIZE - 5 * sizeof(uint32_t) - sizeof(uint16_t);
  static constexpr size_t VISIBLE_OFFSET = SectionPageReader::HEADER_SIZE - sizeof(uint32_t);

  void SetUp() override {
    section.build_.reset();
    for (auto& bytes : files) bytes.clear();
    fileExists = {false, true, false, false};
    faults = {};
    io = {};
  }
  void TearDown() override {
    faults = {};
    io = {};
    captureAllocations = false;
    fileExists = {true, true, false, false};
  }
  template <typename T>
  static void patch(size_t position, T value) {
    ASSERT_LE(position + sizeof(value), files[0].size());
    std::memcpy(files[0].data() + position, &value, sizeof(value));
  }
  void install(const std::vector<uint32_t>& offsets, bool partial = false) {
    const uint32_t tableOffset = SectionPageReader::HEADER_SIZE + 8;
    files[0].assign(tableOffset + offsets.size() * sizeof(uint32_t), 0);
    files[0][0] = partial ? section_test::partialVersion() : section_test::completeVersion();
    patch(COUNT_OFFSET, static_cast<uint16_t>(offsets.size()));
    patch(VISIBLE_OFFSET, tableOffset);
    if (!offsets.empty()) std::memcpy(files[0].data() + tableOffset, offsets.data(), offsets.size() * sizeof(uint32_t));
    fileExists[0] = true;
    io = {};
    faults = {};
  }
};

TEST_F(SectionLookup, SelectsPreviousPageAndHonorsFirstOrLastDuplicateOffset) {
  install({0, 100, 100, 200});
  for (bool first : {false, true}) {
    EXPECT_EQ(0, section.getPageForVisibleTextOffset(0, first));
    EXPECT_EQ(0, section.getPageForVisibleTextOffset(99, first));
    EXPECT_EQ(first ? 1 : 2, section.getPageForVisibleTextOffset(100, first));
    EXPECT_EQ(2, section.getPageForVisibleTextOffset(101, first));
    EXPECT_EQ(2, section.getPageForVisibleTextOffset(199, first));
    EXPECT_EQ(3, section.getPageForVisibleTextOffset(200, first));
    EXPECT_EQ(3, section.getPageForVisibleTextOffset(999, first));
  }
}

TEST_F(SectionLookup, PreservesDuplicateSelectionAcrossChunkBoundaries) {
  std::vector<uint32_t> offsets(34, 100);
  offsets[0] = 0;
  offsets.back() = 200;
  install(offsets);
  EXPECT_EQ(1, section.getPageForVisibleTextOffset(100, true));
  EXPECT_EQ(32, section.getPageForVisibleTextOffset(100));
  EXPECT_EQ(32, section.getPageForVisibleTextOffset(199));
  EXPECT_EQ(33, section.getPageForVisibleTextOffset(200));
}

TEST_F(SectionLookup, PartialCacheRejectsOffsetsBeyondItsKnownRange) {
  install({10, 100, 100, 200}, true);
  EXPECT_EQ(0, section.getPageForVisibleTextOffset(0));
  EXPECT_EQ(1, section.getPageForVisibleTextOffset(100, true));
  EXPECT_EQ(2, section.getPageForVisibleTextOffset(100));
  EXPECT_EQ(2, section.getPageForVisibleTextOffset(199));
  EXPECT_EQ(3, section.getPageForVisibleTextOffset(200));
  EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(201));
}

TEST_F(SectionLookup, ActiveBuildUsesKnownOffsetsWithoutOpeningStorage) {
  section.build_ = std::make_unique<Section::BuildContext>();
  section.build_->lut = {{1, 0, 0, 0}, {2, 0, 0, 100}, {3, 0, 0, 100}, {4, 0, 0, 200}};
  faults.failOpen = true;
  EXPECT_EQ(1, section.getPageForVisibleTextOffset(100, true));
  EXPECT_EQ(2, section.getPageForVisibleTextOffset(100));
  EXPECT_EQ(2, section.getPageForVisibleTextOffset(199));
  EXPECT_EQ(3, section.getPageForVisibleTextOffset(200));
  EXPECT_EQ(0u, io.opens);
  EXPECT_EQ(0u, io.reads);
}

TEST_F(SectionLookup, ActiveBuildFallsBackToLongerCommittedPartial) {
  install({0, 100, 200, 300}, true);
  section.build_ = std::make_unique<Section::BuildContext>();
  section.build_->lut = {{1, 0, 0, 0}, {2, 0, 0, 100}};
  EXPECT_EQ(2, section.getPageForVisibleTextOffset(250));
  EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(301));
  EXPECT_EQ(2u, io.opens);
}

TEST_F(SectionLookup, LongChapterLookupHasBoundedReadCountAndNoHeapAllocation) {
  std::vector<uint32_t> offsets(1024);
  for (size_t i = 0; i < offsets.size(); ++i) offsets[i] = static_cast<uint32_t>(i * 100);
  install(offsets);
  allocations = {};
  captureAllocations = true;
  const auto result = section.getPageForVisibleTextOffset(offsets.back());
  captureAllocations = false;
  ASSERT_EQ(1023, result);
  EXPECT_EQ(67u, io.reads);
  EXPECT_EQ(3u, io.seeks);
  EXPECT_EQ(1u, io.opens);
  EXPECT_EQ(7u + offsets.size() * sizeof(uint32_t), io.bytes);
  EXPECT_EQ(0u, allocations.calls);
}

TEST_F(SectionLookup, MaximumPageCountDoesNotWrapTheChunkCursor) {
  std::vector<uint32_t> offsets(std::numeric_limits<uint16_t>::max());
  for (size_t i = 0; i < offsets.size(); ++i) offsets[i] = static_cast<uint32_t>(i);
  install(offsets);
  EXPECT_EQ(65534, section.getPageForVisibleTextOffset(offsets.back()));
  EXPECT_EQ(4099u, io.reads);
  EXPECT_EQ(7u + offsets.size() * sizeof(uint32_t), io.bytes);
}

TEST_F(SectionLookup, EveryFailedReadReturnsNoPositionAndStopsImmediately) {
  std::vector<uint32_t> offsets(40);
  for (size_t i = 0; i < offsets.size(); ++i) offsets[i] = static_cast<uint32_t>(i);
  install(offsets);
  ASSERT_EQ(39, section.getPageForVisibleTextOffset(39));
  const size_t reads = io.reads;
  ASSERT_EQ(6u, reads);
  for (size_t failed = 1; failed <= reads; ++failed) {
    SCOPED_TRACE(failed);
    io = {};
    faults.failReadCall = failed;
    EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(39));
    EXPECT_EQ(failed, io.reads);
    EXPECT_EQ(1u, io.injectedFailures);
  }
}

TEST_F(SectionLookup, EveryShortReadReturnsNoPositionAndStopsImmediately) {
  std::vector<uint32_t> offsets(40);
  for (size_t i = 0; i < offsets.size(); ++i) offsets[i] = static_cast<uint32_t>(i);
  install(offsets);
  for (size_t shortened = 1; shortened <= 6; ++shortened) {
    SCOPED_TRACE(shortened);
    io = {};
    faults.shortReadCall = shortened;
    EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(39));
    EXPECT_EQ(shortened, io.reads);
    EXPECT_EQ(1u, io.injectedFailures);
  }
}

TEST_F(SectionLookup, EveryFailedSeekReturnsNoPositionWithoutReadingTheWrongLocation) {
  install({0, 100, 200});
  for (size_t failed = 1; failed <= 3; ++failed) {
    SCOPED_TRACE(failed);
    io = {};
    faults.failSeekCall = failed;
    EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(200));
    EXPECT_EQ(failed, io.seeks);
    EXPECT_EQ(failed, io.reads);
    EXPECT_EQ(1u, io.injectedFailures);
  }
}

TEST_F(SectionLookup, RejectsMissingEmptyIncompleteAndShortCaches) {
  EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(0));
  install({});
  EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(0));
  install({0, 100, 200});
  files[0][0] = section_test::incompleteVersion();
  EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(200));
  install({0, 100, 200});
  files[0].resize(SectionPageReader::HEADER_SIZE - 1);
  io = {};
  EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(200));
  EXPECT_EQ(0u, io.reads);
}

TEST_F(SectionLookup, RejectsTruncatedTablesAndOverflowingLookupOffsets) {
  install({0, 100, 200});
  files[0].pop_back();
  EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(200));
  for (uint32_t invalid : {0u, SectionPageReader::HEADER_SIZE - 1, std::numeric_limits<uint32_t>::max() - 7,
                           std::numeric_limits<uint32_t>::max()}) {
    SCOPED_TRACE(invalid);
    install({0, 100, 200});
    patch(VISIBLE_OFFSET, invalid);
    EXPECT_EQ(std::nullopt, section.getPageForVisibleTextOffset(200));
    EXPECT_EQ(3u, io.reads);
    EXPECT_EQ(2u, io.seeks);
  }
}

TEST_F(SectionLookup, PageOffsetRejectsTruncatedWholeTableEvenWhenRequestedEntryRemains) {
  install({0, 100, 200});
  files[0].pop_back();
  EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(0));
  EXPECT_EQ(3u, io.reads);
  EXPECT_EQ(2u, io.seeks);
}

TEST_F(SectionLookup, PageOffsetReadsZeroAndMaximumValuesFromCompleteAndPartialCaches) {
  for (bool partial : {false, true}) {
    install({0, 100, std::numeric_limits<uint32_t>::max()}, partial);
    EXPECT_EQ(0u, section.getVisibleTextOffsetForPage(0));
    EXPECT_EQ(100u, section.getVisibleTextOffsetForPage(1));
    EXPECT_EQ(std::numeric_limits<uint32_t>::max(), section.getVisibleTextOffsetForPage(2));
    EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(3));
  }
}

TEST_F(SectionLookup, PageOffsetFromActiveBuildNeedsNoStorage) {
  section.build_ = std::make_unique<Section::BuildContext>();
  section.build_->lut = {{1, 0, 0, 0}, {2, 0, 0, 100}, {3, 0, 0, 200}};
  faults.failOpen = true;
  EXPECT_EQ(0u, section.getVisibleTextOffsetForPage(0));
  EXPECT_EQ(100u, section.getVisibleTextOffsetForPage(1));
  EXPECT_EQ(200u, section.getVisibleTextOffsetForPage(2));
  EXPECT_EQ(0u, io.opens);
  EXPECT_EQ(0u, io.reads);
  EXPECT_EQ(0u, io.injectedFailures);
}

TEST_F(SectionLookup, PageOffsetBeyondActiveBuildFallsBackToLongerPartial) {
  install({0, 100, 200}, true);
  section.build_ = std::make_unique<Section::BuildContext>();
  section.build_->lut = {{1, 0, 0, 7}, {2, 0, 0, 123}};
  EXPECT_EQ(123u, section.getVisibleTextOffsetForPage(1));
  EXPECT_EQ(0u, io.opens);
  EXPECT_EQ(200u, section.getVisibleTextOffsetForPage(2));
  EXPECT_EQ(1u, io.opens);
  EXPECT_EQ(4u, io.reads);
  EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(3));
}

TEST_F(SectionLookup, PageOffsetStopsAtEveryFailedOrShortRead) {
  install({0, 100, 200});
  for (bool shortRead : {false, true}) {
    for (size_t failed = 1; failed <= 4; ++failed) {
      SCOPED_TRACE(shortRead);
      SCOPED_TRACE(failed);
      io = {};
      faults = {};
      if (shortRead)
        faults.shortReadCall = failed;
      else
        faults.failReadCall = failed;
      EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(2));
      EXPECT_EQ(failed, io.reads);
      EXPECT_EQ(1u, io.injectedFailures);
    }
  }
}

TEST_F(SectionLookup, PageOffsetStopsAtEveryFailedSeek) {
  install({0, 100, 200});
  for (size_t failed = 1; failed <= 3; ++failed) {
    SCOPED_TRACE(failed);
    io = {};
    faults.failSeekCall = failed;
    EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(2));
    EXPECT_EQ(failed, io.seeks);
    EXPECT_EQ(failed, io.reads);
    EXPECT_EQ(1u, io.injectedFailures);
  }
}

TEST_F(SectionLookup, PageOffsetRejectsMissingEmptyIncompleteAndShortCaches) {
  EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(0));
  install({});
  EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(0));
  install({0, 100, 200});
  faults.failOpen = true;
  EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(0));
  EXPECT_EQ(1u, io.injectedFailures);
  faults = {};
  files[0][0] = section_test::incompleteVersion();
  EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(0));
  install({0, 100, 200});
  files[0].resize(SectionPageReader::HEADER_SIZE - 1);
  io = {};
  EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(0));
  EXPECT_EQ(0u, io.reads);
}

TEST_F(SectionLookup, PageOffsetMaximumPageCountKeepsConstantIoAndRejectsOutOfRange) {
  std::vector<uint32_t> offsets(std::numeric_limits<uint16_t>::max());
  for (size_t i = 0; i < offsets.size(); ++i) offsets[i] = static_cast<uint32_t>(i * 100);
  install(offsets);
  EXPECT_EQ(offsets.back(), section.getVisibleTextOffsetForPage(65534));
  EXPECT_EQ(1u, io.opens);
  EXPECT_EQ(4u, io.reads);
  EXPECT_EQ(3u, io.seeks);
  EXPECT_EQ(11u, io.bytes);
  io = {};
  EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(65535));
  EXPECT_EQ(2u, io.reads);
  EXPECT_EQ(1u, io.seeks);
}

TEST_F(SectionLookup, PageOffsetRejectsOverflowingOffsetsBeforeSeekingToTheEntry) {
  for (uint32_t invalid : {0u, SectionPageReader::HEADER_SIZE - 1, std::numeric_limits<uint32_t>::max() - 7,
                           std::numeric_limits<uint32_t>::max()}) {
    for (uint16_t page : {0, 2}) {
      SCOPED_TRACE(invalid);
      SCOPED_TRACE(page);
      install({0, 100, 200});
      patch(VISIBLE_OFFSET, invalid);
      EXPECT_EQ(std::nullopt, section.getVisibleTextOffsetForPage(page));
      EXPECT_EQ(3u, io.reads);
      EXPECT_EQ(2u, io.seeks);
    }
  }
}
}  // namespace
