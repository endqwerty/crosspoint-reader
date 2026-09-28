#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <PersistableStore.h>
#include <gtest/gtest.h>

#include "AtomicFile.h"
#include "BookmarkFile.h"
#include "BookmarkUtil.h"

namespace {
constexpr char BOOK[] = "/Books/Novel.epub";
constexpr char SETTINGS_PATH[] = "/.crosspoint/test-settings.json";

class RejectAllocator : public ArduinoJson::Allocator {
 public:
  void* allocate(size_t) override { return nullptr; }
  void* reallocate(void*, size_t) override { return nullptr; }
  void deallocate(void*) override {}
};

BookmarkEntry entry(const char* xpath = "/body/section[2]/p[3]", const char* name = "A note") {
  BookmarkEntry result;
  result.xpath = xpath;
  result.name = name;
  result.summary = "A quote: \"hello\"\nnext line — café";
  result.percentage = 0.375f;
  result.computedSpineIndex = 5;
  result.computedChapterPageCount = 123;
  result.computedChapterProgress = 47;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = 98765;
  return result;
}

void expectEntry(const BookmarkEntry& actual, const BookmarkEntry& expected) {
  EXPECT_EQ(actual.xpath, expected.xpath);
  EXPECT_EQ(actual.name, expected.name);
  EXPECT_EQ(actual.summary, expected.summary);
  EXPECT_FLOAT_EQ(actual.percentage, expected.percentage);
  EXPECT_EQ(actual.computedSpineIndex, expected.computedSpineIndex);
  EXPECT_EQ(actual.computedChapterPageCount, expected.computedChapterPageCount);
  EXPECT_EQ(actual.computedChapterProgress, expected.computedChapterProgress);
  EXPECT_EQ(actual.hasVisibleTextOffset, expected.hasVisibleTextOffset);
  if (expected.hasVisibleTextOffset) EXPECT_EQ(actual.visibleTextOffset, expected.visibleTextOffset);
}

class BookmarkPersistenceTest : public testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    string_test::reset();
  }
  static std::string path() { return BookmarkUtil::getBookmarkPath(BOOK); }
  static std::string bytes(const std::string& file) {
    const auto found = fake::files.find(file);
    if (found == fake::files.end()) return {};
    return {found->second->bytes.begin(), found->second->bytes.end()};
  }
};

TEST_F(BookmarkPersistenceTest, ActualJsonRoundTripPreservesEscapesUnicodeAndPositionFields) {
  const std::vector<BookmarkEntry> original{entry()};
  ASSERT_TRUE(BookmarkFile::save(BOOK, original));
  ASSERT_GT(string_test::concatCalls, 0u);
  EXPECT_FALSE(Storage.exists((path() + ".bak").c_str()));
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded));
  ASSERT_EQ(loaded.size(), 1u);
  expectEntry(loaded[0], original[0]);
}

TEST_F(BookmarkPersistenceTest, PrependAndExcludePersistProposedEditWithoutMutatingSource) {
  const std::vector<BookmarkEntry> original{entry("/old/first"), entry("/old/second")};
  const BookmarkEntry proposed = entry("/new", "Inserted first");
  const std::string excluded = "/old/first";
  BookmarkFile::SaveOptions options;
  options.prepend = &proposed;
  options.exclude = [](const BookmarkEntry& candidate, const void* context) {
    return candidate.xpath == *static_cast<const std::string*>(context);
  };
  options.context = &excluded;
  ASSERT_TRUE(BookmarkFile::save(BOOK, original, options));
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded));
  ASSERT_EQ(loaded.size(), 2u);
  expectEntry(loaded[0], proposed);
  expectEntry(loaded[1], original[1]);
  EXPECT_EQ(original[0].xpath, "/old/first");
  EXPECT_EQ(original[1].xpath, "/old/second");
}

TEST_F(BookmarkPersistenceTest, RemovingEveryEntryPersistsAnEmptyBookmarkArray) {
  const std::vector<BookmarkEntry> original{entry()};
  BookmarkFile::SaveOptions options;
  options.exclude = [](const BookmarkEntry&, const void*) { return true; };
  ASSERT_TRUE(BookmarkFile::save(BOOK, original, options));
  std::vector<BookmarkEntry> loaded{entry()};
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded));
  EXPECT_TRUE(loaded.empty());
  EXPECT_EQ(bytes(path()), "{\"bookmarks\":[]}");
}

