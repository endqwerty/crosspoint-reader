#include <Memory.h>
#include <Serialization.h>
#include <gtest/gtest.h>

#include "PageTurnFixture.h"

namespace {
using namespace epub_page_test;

std::unique_ptr<TextBlock> smallBlock(bool focused = false, bool ruby = false) {
  const std::vector<std::string> words{"office", "reader"};
  const std::vector<int16_t> positions{0, 90};
  const std::vector<EpdFontFamily::Style> styles(2, EpdFontFamily::REGULAR);
  const std::vector<uint8_t> boundaries = focused ? std::vector<uint8_t>{2, 2} : std::vector<uint8_t>{};
  const std::vector<uint16_t> suffixes = focused ? std::vector<uint16_t>{25, 25} : std::vector<uint16_t>{};
  std::vector<std::string> rubies;
  if (ruby) {
    rubies.resize(2);
    rubies[0].reserve(300);
    rubies[0] = "annotation";
  }
  return std::make_unique<TextBlock>(words, positions, styles, boundaries, suffixes, BlockStyle{}, std::move(rubies));
}

TEST(PrefetchedPageCache, SuccessfulTakeMovesSamePageWithoutAllocating) {
  PageTurnFixture fixture(Scene::Annotated);
  auto page = fixture.loadPage(1);
  ASSERT_NE(page, nullptr);
  auto* address = page.get();
  const auto budget = page->cacheBudgetBytes();
  ASSERT_LE(budget, PrefetchedPageCache::MAX_PAGE_BUDGET);
  PrefetchedPageCache cache;
  allocations = {};
  captureAllocations = true;
  const bool retained =
      cache.retain(1, std::move(page), PrefetchedPageCache::MIN_FREE_HEAP, PrefetchedPageCache::MIN_MAX_ALLOC);
  auto received = cache.take(1);
  captureAllocations = false;
  EXPECT_TRUE(retained);
  EXPECT_EQ(address, received.get());
  EXPECT_EQ(0u, allocations.calls);
  EXPECT_EQ(nullptr, cache.take(1));
  ASSERT_NE(received, nullptr);
  EXPECT_EQ(PageTurnFixture::VISIBLE_OFFSET + 1, received->visibleTextOffset);
}

TEST(PrefetchedPageCache, WrongPageOrExplicitInvalidationReleasesSlot) {
  PageTurnFixture fixture(Scene::Prose);
  for (bool wrongPage : {false, true}) {
    auto page = fixture.loadPage(1);
    ASSERT_NE(page, nullptr);
    watchDeallocation(page->elements.front().get());
    PrefetchedPageCache cache;
    ASSERT_TRUE(cache.retain(1, std::move(page), 200 * 1024, 100 * 1024));
    ASSERT_FALSE(watchedAllocationWasFreed());
    if (wrongPage)
      EXPECT_EQ(nullptr, cache.take(0));
    else
      cache.clear();
    EXPECT_TRUE(watchedAllocationWasFreed());
    watchDeallocation(nullptr);
    EXPECT_EQ(nullptr, cache.take(1));
  }
}

TEST(PrefetchedPageCache, HeapFloorsRejectAndReleasePages) {
  PageTurnFixture fixture(Scene::Prose);
  for (bool freeHeapLow : {false, true}) {
    const size_t freeHeap = PrefetchedPageCache::MIN_FREE_HEAP - (freeHeapLow ? 1 : 0);
    const size_t largest = PrefetchedPageCache::MIN_MAX_ALLOC - (freeHeapLow ? 0 : 1);
    PrefetchedPageCache cache;
    EXPECT_FALSE(cache.retain(1, fixture.loadPage(1), freeHeap, largest));
    EXPECT_EQ(nullptr, cache.take(1));
    auto page = fixture.loadPage(1);
    watchDeallocation(page->elements.front().get());
    ASSERT_TRUE(cache.retain(1, std::move(page), 200 * 1024, 100 * 1024));
    cache.releaseIfLowMemory(freeHeap, largest);
    EXPECT_TRUE(watchedAllocationWasFreed());
    watchDeallocation(nullptr);
    EXPECT_EQ(nullptr, cache.take(1));
  }
}

TEST(PrefetchedPageCache, OversizedAnnotatedPageIsNotRetained) {
  PageTurnFixture fixture(Scene::Annotated);
  auto page = fixture.loadPage(1);
  ASSERT_NE(page, nullptr);
  page->elements.reserve(256);
  while (page->cacheBudgetBytes() <= PrefetchedPageCache::MAX_PAGE_BUDGET) {
    page->elements.push_back(std::make_unique<PageLine>(smallBlock(true, true), 0, 0));
  }
  PrefetchedPageCache cache;
  EXPECT_FALSE(cache.retain(1, std::move(page), 200 * 1024, 100 * 1024));
  EXPECT_EQ(nullptr, cache.take(1));
}

TEST(PageBudget, ImageTagIsRejectedBeforeImagePayloadAccess) {
  auto page = makeUniqueNoThrow<Page>();
  page->elements.reserve(1);
  page->elements.push_back(std::make_unique<PageImage>(nullptr, 0, 0));
  EXPECT_EQ(SIZE_MAX, page->cacheBudgetBytes());
  PrefetchedPageCache cache;
  EXPECT_FALSE(cache.retain(1, std::move(page), 200 * 1024, 100 * 1024));
}

TEST(PageBudget, EveryOwnedBlockContributesItsFullStorage) {
  const size_t blockBudget = smallBlock()->cacheBudgetBytes();
  Page once;
  Page twice;
  once.elements.reserve(2);
  twice.elements.reserve(2);
  once.elements.push_back(std::make_unique<PageLine>(smallBlock(), 0, 0));
  twice.elements.push_back(std::make_unique<PageLine>(smallBlock(), 0, 0));
  const size_t before = twice.cacheBudgetBytes();
  twice.elements.push_back(std::make_unique<PageLine>(smallBlock(), 0, 0));
  EXPECT_EQ(once.cacheBudgetBytes(), before);
  EXPECT_GE(twice.cacheBudgetBytes() - before, blockBudget + sizeof(PageLine));
}

TEST(PageBudget, RetainedVectorCapacityCountsEvenAfterClear) {
  Page page;
  const size_t empty = page.cacheBudgetBytes();
  page.elements.reserve(30);
  page.footnotes.reserve(8);
  page.links.reserve(16);
  const size_t reserved = page.cacheBudgetBytes();
  const size_t payload = page.elements.capacity() * sizeof(page.elements[0]) +
                         page.footnotes.capacity() * sizeof(FootnoteEntry) + page.links.capacity() * sizeof(PageLink);
  EXPECT_GE(reserved - empty, payload);
  page.elements.clear();
  page.footnotes.clear();
  page.links.clear();
  EXPECT_EQ(reserved, page.cacheBudgetBytes());
}

TEST(PageBudget, FocusArenaAndRubyCapacityAreIncludedWithoutAllocating) {
  const auto plain = smallBlock();
  const auto focus = smallBlock(true);
  const auto ruby = smallBlock(false, true);
  EXPECT_EQ(2 * (sizeof(uint16_t) + sizeof(uint8_t)), focus->cacheBudgetBytes() - plain->cacheBudgetBytes());
  const auto& strings = ruby->getRubyTexts();
  ASSERT_EQ(2u, strings.size());
  ASSERT_GE(strings[0].capacity(), 300u);
  const size_t rubyStorage = strings.capacity() * sizeof(std::string) + strings[0].capacity() + 1;
  EXPECT_GE(ruby->cacheBudgetBytes() - plain->cacheBudgetBytes(), rubyStorage);
  allocations = {};
  captureAllocations = true;
  const auto budget = ruby->cacheBudgetBytes();
  captureAllocations = false;
  EXPECT_GT(budget, 0u);
  EXPECT_EQ(0u, allocations.calls);
}

TEST(SectionPageReader, RejectsInvalidHeaderAndBodyOffsetsBeforeDeserializing) {
  PageTurnFixture fixture(Scene::Prose);
  const auto valid = files[0];
  files[0].resize(SectionPageReader::HEADER_SIZE - 1);
  EXPECT_EQ(nullptr, SectionPageReader::load("section", 1));
  files[0] = valid;
  HalFile file;
  file.open(files[0]);
  file.seek(SectionPageReader::HEADER_SIZE - sizeof(uint32_t) * 5);
  serialization::writePod(file, uint32_t{0});
  EXPECT_EQ(nullptr, SectionPageReader::load("section", 1));
  files[0] = valid;
  file.open(files[0]);
  file.seek(SectionPageReader::HEADER_SIZE - sizeof(uint32_t) * 5);
  uint32_t lutOffset = 0;
  serialization::readPod(file, lutOffset);
  file.seek(lutOffset + sizeof(uint32_t));
  serialization::writePod(file, uint32_t{SectionPageReader::HEADER_SIZE - 1});
  EXPECT_EQ(nullptr, SectionPageReader::load("section", 1));
}
}  // namespace
