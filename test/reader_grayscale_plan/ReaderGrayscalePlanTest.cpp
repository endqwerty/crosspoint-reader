#include <ReaderGrayscalePlan.h>
#include <gtest/gtest.h>

namespace {
using freeink::GrayscaleBase;
using freeink::GrayscaleCapabilities;
using freeink::GrayscaleEncoding;

void expectDisabled(const ReaderGrayscalePlan& plan) {
  EXPECT_FALSE(plan.text);
  EXPECT_FALSE(plan.enabled);
  EXPECT_FALSE(plan.tiled);
  EXPECT_FALSE(plan.combinedBase);
  EXPECT_FALSE(plan.overlap);
}
}  // namespace

TEST(ReaderGrayscalePlan, UnsupportedOutputSkipsTextAndImagePlanes) {
  for (bool aa : {false, true}) {
    for (bool images : {false, true}) {
      expectDisabled(ReaderGrayscalePlan::forPage(aa, images, {}));
      // Support controls allocation even if an unsupported mode carries stale
      // optional flags; a strip/async flag alone is not a grayscale promise.
      const GrayscaleCapabilities unsupported{GrayscaleEncoding::Unsupported, GrayscaleBase::Combined, true, true,
                                              true};
      expectDisabled(ReaderGrayscalePlan::forPage(aa, images, unsupported));
    }
  }
}

TEST(ReaderGrayscalePlan, AaSettingOffStillPermitsImageGrayscale) {
  for (auto encoding : {GrayscaleEncoding::OverlayMasks, GrayscaleEncoding::AbsolutePlanes}) {
    const GrayscaleCapabilities caps{encoding, GrayscaleBase::Separate, true, true, false};
    expectDisabled(ReaderGrayscalePlan::forPage(false, false, caps));
    const auto plan = ReaderGrayscalePlan::forPage(false, true, caps);
    EXPECT_TRUE(plan.enabled);
    EXPECT_TRUE(plan.tiled);
    EXPECT_FALSE(plan.text);
    EXPECT_FALSE(plan.overlap);
    EXPECT_FALSE(plan.combinedBase);
  }
}

TEST(ReaderGrayscalePlan, BufferedOverlapRequiresBothStripAndAsyncSupport) {
  for (bool strips : {false, true}) {
    for (bool async : {false, true}) {
      const GrayscaleCapabilities caps{GrayscaleEncoding::OverlayMasks, GrayscaleBase::Separate, strips, async, false};
      const auto plan = ReaderGrayscalePlan::forPage(true, false, caps);
      EXPECT_TRUE(plan.enabled);
      EXPECT_TRUE(plan.text);
      EXPECT_EQ(plan.tiled, strips);
      EXPECT_EQ(plan.overlap, strips && async);
      EXPECT_FALSE(plan.combinedBase);
    }
  }
}

TEST(ReaderGrayscalePlan, CombinedBaseRemainsLimitedToTiledTextPages) {
  for (bool images : {false, true}) {
    for (bool strips : {false, true}) {
      const GrayscaleCapabilities caps{GrayscaleEncoding::OverlayMasks, GrayscaleBase::Combined, strips, false, true};
      const auto plan = ReaderGrayscalePlan::forPage(true, images, caps);
      EXPECT_TRUE(plan.enabled);
      EXPECT_EQ(plan.combinedBase, strips && !images);
      EXPECT_FALSE(plan.overlap);
    }
  }
}

TEST(ReaderGrayscalePlan, NightModeTransitionDoesNotChangeSavedAaChoice) {
  constexpr bool savedAa = true;
  constexpr GrayscaleCapabilities day{GrayscaleEncoding::OverlayMasks, GrayscaleBase::Separate, true, true, false};
  EXPECT_TRUE(ReaderGrayscalePlan::forPage(savedAa, false, day).overlap);
  expectDisabled(ReaderGrayscalePlan::forPage(savedAa, false, {}));
  EXPECT_TRUE(ReaderGrayscalePlan::forPage(savedAa, false, day).overlap);
}

TEST(ReaderGrayscalePlan, BinaryBodySkipsAllGrayWorkWithAaSettingUnchanged) {
  constexpr bool savedAa = true;
  for (auto base : {GrayscaleBase::Separate, GrayscaleBase::Combined}) {
    for (bool strips : {false, true}) {
      for (bool async : {false, true}) {
        const GrayscaleCapabilities caps{GrayscaleEncoding::OverlayMasks, base, strips, async, false};
        expectDisabled(ReaderGrayscalePlan::forPage(savedAa, false, caps, false));
        EXPECT_TRUE(ReaderGrayscalePlan::forPage(savedAa, false, caps, true).enabled);
      }
    }
  }
}

TEST(ReaderGrayscalePlan, BinaryBodyDoesNotChangeImageCompositing) {
  for (auto encoding : {GrayscaleEncoding::OverlayMasks, GrayscaleEncoding::AbsolutePlanes}) {
    for (bool aa : {false, true}) {
      const GrayscaleCapabilities caps{encoding, GrayscaleBase::Separate, true, true, false};
      const auto binary = ReaderGrayscalePlan::forPage(aa, true, caps, false);
      const auto gray = ReaderGrayscalePlan::forPage(aa, true, caps, true);
      EXPECT_TRUE(binary.enabled);
      EXPECT_EQ(binary.text, gray.text);
      EXPECT_EQ(binary.tiled, gray.tiled);
      EXPECT_EQ(binary.overlap, gray.overlap);
      EXPECT_EQ(binary.combinedBase, gray.combinedBase);
    }
  }
}
