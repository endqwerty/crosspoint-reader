#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "lib/Epub/Epub/ReferencePages.h"

TEST(ReferencePagesTest, EmptyAndTinyBooks) {
  EXPECT_EQ(ReferencePages::count(0), 0);
  EXPECT_EQ(ReferencePages::pageFor(0.5, 0), 0);
  EXPECT_EQ(ReferencePages::startOffset(1, 0, 0), 0);
  EXPECT_EQ(ReferencePages::startOffset(1, 2048, 0), 0);
  for (uint32_t bytes : {1u, 1023u, 1024u, 2048u, 3071u}) {
    EXPECT_EQ(ReferencePages::count(bytes), 1);
    EXPECT_EQ(ReferencePages::pageFor(1, 1), 1);
    EXPECT_EQ(ReferencePages::startOffset(1, bytes, 1), 0);
  }
}

TEST(ReferencePagesTest, CountRoundsHalfUpWithoutOverflow) {
  EXPECT_EQ(ReferencePages::count(3072), 2);
  EXPECT_EQ(ReferencePages::count(5120), 3);
  EXPECT_EQ(ReferencePages::count(std::numeric_limits<uint32_t>::max()), 2097152);
}

TEST(ReferencePagesTest, ProgressClampsAndUsesOneBasedPages) {
  EXPECT_EQ(ReferencePages::pageFor(-1, 4), 1);
  EXPECT_EQ(ReferencePages::pageFor(0, 4), 1);
  EXPECT_EQ(ReferencePages::pageFor(0.249, 4), 1);
  EXPECT_EQ(ReferencePages::pageFor(0.25, 4), 2);
  EXPECT_EQ(ReferencePages::pageFor(0.999, 4), 4);
  EXPECT_EQ(ReferencePages::pageFor(1, 4), 4);
  EXPECT_EQ(ReferencePages::pageFor(2, 4), 4);
  EXPECT_EQ(ReferencePages::pageFor(std::numeric_limits<double>::quiet_NaN(), 4), 1);
  EXPECT_EQ(ReferencePages::pageFor(std::numeric_limits<double>::infinity(), 4), 4);
  EXPECT_EQ(ReferencePages::pageFor(0.29, 100), 30);
  EXPECT_EQ(ReferencePages::pageFor(std::nextafter(0.29, 0.0), 100), 29);
}

TEST(ReferencePagesTest, StartOffsetsClampAndUseWideProducts) {
  EXPECT_EQ(ReferencePages::startOffset(0, 5001, 2), 0);
  EXPECT_EQ(ReferencePages::startOffset(1, 5001, 2), 0);
  EXPECT_EQ(ReferencePages::startOffset(2, 5001, 2), 2501);
  EXPECT_EQ(ReferencePages::startOffset(3, 5001, 2), 2501);
  constexpr uint32_t bytes = std::numeric_limits<uint32_t>::max();
  const auto pages = ReferencePages::count(bytes);
  EXPECT_LT(ReferencePages::startOffset(pages, bytes, pages), bytes);
  EXPECT_GT(ReferencePages::startOffset(pages, bytes, pages), bytes - 4096);
}

TEST(ReferencePagesTest, ProgressIsMonotonic) {
  uint32_t previous = 1;
  for (int step = 0; step <= 10000; ++step) {
    const auto page = ReferencePages::pageFor(step / 10000.0, 487);
    EXPECT_GE(page, previous);
    EXPECT_LE(page, 487);
    previous = page;
  }
}

TEST(ReferencePagesTest, PageStartsRoundTrip) {
  for (uint32_t bytes : {1u, 3072u, 5001u, 204800u, 1000003u, std::numeric_limits<uint32_t>::max()}) {
    const auto pages = ReferencePages::count(bytes);
    uint32_t previousOffset = 0;
    for (uint32_t page = 1; page <= pages; ++page) {
      const auto offset = ReferencePages::startOffset(page, bytes, pages);
      ASSERT_GE(offset, previousOffset);
      ASSERT_LT(offset, bytes);
      ASSERT_EQ(ReferencePages::pageFor(static_cast<double>(offset) / bytes, pages), page)
          << "bytes=" << bytes << " offset=" << offset;
      previousOffset = offset;
    }
  }
}

TEST(ReferencePagesTest, EndPageOwnsItsUpperBoundary) {
  EXPECT_EQ(ReferencePages::pageForEnd(0.5, 0), 0);
  EXPECT_EQ(ReferencePages::pageForEnd(-1, 4), 1);
  EXPECT_EQ(ReferencePages::pageForEnd(0, 4), 1);
  EXPECT_EQ(ReferencePages::pageForEnd(0.1, 4), 1);
  EXPECT_EQ(ReferencePages::pageForEnd(0.25, 4), 1);
  EXPECT_EQ(ReferencePages::pageForEnd(0.2500001, 4), 2);
  EXPECT_EQ(ReferencePages::pageForEnd(1, 4), 4);
  EXPECT_EQ(ReferencePages::pageForEnd(std::numeric_limits<double>::quiet_NaN(), 4), 1);
}

TEST(ReferencePagesTest, JumpingToAPageNeverShowsAnEarlierPage) {
  // A screen page that starts at the page's first byte and holds at least that
  // byte ends at or after startOffset + 1.
  for (uint32_t bytes : {1u, 3072u, 5001u, 204800u, 1000003u, std::numeric_limits<uint32_t>::max() - 1}) {
    const auto pages = ReferencePages::count(bytes);
    for (uint32_t page = 1; page <= pages; ++page) {
      const auto offset = ReferencePages::startOffset(page, bytes, pages);
      ASSERT_EQ(ReferencePages::pageForEnd((static_cast<double>(offset) + 1) / bytes, pages), page)
          << "bytes=" << bytes << " page=" << page;
    }
  }
}

TEST(ReferencePagesTest, EvaluatesAtCompileTime) {
  static_assert(ReferencePages::count(0) == 0);
  static_assert(ReferencePages::count(3072) == 2);
  static_assert(ReferencePages::pageFor(0.5, 2) == 2);
  static_assert(ReferencePages::pageForEnd(0.5, 2) == 1);
  static_assert(ReferencePages::startOffset(2, 5001, 2) == 2501);
}
