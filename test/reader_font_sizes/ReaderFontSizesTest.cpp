#include <gtest/gtest.h>

#include "ReaderFontSizes.h"
namespace {
SdCardFontFamilyInfo family;
}
const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& name) const {
  return name == family.name ? &family : nullptr;
}
std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const {
  std::vector<uint8_t> result;
  result.reserve(files.size());
  for (const auto& file : files) result.push_back(file.pointSize);
  return result;
}
TEST(ReaderFontSizes, VectorOffersEveryPointFromEightThroughTwentyTwo) {
  SdCardFontRegistry registry;
  family = {};
  family.name = "Vector";
  family.vector = true;
  const auto sizes = readerFontPointSizes(&registry, "Vector");
  ASSERT_EQ(sizes.size(), 15u);
  for (size_t i = 0; i < sizes.size(); ++i) EXPECT_EQ(sizes[i], 8u + i);
}
TEST(ReaderFontSizes, BitmapSizesRemainExactlyThoseInstalled) {
  SdCardFontRegistry registry;
  family = {};
  family.name = "Bitmap";
  family.files = {{"small.cpfont", 9, 0}, {"large.cpfont", 21, 0}};
  EXPECT_EQ(readerFontPointSizes(&registry, "Bitmap"), (std::vector<uint8_t>{9, 21}));
}
TEST(ReaderFontSizes, UnavailableOrEmptyFamiliesFallBackToBuiltins) {
  SdCardFontRegistry registry;
  family = {};
  family.name = "Empty";
  const std::vector<uint8_t> expected{12, 14, 16, 18};
  EXPECT_EQ(readerFontPointSizes(nullptr, "anything"), expected);
  EXPECT_EQ(readerFontPointSizes(&registry, nullptr), expected);
  EXPECT_EQ(readerFontPointSizes(&registry, ""), expected);
  EXPECT_EQ(readerFontPointSizes(&registry, "unknown"), expected);
  EXPECT_EQ(readerFontPointSizes(&registry, "Empty"), expected);
}
TEST(ReaderFontSizes, VectorSnappingClampsBoundsAndKeepsAllSelectableSizes) {
  for (unsigned pt = 0; pt <= 255; ++pt) {
    const auto actual = snapToNearestPointSize(VECTOR_READER_POINT_SIZES, std::size(VECTOR_READER_POINT_SIZES), pt);
    EXPECT_EQ(actual, pt < 8 ? 8 : pt > 22 ? 22 : pt);
  }
}
TEST(ReaderFontSizes, BuiltinTiesStillChooseTheSmallerSize) {
  EXPECT_EQ(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), 15), 14);
  EXPECT_EQ(snapToNearestPointSize(nullptr, 0, 17), 17);
}
