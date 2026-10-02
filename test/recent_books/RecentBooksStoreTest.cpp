#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include "RecentBooksStore.h"

namespace {
constexpr char FIRST[] = "/Books/First.epub";
constexpr char SECOND[] = "/Books/Second.epub";
constexpr char RENAMED[] = "/Books/Renamed.epub";

class RecentBooksStoreTest : public testing::Test {
 protected:
  RecentBooksStore& store = RECENT_BOOKS;

  void SetUp() override {
    fake::reset();
    string_test::reset();
    JsonDocument empty;
    ASSERT_TRUE(store.fromJson(empty.as<JsonVariantConst>()));
    fake::add(FIRST);
    fake::add(SECOND);
  }

  void add(const char* path = FIRST, const char* title = "Title") { store.addBook(path, title, "Author", ""); }

  static unsigned writes() {
    unsigned count = 0;
    for (const auto& entry : fake::writesByPath) count += entry.second;
    return count;
  }

  static JsonDocument persisted() {
    JsonDocument doc;
    const auto file = fake::files.find(RecentBooksStore::getFilePath());
    if (file != fake::files.end()) {
      const auto& bytes = file->second->bytes;
      EXPECT_FALSE(deserializeJson(doc, bytes.data(), bytes.size()));
    }
    return doc;
  }
};

TEST_F(RecentBooksStoreTest, UnchangedFrontOpenSkipsSerializationAndWrites) {
  add();
  const unsigned before = writes();
  const unsigned serializations = string_test::concatCalls;
  add();
  add();
  EXPECT_EQ(writes(), before);
  EXPECT_EQ(string_test::concatCalls, serializations);
  EXPECT_EQ(store.getCount(), 1);
}

TEST_F(RecentBooksStoreTest, ReorderingExistingEntryPersists) {
  add();
  add(SECOND);
  const unsigned before = writes();
  add();
  EXPECT_GT(writes(), before);
  EXPECT_EQ(store.getBooks().front().path, FIRST);
  EXPECT_STREQ(persisted()["books"][0]["path"], FIRST);
}

TEST_F(RecentBooksStoreTest, ChangedMetadataPersistsAndUnchangedUpdateSkips) {
  add();
  const unsigned before = writes();
  store.updateBook(FIRST, "Changed", "New author", "/cover.bmp");
  EXPECT_GT(writes(), before);
  auto doc = persisted();
  EXPECT_STREQ(doc["books"][0]["title"], "Changed");
  EXPECT_STREQ(doc["books"][0]["author"], "New author");
  EXPECT_STREQ(doc["books"][0]["coverBmpPath"], "/cover.bmp");
  const unsigned updated = writes();
  store.updateBook(FIRST, "Changed", "New author", "/cover.bmp");
  EXPECT_EQ(writes(), updated);
}

TEST_F(RecentBooksStoreTest, AddUpdatesMetadataWithoutReordering) {
  add();
  const unsigned before = writes();
  add(FIRST, "New title");
  EXPECT_GT(writes(), before);
  EXPECT_STREQ(persisted()["books"][0]["title"], "New title");
}

TEST_F(RecentBooksStoreTest, PruningPersistsOnUnchangedFrontOpen) {
  add(SECOND);
  add();
  fake::files.erase(SECOND);
  const unsigned before = writes();
  add();
  EXPECT_GT(writes(), before);
  EXPECT_EQ(persisted()["books"].size(), 1u);
}

TEST_F(RecentBooksStoreTest, FailedAddRetriesOnUnchangedOpen) {
  fake::failWrite = 0;
  add();
  ASSERT_TRUE(fake::failureTriggered);
  EXPECT_FALSE(Storage.exists(RecentBooksStore::getFilePath()));
  add();
  EXPECT_STREQ(persisted()["books"][0]["path"], FIRST);
  const unsigned before = writes();
  add();
  EXPECT_EQ(writes(), before);
}

TEST_F(RecentBooksStoreTest, FailedMetadataUpdateRetriesOnUnchangedUpdate) {
  add();
  fake::failWrite = 0;
  store.updateBook(FIRST, "Changed", "Author", "");
  ASSERT_TRUE(fake::failureTriggered);
  EXPECT_STREQ(persisted()["books"][0]["title"], "Title");
  store.updateBook(FIRST, "Changed", "Author", "");
  EXPECT_STREQ(persisted()["books"][0]["title"], "Changed");
}

TEST_F(RecentBooksStoreTest, FailedPathUpdateRetriesOnUnchangedOpen) {
  add();
  fake::add(RENAMED);
  fake::files.erase(FIRST);
  fake::failWrite = 0;
  store.updatePath(FIRST, RENAMED, "", "");
  ASSERT_TRUE(fake::failureTriggered);
  EXPECT_STREQ(persisted()["books"][0]["path"], FIRST);
  add(RENAMED);
  EXPECT_STREQ(persisted()["books"][0]["path"], RENAMED);
}

TEST_F(RecentBooksStoreTest, FailedRemovalRetriesOnUnchangedOpen) {
  add(SECOND);
  add();
  fake::failWrite = 0;
  ASSERT_TRUE(store.removeByPath(SECOND));
  ASSERT_TRUE(fake::failureTriggered);
  EXPECT_EQ(persisted()["books"].size(), 2u);
  add();
  EXPECT_EQ(persisted()["books"].size(), 1u);
}

TEST_F(RecentBooksStoreTest, PublicPruneAndExplicitSaveClearPendingChanges) {
  add(SECOND);
  add();
  fake::files.erase(SECOND);
  ASSERT_TRUE(store.pruneMissing());
  ASSERT_TRUE(store.saveToFile());
  EXPECT_EQ(persisted()["books"].size(), 1u);
  const unsigned before = writes();
  add();
  EXPECT_EQ(writes(), before);
}

TEST_F(RecentBooksStoreTest, ExplicitSaveAlwaysWritesAndFailureRemainsPending) {
  add();
  const unsigned before = writes();
  ASSERT_TRUE(store.saveToFile());
  EXPECT_GT(writes(), before);
  fake::failWrite = 0;
  EXPECT_FALSE(store.saveToFile());
  const unsigned failed = writes();
  add();
  EXPECT_GT(writes(), failed);
}

TEST_F(RecentBooksStoreTest, FailedPublicPruneSaveRetriesOnUnchangedOpen) {
  add(SECOND);
  add();
  fake::files.erase(SECOND);
  ASSERT_TRUE(store.pruneMissing());
  fake::failWrite = 0;
  EXPECT_FALSE(store.saveToFile());
  EXPECT_EQ(persisted()["books"].size(), 2u);
  add();
  EXPECT_EQ(persisted()["books"].size(), 1u);
}

TEST_F(RecentBooksStoreTest, LoadedStoreSavesOnceThenSkipsUnchangedOpen) {
  add();
  ASSERT_TRUE(store.loadFromFile());
  const unsigned before = writes();
  add();
  EXPECT_GT(writes(), before);
  const unsigned saved = writes();
  add();
  EXPECT_EQ(writes(), saved);
}

TEST_F(RecentBooksStoreTest, NewEntriesKeepBoundedListAndRoundTripOrder) {
  for (int i = 0; i < RecentBooksStore::MAX_RECENT_BOOKS + 2; ++i) {
    const std::string path = "/Books/Book" + std::to_string(i) + ".epub";
    fake::add(path);
    add(path.c_str());
  }
  EXPECT_EQ(store.getCount(), RecentBooksStore::MAX_RECENT_BOOKS);
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.getBooks().front().path, "/Books/Book11.epub");
  EXPECT_EQ(store.getBooks().back().path, "/Books/Book2.epub");
}
}  // namespace
