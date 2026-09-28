#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <tuple>

#include "PageTurnFixture.h"

namespace {
using namespace epub_page_test;
class EpubPageTurn : public testing::TestWithParam<std::tuple<Scene, GfxRenderer::Orientation>> {};

TEST_P(EpubPageTurn, RetainedDecodedPageMatchesReloadWithoutForegroundIo) {
  const auto [scene, orientation] = GetParam();
  PageTurnFixture fixture(scene);
  const auto original = fixture.run(false, orientation);
  const auto retained = fixture.run(true, orientation);
  EXPECT_FALSE(original.reusedDecodedPage);
  EXPECT_TRUE(retained.reusedDecodedPage);
  EXPECT_LE(retained.retainedBudgetBytes, PrefetchedPageCache::MAX_PAGE_BUDGET);
  EXPECT_EQ(original.serializedPage, fixture.expectedBody());
  EXPECT_EQ(retained.serializedPage, fixture.expectedBody());
  EXPECT_EQ(retained.visibleTextOffset, PageTurnFixture::VISIBLE_OFFSET + 1);
  EXPECT_EQ(retained.visibleTextOffset, original.visibleTextOffset);
  EXPECT_EQ(retained.bw, original.bw);
  EXPECT_EQ(retained.lsb, original.lsb);
  EXPECT_EQ(retained.msb, original.msb);
  EXPECT_NE(retained.lsb, retained.msb);
  EXPECT_TRUE(std::any_of(retained.bw.begin(), retained.bw.end(), [](uint8_t value) { return value != 0xff; }));
  EXPECT_EQ(original.idle.deserializations, 1u);
  EXPECT_EQ(retained.idle.deserializations, 1u);
  EXPECT_EQ(original.foregroundLoad.deserializations, 1u);
  EXPECT_EQ(retained.foregroundLoad.deserializations, 0u);
  EXPECT_EQ(original.foregroundLoad.io.opens, 1u);
  EXPECT_EQ(original.foregroundLoad.io.seeks, 5u);
  EXPECT_EQ(original.foregroundLoad.io.bytes, fixture.expectedBody().size() + 4 * sizeof(uint32_t));
  EXPECT_LE(original.foregroundLoad.io.reads, scene == Scene::Prose ? 121u : 126u);
  EXPECT_EQ(original.foregroundLoad.allocations.calls, scene == Scene::Prose ? 59u : 64u);
  EXPECT_GT(original.foregroundLoad.allocations.calls, 0u);
  EXPECT_EQ(retained.foregroundLoad.io.opens, 0u);
  EXPECT_EQ(retained.foregroundLoad.io.seeks, 0u);
  EXPECT_EQ(retained.foregroundLoad.io.reads, 0u);
  EXPECT_EQ(retained.foregroundLoad.io.bytes, 0u);
  EXPECT_EQ(retained.foregroundLoad.allocations.calls, 0u);
  EXPECT_EQ(retained.idle.bwRefreshes, 0u);
  EXPECT_EQ(retained.idle.grayRefreshes, 0u);
  EXPECT_EQ(retained.foregroundLoad.bwRefreshes, 0u);
  EXPECT_EQ(retained.foregroundLoad.grayRefreshes, 0u);
  EXPECT_EQ(original.foregroundRender.bwRefreshes, retained.foregroundRender.bwRefreshes);
  EXPECT_EQ(original.foregroundRender.grayRefreshes, retained.foregroundRender.grayRefreshes);
  EXPECT_EQ(retained.foregroundRender.bwRefreshes, 1u);
  EXPECT_EQ(retained.foregroundRender.grayRefreshes, 1u);
  EXPECT_EQ(retained.foregroundRender.renderPasses, original.foregroundRender.renderPasses);
}

INSTANTIATE_TEST_SUITE_P(CachedTextPages, EpubPageTurn,
                         testing::Combine(testing::Values(Scene::Prose, Scene::Annotated),
                                          testing::Values(GfxRenderer::Portrait, GfxRenderer::PortraitInverted,
                                                          GfxRenderer::LandscapeClockwise,
                                                          GfxRenderer::LandscapeCounterClockwise)));
}  // namespace