TEST_F(BookmarkPersistenceTest, MissingOptionalFieldsRemainCompatibleWithLegacyBookmarks) {
  fake::add(path(), "{\"bookmarks\":[{\"xpath\":\"/old\",\"percentage\":0.5,\"summary\":\"old\"}]}");
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded));
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded[0].xpath, "/old");
  EXPECT_EQ(loaded[0].computedSpineIndex, 0);
  EXPECT_FALSE(loaded[0].hasVisibleTextOffset);
  EXPECT_TRUE(loaded[0].name.empty());
}

TEST_F(BookmarkPersistenceTest, ExactZeroVisibleOffsetIsPersistedAsPresent) {
  BookmarkEntry zero = entry();
  zero.visibleTextOffset = 0;
  ASSERT_TRUE(BookmarkFile::save(BOOK, {zero}));
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded));
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_TRUE(loaded[0].hasVisibleTextOffset);
  EXPECT_EQ(loaded[0].visibleTextOffset, 0u);
}

TEST_F(BookmarkPersistenceTest, OversizedLiveOrProposedNameCannotReplaceCommittedBookmarks) {
  const std::vector<BookmarkEntry> original{entry()};
  ASSERT_TRUE(BookmarkFile::save(BOOK, original));
  const std::string before = bytes(path());
  BookmarkEntry oversized = entry();
  oversized.name.assign(BookmarkEntry::MAX_NAME_LENGTH + 1, 'x');
  EXPECT_FALSE(BookmarkFile::save(BOOK, {oversized}));
  BookmarkFile::SaveOptions options;
  options.prepend = &oversized;
  EXPECT_FALSE(BookmarkFile::save(BOOK, original, options));
  EXPECT_EQ(bytes(path()), before);
  options.prepend = nullptr;
  options.exclude = [](const BookmarkEntry&, const void*) { return true; };
  EXPECT_TRUE(BookmarkFile::save(BOOK, {oversized}, options));
}

TEST_F(BookmarkPersistenceTest, ExactMaximumNameLengthRoundTrips) {
  BookmarkEntry value = entry();
  value.name.assign(BookmarkEntry::MAX_NAME_LENGTH, 'x');
  ASSERT_TRUE(BookmarkFile::save(BOOK, {value}));
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded));
  ASSERT_EQ(loaded.size(), 1u);
  expectEntry(loaded[0], value);
}

TEST_F(BookmarkPersistenceTest, ShortArduinoStringSerializationCannotReplaceCommittedJson) {
  JsonDocument doc;
  doc["long"] = std::string(4096, 'x');
  const std::string before = "{\"committed\":true}";
  for (const size_t capacity : {0u, 1u, 63u, 64u, 65u, 128u, 1024u}) {
    SCOPED_TRACE(capacity);
    fake::reset();
    string_test::reset();
    fake::add(SETTINGS_PATH, before);
    string_test::capacity = capacity;
    EXPECT_FALSE(PersistableStoreBase::writeDocToFile(SETTINGS_PATH, doc));
    EXPECT_TRUE(string_test::allocationFailed);
    EXPECT_EQ(bytes(SETTINGS_PATH), before);
    EXPECT_FALSE(Storage.exists("/.crosspoint/test-settings.json.new"));
  }
}

TEST_F(BookmarkPersistenceTest, OverflowedJsonDocumentIsRejectedBeforeStorageWrites) {
  RejectAllocator allocator;
  JsonDocument doc(&allocator);
  doc["bookmarks"].to<JsonArray>().add<JsonObject>()["name"] = "lost";
  ASSERT_TRUE(doc.overflowed());
  fake::add(SETTINGS_PATH, "{\"old\":true}");
  EXPECT_FALSE(PersistableStoreBase::writeDocToFile(SETTINGS_PATH, doc));
  EXPECT_EQ(bytes(SETTINGS_PATH), "{\"old\":true}");
  EXPECT_TRUE(fake::writesByPath.empty());
}

TEST_F(BookmarkPersistenceTest, JsonParserAllocationFailureReturnsFalse) {
  fake::add(SETTINGS_PATH, "{\"bookmarks\":[{\"name\":\"content\"}]}");
  RejectAllocator allocator;
  JsonDocument doc(&allocator);
  EXPECT_FALSE(PersistableStoreBase::readDocFromFile(SETTINGS_PATH, doc));
  EXPECT_TRUE(doc.overflowed());
  EXPECT_TRUE(fake::writesByPath.empty());
}

TEST_F(BookmarkPersistenceTest, MissingEmptyAndMalformedFilesClearTheLoadedList) {
  for (const char* content : {"", "{", "{\"bookmarks\": ["}) {
    fake::reset();
    std::vector<BookmarkEntry> loaded{entry()};
    EXPECT_FALSE(BookmarkFile::load(BOOK, loaded));
    EXPECT_TRUE(loaded.empty());
    fake::add(path(), content);
    loaded.push_back(entry());
    EXPECT_FALSE(BookmarkFile::load(BOOK, loaded));
    EXPECT_TRUE(loaded.empty());
  }
}

