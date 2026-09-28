#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

#include "ChunkedVector.h"
#include "ScopedAllocationFailure.h"

namespace {

template <typename V>
void fillAndCheck(V& v, const size_t count) {
  for (size_t i = 0; i < count; ++i) {
    ASSERT_TRUE(v.push_back(static_cast<int>(i))) << "at " << i;
  }
  ASSERT_EQ(v.size(), count);
  for (size_t i = 0; i < count; ++i) {
    EXPECT_EQ(v[i], static_cast<int>(i)) << "at " << i;
  }
  size_t i = 0;
  for (const int x : v) {
    EXPECT_EQ(x, static_cast<int>(i++));
  }
  EXPECT_EQ(i, count);
}

}  // namespace

TEST(ChunkedVectorTest, GrowingChunksHoldEveryIndexUpToCapacity) {
  // Chunks of 4, 8, 16, then 16 each: 28 + 3 * 16.
  using V = ChunkedVector<int, 4, 16, 6>;
  static_assert(V::maxSize() == 76);
  V v;
  fillAndCheck(v, V::maxSize());
  EXPECT_FALSE(v.push_back(-1));
  EXPECT_EQ(v.size(), V::maxSize());
  EXPECT_EQ(v.back(), static_cast<int>(V::maxSize() - 1));
}

TEST(ChunkedVectorTest, FixedChunksWhenFirstEqualsMax) {
  using V = ChunkedVector<int, 8, 8, 3>;
  static_assert(V::maxSize() == 24);
  V v;
  fillAndCheck(v, V::maxSize());
  EXPECT_FALSE(v.push_back(-1));
}

TEST(ChunkedVectorTest, ReaderInstantiationsCoverTheirCaps) {
  static_assert(ChunkedVector<int, 16, 128, 131>::maxSize() >= 16384);
  static_assert(ChunkedVector<std::pair<std::string, uint16_t>, 4, 64, 23>::maxSize() >= 1024 + 128);
  ChunkedVector<int, 16, 128, 131> lut;
  fillAndCheck(lut, 3000);
}

TEST(ChunkedVectorTest, MovesNonTrivialValues) {
  ChunkedVector<std::pair<std::string, uint16_t>, 4, 64, 23> anchors;
  for (int i = 0; i < 200; ++i) {
    ASSERT_TRUE(anchors.push_back({"anchor-with-a-long-enough-id-" + std::to_string(i), static_cast<uint16_t>(i)}));
  }
  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ(anchors[i].first, "anchor-with-a-long-enough-id-" + std::to_string(i));
    EXPECT_EQ(anchors[i].second, i);
  }
}

TEST(ChunkedVectorTest, RandomAccessSortingAndLookupCrossEveryChunkBoundary) {
  using V = ChunkedVector<int, 4, 16, 6>;
  static_assert(std::random_access_iterator<V::iterator>);
  V values;
  for (int i = static_cast<int>(V::maxSize()) - 1; i >= 0; --i) ASSERT_TRUE(values.push_back(i));
  std::sort(values.begin(), values.end());
  for (size_t i = 0; i < values.size(); ++i) {
    EXPECT_EQ(values[i], static_cast<int>(i));
    const auto found = std::lower_bound(values.begin(), values.end(), static_cast<int>(i));
    ASSERT_NE(found, values.end());
    EXPECT_EQ(found - values.begin(), static_cast<std::ptrdiff_t>(i));
  }
  EXPECT_EQ(values.end() - values.begin(), static_cast<std::ptrdiff_t>(values.size()));
  EXPECT_EQ(values.begin()[5], 5);
  EXPECT_EQ(*(5 + values.begin()), 5);
  EXPECT_EQ(*--values.end(), 75);
}

TEST(ChunkedVectorTest, EveryAllocationFailureKeepsEarlierEntriesAndCanRetry) {
  using V = ChunkedVector<int, 4, 16, 6>;
  for (size_t chunk = 1; chunk <= 6; ++chunk) {
    V values;
    size_t accepted = 0;
    {
      parser_test::ScopedAllocationFailure failure(parser_test::ScopedAllocationFailure::Kind::Array, 0, chunk);
      for (; accepted < V::maxSize(); ++accepted) {
        if (!values.push_back(static_cast<int>(accepted))) break;
      }
      ASSERT_EQ(failure.failures(), 1u);
      EXPECT_EQ(values.size(), accepted);
      for (size_t i = 0; i < accepted; ++i) EXPECT_EQ(values[i], static_cast<int>(i));
    }
    ASSERT_TRUE(values.push_back(static_cast<int>(accepted)));
    EXPECT_EQ(values.size(), accepted + 1);
    EXPECT_EQ(values.back(), static_cast<int>(accepted));
  }
}