namespace {
std::vector<uint8_t> encodeBlock(const TextBlock& block) {
  std::vector<uint8_t> bytes;
  bytes.reserve(16384);
  HalFile file;
  file.open(bytes);
  EXPECT_TRUE(block.serialize(file));
  return bytes;
}

std::unique_ptr<TextBlock> annotationBlock(const size_t count, std::vector<std::string> rubies = {}) {
  std::vector<std::string> words(count, "word");
  std::vector<int16_t> positions(count, 12);
  std::vector<EpdFontFamily::Style> styles(count, EpdFontFamily::REGULAR);
  BlockStyle style;
  style.marginLeft = 19;
  style.characterSpacing = -2;
  return std::make_unique<TextBlock>(words, positions, styles, std::vector<uint8_t>{}, std::vector<uint16_t>{}, style,
                                     std::move(rubies));
}

TEST(CachedPageReads, EmptyAnnotationsBatchWithoutAllocationsOrExtraBytes) {
  using namespace epub_page_test;
  for (const size_t words : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 127u}) {
    SCOPED_TRACE(words);
    auto original = annotationBlock(words);
    auto bytes = encodeBlock(*original);
    HalFile file;
    file.open(bytes);
    io = {};
    allocations = {};
    captureAllocations = true;
    auto decoded = TextBlock::deserialize(file);
    captureAllocations = false;
    const auto measuredIo = io;
    const auto measuredAllocations = allocations;
    ASSERT_NE(nullptr, decoded);
    EXPECT_EQ(words == 0 ? 2u : 3u + (words + 15) / 16, measuredIo.reads);
    EXPECT_EQ(0u, measuredIo.seeks);
    EXPECT_EQ(bytes.size(), measuredIo.bytes);
    EXPECT_EQ(bytes.size(), file.position());
    EXPECT_EQ(words == 0 ? 1u : 2u, measuredAllocations.calls);
    EXPECT_EQ(bytes, encodeBlock(*decoded));
  }
}

TEST(CachedPageReads, VariableAnnotationsPreserveBytesAcrossBufferBoundaries) {
  using namespace epub_page_test;
  for (const size_t words : {1u, 5u, 15u, 16u, 17u, 33u}) {
    for (const size_t length : {1u, 3u, 4u, 17u, 63u, 64u, 65u, 129u, 255u}) {
      SCOPED_TRACE(words);
      SCOPED_TRACE(length);
      std::vector<std::string> rubies(words);
      for (size_t i = 0; i < words; ++i) {
        if (i % 3 != 1) rubies[i] = std::string(length + i % 2, 'a' + i % 26);
      }
      auto original = annotationBlock(words, std::move(rubies));
      auto bytes = encodeBlock(*original);
      HalFile file;
      file.open(bytes);
      io = {};
      auto decoded = TextBlock::deserialize(file);
      const auto measured = io;
      ASSERT_NE(nullptr, decoded);
      EXPECT_EQ(0u, measured.seeks);
      EXPECT_EQ(bytes.size(), measured.bytes);
      EXPECT_EQ(bytes.size(), file.position());
      EXPECT_EQ(bytes, encodeBlock(*decoded));
    }
  }
}

TEST(CachedPageReads, CompletePagesReduceStorageCallsWithUnchangedAllocationBudget) {
  using namespace epub_page_test;
  for (const auto scene : {Scene::Prose, Scene::Annotated}) {
    PageTurnFixture fixture(scene);
    const auto result = fixture.run(false, GfxRenderer::Portrait);
    const auto& work = result.foregroundLoad;
    EXPECT_EQ(fixture.expectedBody(), result.serializedPage);
    EXPECT_EQ(scene == Scene::Prose ? 121u : 126u, work.io.reads);
    EXPECT_EQ(5u, work.io.seeks);
    EXPECT_EQ(fixture.expectedBody().size() + 4 * sizeof(uint32_t), work.io.bytes);
    EXPECT_EQ(scene == Scene::Prose ? 59u : 64u, work.allocations.calls);
    std::printf("PAGE_READS scene=%s reads=%zu seeks=%zu bytes=%zu allocations=%zu\n", sceneName(scene), work.io.reads,
                work.io.seeks, work.io.bytes, work.allocations.calls);
  }
}
}  // namespace