TEST_F(BookmarkPersistenceTest, ExactMaximumDocumentSizeRemainsReadable) {
  JsonDocument doc;
  doc["payload"] = "";
  const size_t overhead = measureJson(doc);
  doc["payload"] = std::string(atomic_file::MAX_FILE_BYTES - overhead, 'x');
  ASSERT_EQ(measureJson(doc), atomic_file::MAX_FILE_BYTES);
  ASSERT_TRUE(PersistableStoreBase::writeDocToFile(SETTINGS_PATH, doc));
  JsonDocument loaded;
  ASSERT_TRUE(PersistableStoreBase::readDocFromFile(SETTINGS_PATH, loaded));
  EXPECT_EQ(loaded["payload"].as<std::string>(), doc["payload"].as<std::string>());
}

TEST_F(BookmarkPersistenceTest, OversizedDocumentIsRejectedBeforeSerializationOrCommit) {
  JsonDocument previous;
  previous["before"] = true;
  ASSERT_TRUE(PersistableStoreBase::writeDocToFile(SETTINGS_PATH, previous));
  const std::string before = bytes(SETTINGS_PATH);
  JsonDocument proposed;
  proposed["payload"] = std::string(atomic_file::MAX_FILE_BYTES, 'x');
  string_test::reset();
  ASSERT_FALSE(PersistableStoreBase::writeDocToFile(SETTINGS_PATH, proposed));
  EXPECT_EQ(bytes(SETTINGS_PATH), before);
  EXPECT_EQ(string_test::concatCalls, 0u);
}

TEST_F(BookmarkPersistenceTest, OversizedOnDiskDocumentIsRejectedBeforeAllocation) {
  fake::add(SETTINGS_PATH, std::string(atomic_file::MAX_FILE_BYTES + 1, ' '));
  JsonDocument doc;
  EXPECT_FALSE(PersistableStoreBase::readDocFromFile(SETTINGS_PATH, doc));
  EXPECT_EQ(string_test::reserveCalls, 0u);
  EXPECT_EQ(string_test::concatCalls, 0u);
}

TEST_F(BookmarkPersistenceTest, ReadStringAllocationFailureDoesNotReturnPartialJson) {
  fake::add(SETTINGS_PATH, "{\"payload\":\"" + std::string(512, 'x') + "\"}");
  for (const bool failReserve : {true, false}) {
    string_test::reset();
    string_test::failReserve = failReserve;
    if (!failReserve) string_test::concatsBeforeFailure = 1;
    JsonDocument doc;
    EXPECT_FALSE(PersistableStoreBase::readDocFromFile(SETTINGS_PATH, doc));
    EXPECT_TRUE(string_test::allocationFailed);
    EXPECT_TRUE(doc.isNull());
  }
}

TEST_F(BookmarkPersistenceTest, PathAllocationFailureDoesNotReplaceSavedBookmarks) {
  ASSERT_TRUE(BookmarkFile::save(BOOK, {entry("/old")}));
  const std::string before = bytes(path());
  fake::failAlloc = 0;
  EXPECT_FALSE(BookmarkFile::save(BOOK, {entry("/new")}));
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_EQ(bytes(path()), before);
}

TEST_F(BookmarkPersistenceTest, FailedAtomicInstallKeepsPriorBookmarkDocumentReadable) {
  const std::vector<BookmarkEntry> original{entry("/old")};
  ASSERT_TRUE(BookmarkFile::save(BOOK, original));
  fake::failRename = 1;
  EXPECT_FALSE(BookmarkFile::save(BOOK, {entry("/new")}));
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded));
  ASSERT_EQ(loaded.size(), 1u);
  expectEntry(loaded[0], original[0]);
  ASSERT_TRUE(BookmarkFile::save(BOOK, {entry("/new")}));
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded));
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded[0].xpath, "/new");
}

