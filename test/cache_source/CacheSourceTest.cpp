#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "CacheSource.h"
#include "HalStorage.h"

namespace {
using CacheSource::Result;
const std::string BOOK = "/Author/Title/book.epub";
const std::string CACHE = "/.crosspoint/epub_1";
const std::vector<std::string> FULL_CACHE{"book.bin", "links.bin",      "progress.bin",
                                          "sections", "sections/0.bin", "source.bin"};
const std::vector<std::string> EMPTIED_CACHE{"progress.bin", "source.bin"};

class CacheSourceTest : public testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    fake::add(BOOK, "first version", 100);
    fake::add(CACHE + "/progress.bin", "pos");
    fake::add(CACHE + "/book.bin", "index");
    fake::add(CACHE + "/links.bin", "back");
    fake::add(CACHE + "/sections/0.bin", "page");
    fake::files[CACHE + "/sections"]->directory = true;
    fake::files[CACHE]->directory = true;
  }

  static void replaceBook() { fake::add(BOOK, "second, longer version", 100); }

  static std::vector<std::string> cacheEntries() {
    std::vector<std::string> out;
    for (const auto& [path, node] : fake::files) {
      if (path.starts_with(CACHE + "/")) out.push_back(path.substr(CACHE.size() + 1));
    }
    return out;
  }
};

TEST_F(CacheSourceTest, CacheWithoutStampIsAdoptedForTheCurrentFile) {
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Unchanged);
  EXPECT_EQ(cacheEntries(), FULL_CACHE);
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Unchanged);
  EXPECT_EQ(cacheEntries(), FULL_CACHE);
}

TEST_F(CacheSourceTest, MissingCacheIsNotStamped) {
  fake::files.erase(CACHE + "/book.bin");
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Unchanged);
  EXPECT_FALSE(Storage.exists((CACHE + "/source.bin").c_str()));
}

TEST_F(CacheSourceTest, ReplacedBookKeepsOnlyProgressAndTheOldStamp) {
  CacheSource::record(BOOK, CACHE);
  replaceBook();
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Replaced);
  EXPECT_EQ(cacheEntries(), EMPTIED_CACHE);
  // Until the rebuild records the new file, every load still sees the change.
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Replaced);
}

TEST_F(CacheSourceTest, SameSizeWithNewTimestampIsAlsoAChange) {
  CacheSource::record(BOOK, CACHE);
  fake::add(BOOK, "first VERSION", 200);
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Replaced);
  EXPECT_EQ(cacheEntries(), EMPTIED_CACHE);
}

TEST_F(CacheSourceTest, UnchangedBookKeepsTheCache) {
  CacheSource::record(BOOK, CACHE);
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Unchanged);
  EXPECT_EQ(cacheEntries(), FULL_CACHE);
}

TEST_F(CacheSourceTest, RebuiltCacheIsStampedWithTheNewFile) {
  CacheSource::record(BOOK, CACHE);
  replaceBook();
  ASSERT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Replaced);
  fake::add(CACHE + "/book.bin", "new index");
  CacheSource::record(BOOK, CACHE);
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Unchanged);
  EXPECT_TRUE(Storage.exists((CACHE + "/book.bin").c_str()));
}

TEST_F(CacheSourceTest, UnreadableBookLeavesTheCacheAlone) {
  CacheSource::record(BOOK, CACHE);
  fake::files.erase(BOOK);
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Unchanged);
  EXPECT_EQ(cacheEntries(), FULL_CACHE);
}

TEST_F(CacheSourceTest, IndexThatCannotBeRemovedRejectsTheCacheUntilRetried) {
  CacheSource::record(BOOK, CACHE);
  replaceBook();
  fake::failRemove = 0;
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Failed);
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Replaced);
  EXPECT_EQ(cacheEntries(), EMPTIED_CACHE);
}

TEST_F(CacheSourceTest, LaterRemovalFailureLeavesNoIndexAndRetries) {
  CacheSource::record(BOOK, CACHE);
  replaceBook();
  fake::failRemove = 1;
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Failed);
  EXPECT_FALSE(Storage.exists((CACHE + "/book.bin").c_str()));
  // The stamp still names the old file, so the next open finishes the removal.
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Replaced);
  EXPECT_EQ(cacheEntries(), EMPTIED_CACHE);
}

TEST_F(CacheSourceTest, FailedDirectoryScanRejectsTheCache) {
  CacheSource::record(BOOK, CACHE);
  replaceBook();
  fake::failNext = 0;
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Failed);
}

TEST_F(CacheSourceTest, StampThatCannotBeOpenedIsNotReplacedByTheNewFile) {
  CacheSource::record(BOOK, CACHE);
  replaceBook();
  fake::failOpenPath = CACHE + "/source.bin";
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Failed);
  fake::failOpenPath.clear();
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Replaced);
}

TEST_F(CacheSourceTest, ManyImagesAreRemovedWithLinearDirectoryWork) {
  constexpr int IMAGES = 400;
  for (int i = 0; i < IMAGES; ++i) fake::add(CACHE + "/img_" + std::to_string(i) + ".bmp", "x");
  CacheSource::record(BOOK, CACHE);
  replaceBook();
  fake::directoryEntriesByPath.clear();
  EXPECT_EQ(CacheSource::discardIfChanged(BOOK, CACHE), Result::Replaced);
  EXPECT_EQ(cacheEntries(), EMPTIED_CACHE);
  size_t visits = 0;
  for (const auto& [path, count] : fake::directoryEntriesByPath) visits += count;
  // The fake shifts entries after a removal, so it needs several passes; the
  // bound still rules out a rescan per removed entry.
  EXPECT_LT(visits, 4u * IMAGES);
}
}  // namespace