TEST(RubyRendering, ReusesAnnotationTextAcrossRenderModes) {
  static constexpr uint64_t EXPECTED_PIXELS[4][3] = {
      {0xb620c544256a40e0ULL, 0x64b33547ffd2a6b8ULL, 0x0db3a328e04a1937ULL},
      {0x178e6f174e89db4bULL, 0xf7f5e9d87299f7eeULL, 0xc88ccee41daa44a2ULL},
      {0x4b838fe1eac7505bULL, 0x884487f44027dd73ULL, 0x714849700edf8961ULL},
      {0xcc0f978bd7c14549ULL, 0x5d948514416c2a20ULL, 0x5cc7462b9ba1dbb4ULL},
  };
  using namespace epub_page_test;
  using Style = EpdFontFamily::Style;
  const std::vector<std::string> words = {"Reading", "ruby", "groups", "another", "word"};
  const std::vector<int16_t> positions = {30, 130, 210, 300, 380};
  const std::vector<Style> styles = {EpdFontFamily::REGULAR, EpdFontFamily::RUBY_CONTINUE, EpdFontFamily::RUBY_CONTINUE,
                                     EpdFontFamily::BOLD, EpdFontFamily::UNDERLINE};
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    page_test::RealFontPage gfx;
    gfx.renderer.setOrientation(orientation);
    std::vector<std::string> ruby = {"long annotation with many letters", "", "", "note",
                                     "another long annotation to draw"};
    BlockStyle style;
    style.characterSpacing = 1;
    TextBlock block(words, positions, styles, {}, {}, style, std::move(ruby));
    ASSERT_TRUE(block.valid());
    const auto wireImage = encodeBlock(block);
    auto prewarm = gfx.cache.createPrewarmScope();
    block.render(gfx.renderer, page_test::FONT_ID, 0, 80);
    prewarm.endScanAndPrewarm();
    for (const auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      page_test::Plane plane{};
      gfx.renderer.setRenderMode(mode);
      if (mode == GfxRenderer::BW) {
        gfx.renderer.clearScreen();
      } else {
        gfx.renderer.beginStripTarget(plane.data(), 0, HalDisplay::DISPLAY_HEIGHT);
        gfx.renderer.clearScreen(0);
      }
      allocations = {};
      captureAllocations = true;
      block.render(gfx.renderer, page_test::FONT_ID, 0, 80);
      captureAllocations = false;
      const auto measured = allocations;
      if (mode == GfxRenderer::BW)
        plane = gfx.display.frame;
      else
        gfx.renderer.endStripTarget();
      uint64_t digest = 14695981039346656037ULL;
      for (const uint8_t byte : plane) {
        digest ^= byte;
        digest *= 1099511628211ULL;
      }
      std::printf("RUBY_RENDER orientation=%d mode=%d allocations=%zu bytes=%zu hash=%016llx\n",
                  static_cast<int>(orientation), static_cast<int>(mode), measured.calls, measured.bytes,
                  static_cast<unsigned long long>(digest));
      EXPECT_EQ(digest, EXPECTED_PIXELS[orientation][mode]);
      EXPECT_EQ(measured.calls, 1u);
      EXPECT_EQ(measured.bytes, 40u);
      EXPECT_EQ(encodeBlock(block), wireImage);
      const uint8_t background = mode == GfxRenderer::BW ? 0xff : 0;
      EXPECT_TRUE(
          std::any_of(plane.begin(), plane.end(), [background](const uint8_t byte) { return byte != background; }));
    }
  }
}

TEST(RubyRendering, CompletePagesKeepPixelsAndProseAllocationBudget) {
  static constexpr uint64_t EXPECTED_PIXELS[2][4] = {
      {0x9bd371383337398bULL, 0x224c0109b01c030cULL, 0x0fe7b1a9a079401cULL, 0x337275435822b51cULL},
      {0xf9a8a86d3d456bb6ULL, 0x696d546b9894ac70ULL, 0x4e5b03d2e6decbdeULL, 0x482cdbbd6fdffcc0ULL},
  };
  using namespace epub_page_test;
  for (const auto scene : {Scene::Prose, Scene::Annotated}) {
    PageTurnFixture fixture(scene);
    for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted,
                                   GfxRenderer::LandscapeClockwise, GfxRenderer::LandscapeCounterClockwise}) {
      const auto result = fixture.run(false, orientation);
      uint64_t digest = 14695981039346656037ULL;
      for (const auto* plane : {&result.bw, &result.lsb, &result.msb}) {
        for (const uint8_t byte : *plane) {
          digest ^= byte;
          digest *= 1099511628211ULL;
        }
      }
      EXPECT_EQ(digest, EXPECTED_PIXELS[static_cast<int>(scene)][orientation]);
      EXPECT_EQ(result.foregroundRender.allocations.calls, scene == Scene::Prose ? 0u : 20u);
      EXPECT_EQ(result.foregroundRender.allocations.bytes, scene == Scene::Prose ? 0u : 800u);
      std::printf("PAGE_RENDER scene=%s orientation=%d allocations=%zu bytes=%zu hash=%016llx\n", sceneName(scene),
                  static_cast<int>(orientation), result.foregroundRender.allocations.calls,
                  result.foregroundRender.allocations.bytes, static_cast<unsigned long long>(digest));
    }
  }
}