TEST_F(BookmarkPersistenceTest, InterruptedCommitLoadsBackupRatherThanUncommittedPrimary) {
  ASSERT_TRUE(BookmarkFile::save(BOOK, {entry("/old")}));
  fake::failRemove = 0;
  EXPECT_FALSE(BookmarkFile::save(BOOK, {entry("/new")}));
  ASSERT_TRUE(Storage.exists((path() + ".bak").c_str()));
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded));
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded[0].xpath, "/old");
}
TEST_F(BookmarkPersistenceTest, MissingBookmarksAreDistinguishedFromUnreadableFiles) {
  bool exists = true;
  std::vector<BookmarkEntry> loaded{entry()};
  EXPECT_FALSE(BookmarkFile::load(BOOK, loaded, &exists));
  EXPECT_FALSE(exists);
  EXPECT_TRUE(loaded.empty());
  for (const char* content : {"", "{", "{\"bookmarks\": ["}) {
    fake::add(path(), content);
    exists = false;
    loaded.push_back(entry());
    EXPECT_FALSE(BookmarkFile::load(BOOK, loaded, &exists));
    EXPECT_TRUE(exists);
    EXPECT_TRUE(loaded.empty());
  }
}

TEST_F(BookmarkPersistenceTest, InvalidBookmarkShapesDoNotPublishAnyEntries) {
  for (const char* content : {"{}", "[]", "null", "{\"bookmarks\":null}", "{\"bookmarks\":{}}", "{\"bookmarks\":1}",
                              "{\"bookmarks\":[{},null]}", "{\"bookmarks\":[{},1]}", "{\"bookmarks\":[{},[]]}",
                              "{\"bookmarks\":[{},\"text\"]}"}) {
    SCOPED_TRACE(content);
    fake::add(path(), content);
    const std::string before = bytes(path());
    bool exists = false;
    std::vector<BookmarkEntry> loaded{entry()};
    EXPECT_FALSE(BookmarkFile::load(BOOK, loaded, &exists));
    EXPECT_TRUE(exists);
    EXPECT_TRUE(loaded.empty());
    EXPECT_EQ(bytes(path()), before);
  }
}

TEST_F(BookmarkPersistenceTest, ObjectEntriesRetainLegacyOptionalFieldDefaults) {
  fake::add(path(), "{\"bookmarks\":[{}]}");
  bool exists = false;
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded, &exists));
  EXPECT_TRUE(exists);
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_TRUE(loaded[0].xpath.empty());
  EXPECT_TRUE(loaded[0].name.empty());
  EXPECT_FLOAT_EQ(loaded[0].percentage, 0.0f);
  EXPECT_FALSE(loaded[0].hasVisibleTextOffset);
}

TEST_F(BookmarkPersistenceTest, ReadOpenAndAllocationFailuresCannotMasqueradeAsMissing) {
  ASSERT_TRUE(BookmarkFile::save(BOOK, {entry()}));
  for (int failure = 0; failure < 4; ++failure) {
    SCOPED_TRACE(failure);
    fake::failOpenPath.clear();
    fake::failRead = -1;
    fake::failAlloc = -1;
    string_test::reset();
    if (failure == 0) fake::failOpenPath = path();
    if (failure == 1) fake::failRead = 0;
    if (failure == 2) fake::failAlloc = 0;
    if (failure == 3) string_test::failReserve = true;
    bool exists = false;
    std::vector<BookmarkEntry> loaded{entry()};
    EXPECT_FALSE(BookmarkFile::load(BOOK, loaded, &exists));
    EXPECT_TRUE(exists);
    EXPECT_TRUE(loaded.empty());
  }
}

TEST_F(BookmarkPersistenceTest, ParserOutOfMemoryPreservesExistingFileStatus) {
  fake::add(SETTINGS_PATH, "{\"bookmarks\":[{\"xpath\":\"/old\"}]}");
  RejectAllocator allocator;
  JsonDocument doc(&allocator);
  bool exists = false;
  EXPECT_FALSE(PersistableStoreBase::readDocFromFile(SETTINGS_PATH, doc, &exists));
  EXPECT_TRUE(exists);
  EXPECT_TRUE(doc.overflowed());
}

TEST_F(BookmarkPersistenceTest, BackupOnlyIsAnExistingReadableBookmarkDocument) {
  ASSERT_TRUE(BookmarkFile::save(BOOK, {entry("/old")}));
  ASSERT_TRUE(Storage.rename(path().c_str(), (path() + ".bak").c_str()));
  bool exists = false;
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(BOOK, loaded, &exists));
  EXPECT_TRUE(exists);
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded[0].xpath, "/old");
}

TEST_F(BookmarkPersistenceTest, UnreadableCommittedBackupDoesNotLoadUncommittedPrimary) {
  ASSERT_TRUE(BookmarkFile::save(BOOK, {entry("/uncommitted")}));
  fake::add(path() + ".bak", "{broken");
  bool exists = false;
  std::vector<BookmarkEntry> loaded{entry()};
  EXPECT_FALSE(BookmarkFile::load(BOOK, loaded, &exists));
  EXPECT_TRUE(exists);
  EXPECT_TRUE(loaded.empty());
}

}  // namespace
