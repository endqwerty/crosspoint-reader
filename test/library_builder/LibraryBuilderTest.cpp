#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "Epub.h"
#include "LibraryBuilder.h"
#include "LibraryIndexFile.h"
#include "LibrarySession.h"
#include "LibraryText.h"

using namespace library;

namespace {

constexpr char INDEX[] = "/.crosspoint/library.idx";

std::string numbered(const char* prefix, const unsigned value) {
  char text[32];
  std::snprintf(text, sizeof(text), "%s%04u", prefix, value);
  return text;
}

std::string pathAt(LibraryIndexFile& index, const SortOrder order, const uint16_t row) {
  const uint16_t ordinal = index.ordinalForRow(order, row);
  if (ordinal == 0xFFFF) return {};
  ClixRecord record{};
  if (!index.readRecord(ordinal, record)) return {};
  std::string path;
  return index.readPath(record, path) ? path : std::string();
}

class LibraryBuilderTest : public ::testing::Test {
 protected:
  BuildStats stats;

  void SetUp() override {
    fake::reset();
    bookMetadata.clear();
    fake::add("/a.epub");
    fake::add("/b.epub");
  }

  void initial() { ASSERT_TRUE(buildLibraryIndex("/", stats, true)); }

  void sharedSurnameAuthors() {
    fake::add("/c.epub");
    fake::add("/d.epub");
    bookMetadata["/a.epub"] = {"Alpha", "John Longsharedsurname", "", ""};
    bookMetadata["/b.epub"] = {"Beta", "Mary Longsharedsurname", "", ""};
    bookMetadata["/c.epub"] = {"Gamma", "John Longsharedsurname", "", ""};
    bookMetadata["/d.epub"] = {"Omega", "Mary Longsharedsurname", "", ""};
  }

  void staleAuthorSort() {
    auto& bytes = fake::files[INDEX]->bytes;
    ClixHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    header.foldVersion = CLIX_FOLD_VERSION - 1;
    std::memcpy(bytes.data(), &header, sizeof(header));
    for (uint16_t row = 0; row < header.bookCount; row++)
      std::memcpy(bytes.data() + authorOrderOffset(header, row), &row, sizeof(row));
  }
};

}  // namespace

TEST_F(LibraryBuilderTest, UnchangedRebuildReusesMetadataAndDoesNotReplaceIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 0u);
  EXPECT_EQ(stats.parsed, 0);
  EXPECT_EQ(stats.metadataReused, 2);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, UnchangedMetadataUsesOneBoundedBlobReadPerBook) {
  constexpr unsigned COUNT = 512;
  fake::reset();
  bookMetadata.clear();
  for (unsigned i = 0; i < COUNT; ++i) {
    const std::string path = "/" + numbered("book", i) + ".epub";
    fake::add(path);
    bookMetadata[path] = {numbered("Example Book ", i), "Example Writer", "", ""};
  }
  initial();
  const auto committed = fake::files[INDEX]->bytes;
  fake::parses = 0;
  fake::resetIoCounters();

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(stats.metadataReused, COUNT);
  EXPECT_EQ(fake::parses, 0u);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, committed);
  // Header, reconciliation record/hash, reuse record/metadata/series reference.
  EXPECT_LE(fake::reads, 1u + 5u * COUNT);
  EXPECT_LE(fake::seeks, 5u * COUNT);
}

TEST_F(LibraryBuilderTest, ExternalDeletionAndAdditionRefreshEverySortOrder) {
  fake::add("/nested/deleted.epub", "old book");
  initial();
  ASSERT_TRUE(Storage.remove("/nested/deleted.epub"));
  fake::add("/nested/added.epub", "new different length book");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.removed, 1);
  EXPECT_EQ(stats.added, 1);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.bookCount(), 3);
  for (const auto order : {SortOrder::AddedAsc, SortOrder::AddedDesc, SortOrder::TitleAsc, SortOrder::TitleDesc,
                           SortOrder::AuthorAsc, SortOrder::AuthorDesc}) {
    std::vector<std::string> paths;
    paths.reserve(index.bookCount());
    for (uint16_t row = 0; row < index.bookCount(); ++row) paths.push_back(pathAt(index, order, row));
    EXPECT_EQ(std::count(paths.begin(), paths.end(), "/nested/deleted.epub"), 0);
    EXPECT_EQ(std::count(paths.begin(), paths.end(), "/nested/added.epub"), 1);
  }
}

TEST_F(LibraryBuilderTest, DeletingEveryBookInstallsEmptyIndexAndCountsRemovals) {
  initial();
  ASSERT_TRUE(Storage.remove("/a.epub"));
  ASSERT_TRUE(Storage.remove("/b.epub"));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.removed, 2);
  EXPECT_EQ(stats.books, 0);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.bookCount(), 0);
}

TEST_F(LibraryBuilderTest, UnreadableRootPreservesPreviousIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::failOpenPath = "/";
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, UnreadableSubfolderPreservesPreviousIndexAndCanRetry) {
  fake::add("/nested/deleted.epub");
  initial();
  const auto old = fake::files[INDEX]->bytes;
  ASSERT_TRUE(Storage.remove("/nested/deleted.epub"));
  fake::failOpenPath = "/nested";
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failOpenPath.clear();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.removed, 1);
  EXPECT_EQ(stats.books, 2);
}

TEST_F(LibraryBuilderTest, FolderHeavyUnchangedReconciliationIoScalesLinearly) {
  const auto measure = [this](const unsigned count) {
    fake::reset();
    bookMetadata.clear();
    for (unsigned i = 0; i < count; i++) {
      fake::add("/folder" + numbered("", i) + "/book.txt");
    }
    if (!buildLibraryIndex("/", stats, false)) {
      ADD_FAILURE() << "initial build failed for " << count << " books";
      return 0u;
    }
    fake::resetIoCounters();
    if (!buildLibraryIndex("/", stats, false)) {
      ADD_FAILURE() << "unchanged build failed for " << count << " books";
      return 0u;
    }
    EXPECT_EQ(stats.metadataReused, count);
    EXPECT_FALSE(stats.indexReplaced);
    return fake::reads + fake::seeks;
  };

  const unsigned smallIo = measure(128);
  const unsigned largeIo = measure(256);
  EXPECT_LT(largeIo, smallIo * 3u);
}

TEST_F(LibraryBuilderTest, DirectoryEntriesAreEnumeratedOnce) {
  fake::add("/folder/c.txt");

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));

  EXPECT_EQ(fake::directoryEntriesByPath["/a.epub"], 1u);
  EXPECT_EQ(fake::directoryEntriesByPath["/b.epub"], 1u);
  EXPECT_EQ(fake::directoryEntriesByPath["/folder"], 1u);
  EXPECT_EQ(fake::directoryEntriesByPath["/folder/c.txt"], 1u);
}

TEST_F(LibraryBuilderTest, DirectoryResumeFailureRetainsPreviousIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::add("/aa-folder/c.txt");
  fake::failDirectorySeek = true;

  EXPECT_FALSE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, StagingAndIndexWritesAreBatched) {
  fake::reset();
  for (unsigned i = 0; i < 128; i++) fake::add("/book" + numbered("", i) + ".txt");

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));

  EXPECT_LT(fake::writesByPath["/.crosspoint/library.stage"], 64u);
  EXPECT_LT(fake::writesByPath["/.crosspoint/library.new"], 32u);
}

TEST_F(LibraryBuilderTest, ParentDuplicateTrackingSurvivesDirectoryRecursion) {
  fake::add("/folder/c.txt");
  fake::duplicateDirectoryEntry("/a.epub");

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));

  EXPECT_EQ(stats.books, 3);
  EXPECT_EQ(stats.duplicatesDropped, 1);
}

TEST_F(LibraryBuilderTest, TimestampAndSizeChangesParseOnlyTheChangedBook) {
  initial();
  fake::files["/a.epub"]->time++;
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 1);

  fake::files["/b.epub"]->bytes.push_back('x');
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 1);
}

TEST_F(LibraryBuilderTest, ZeroTimestampAndFailedExtractionAreNeverFresh) {
  fake::files["/a.epub"]->time = 0;
  bookMetadata["/b.epub"].success = false;
  initial();
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 2u);
  EXPECT_EQ(stats.metadataReused, 0);
  EXPECT_TRUE(stats.indexReplaced);
}

TEST_F(LibraryBuilderTest, MetadataModeChangesInvalidateCachedMetadata) {
  initial();
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(fake::parses, 0u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().metadataEnabled, 0);
  index.close();

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 2u);
}

TEST_F(LibraryBuilderTest, RebuildVotesFromSourceAuthorInsteadOfPriorCanonicalAuthor) {
  fake::add("/c.epub");
  bookMetadata["/a.epub"].author = "Victor Hugo";
  bookMetadata["/b.epub"].author = "Hugo Victor";
  bookMetadata["/c.epub"].author = "Hugo Victor";
  initial();
  ASSERT_TRUE(Storage.remove("/b.epub"));
  ASSERT_TRUE(Storage.remove("/c.epub"));
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 0u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ClixRecord record{};
  std::string author;
  ASSERT_TRUE(index.readRecord(0, record));
  ASSERT_TRUE(index.readAuthor(record, author));
  EXPECT_EQ(author, "Victor Hugo");
}

TEST_F(LibraryBuilderTest, SharedForenamePrefixesDoNotMergeDifferentAuthors) {
  fake::add("/c.epub");
  fake::add("/d.epub");
  bookMetadata["/a.epub"] = {"Alpha", "Christopher Tolkien", "", ""};
  bookMetadata["/b.epub"] = {"Beta", "Christopher Priest", "", ""};
  bookMetadata["/c.epub"] = {"Gamma", "Christopher Paolini", "", ""};
  bookMetadata["/d.epub"] = {"Delta", "Tolkien, Christopher", "", ""};
  initial();

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  const char* expectedPaths[] = {"/c.epub", "/b.epub", "/a.epub", "/d.epub"};
  const char* expectedAuthors[] = {"Christopher Paolini", "Christopher Priest", "Christopher Tolkien",
                                   "Christopher Tolkien"};
  for (uint16_t row = 0; row < 4; ++row) {
    ClixRecord record{};
    std::string author;
    ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::AuthorAsc, row), record));
    ASSERT_TRUE(index.readAuthor(record, author));
    EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, row), expectedPaths[row]);
    EXPECT_EQ(author, expectedAuthors[row]);
  }
}

TEST_F(LibraryBuilderTest, SharedSurnamePrefixesKeepEachAuthorsBooksContiguous) {
  sharedSurnameAuthors();
  initial();
  EXPECT_FALSE(stats.ranksDegraded);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  const char* expected[] = {"/a.epub", "/c.epub", "/b.epub", "/d.epub"};
  for (uint16_t row = 0; row < 4; row++) {
    EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, row), expected[row]);
    EXPECT_EQ(pathAt(index, SortOrder::AuthorDesc, row), expected[3 - row]);
  }
  ClixRecord record{};
  std::string author;
  for (uint16_t row = 0; row < 4; row++) {
    ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::AuthorAsc, row), record));
    ASSERT_TRUE(index.readAuthor(record, author));
    EXPECT_EQ(author, row < 2 ? "John Longsharedsurname" : "Mary Longsharedsurname");
  }
}

TEST_F(LibraryBuilderTest, StaleAuthorSortRebuildsUnchangedBooksAndPreservesArrivalHistory) {
  sharedSurnameAuthors();
  initial();
  staleAuthorSort();
  auto& bytes = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  header.nextFirstSeen = 41;
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t arrival[] = {10, 30, 20, 40};
  const uint16_t arrivalOrder[] = {0, 2, 1, 3};
  for (uint16_t row = 0; row < 4; row++) {
    std::memcpy(bytes.data() + recordOffset(header, row) + offsetof(ClixRecord, firstSeen), &arrival[row],
                sizeof(arrival[row]));
    std::memcpy(bytes.data() + arrivalOrderOffset(header, row), &arrivalOrder[row], sizeof(arrivalOrder[row]));
  }
  LibraryIndexFile index;
  EXPECT_FALSE(index.open(INDEX));
  ASSERT_TRUE(index.openForReconciliation(INDEX));
  EXPECT_EQ(index.header().foldVersion, CLIX_FOLD_VERSION - 1);
  index.close();
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_TRUE(stats.indexReplaced);
  EXPECT_EQ(stats.unchanged, 4);
  EXPECT_EQ(stats.parsed, 4);
  EXPECT_EQ(stats.metadataReused, 0);
  EXPECT_EQ(fake::parses, 4u);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().foldVersion, CLIX_FOLD_VERSION);
  EXPECT_EQ(index.header().nextFirstSeen, 41);
  const char* expected[] = {"/a.epub", "/c.epub", "/b.epub", "/d.epub"};
  for (uint16_t row = 0; row < 4; row++) {
    EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, row), expected[row]);
    EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, row), expected[row]);
    ClixRecord record{};
    ASSERT_TRUE(index.readRecord(row, record));
    EXPECT_EQ(record.firstSeen, arrival[row]);
  }
}

TEST_F(LibraryBuilderTest, FailedAuthorSortUpgradeRetainsOldIndexForRetry) {
  sharedSurnameAuthors();
  initial();
  staleAuthorSort();
  const auto old = fake::files[INDEX]->bytes;
  fake::partialWritePath = "/.crosspoint/library.new";
  fake::partialWriteBytes = 7;

  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  ASSERT_TRUE(fake::failureTriggered);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, old);

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(stats.indexReplaced);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().foldVersion, CLIX_FOLD_VERSION);
  EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, 0), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, 1), "/c.epub");
}

TEST_F(LibraryBuilderTest, EqualBasenamesInDifferentFoldersReconcileIndependently) {
  fake::add("/one/same.epub");
  fake::add("/two/same.epub");
  bookMetadata["/one/same.epub"].title = "One";
  bookMetadata["/two/same.epub"].title = "Two";
  initial();
  fake::files["/two/same.epub"]->time++;
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 3);
}

TEST_F(LibraryBuilderTest, AddedRemovedMovedAndRenamedBooksKeepArrivalOrder) {
  initial();
  fake::add("/c.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 0), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 1), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 2), "/c.epub");
  index.close();

  ASSERT_TRUE(Storage.remove("/b.epub"));
  ASSERT_TRUE(Storage.rename("/a.epub", "/moved.epub"));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.removed, 1);
  EXPECT_EQ(stats.renamed, 1);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 0), "/moved.epub");
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 1), "/c.epub");
  index.close();

  ASSERT_TRUE(Storage.rename("/moved.epub", "/renamed.epub"));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.renamed, 1);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 0), "/renamed.epub");
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 1), "/c.epub");
}

TEST_F(LibraryBuilderTest, WholeFolderRenameWithUniqueSizePreservesArrivalOrder) {
  fake::add("/old/unique.epub", "a uniquely sized book");
  initial();
  ASSERT_TRUE(Storage.mkdir("/new"));
  ASSERT_TRUE(Storage.rename("/old/unique.epub", "/new/unique.epub"));
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(stats.renamed, 1);
  EXPECT_EQ(stats.removed, 0);
  EXPECT_EQ(fake::parses, 1u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 2), "/new/unique.epub");
}

TEST_F(LibraryBuilderTest, DuplicateDetectionRemainsBoundedAndFindsTrackedKeysAfterTheCap) {
  fake::duplicateDirectoryEntry("/a.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.books, 2);
  EXPECT_EQ(stats.duplicatesDropped, 1);
  EXPECT_FALSE(stats.dedupDegraded);

  fake::reset();
  bookMetadata.clear();
  for (unsigned i = 0; i <= LIBRARY_MAX_DEDUP_KEYS; i++) {
    fake::add("/book" + numbered("", i) + ".txt");
  }
  fake::duplicateDirectoryEntry("/book0000.txt");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(stats.books, LIBRARY_MAX_DEDUP_KEYS + 1);
  EXPECT_EQ(stats.duplicatesDropped, 1);
  EXPECT_TRUE(stats.dedupDegraded);
  EXPECT_LT(fake::delays, 2000u);
}

TEST_F(LibraryBuilderTest, ReadWriteCloseAndAllocationFailuresRetainPreviousIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;

  fake::failRead = 0;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failRead = -1;

  fake::files["/a.epub"]->time++;
  fake::failWrite = 0;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failWrite = -1;

  fake::failWritePath = "/.crosspoint/library.new";
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);

  fake::failClosePath = "/.crosspoint/library.new";
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);

  fake::failAlloc = 3;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failAlloc = -1;

  fake::failRename = 1;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, TruncatedPersistedPathHashAbortsAndRetainsTheLiveIndex) {
  initial();
  auto& bytes = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  ClixRecord record{};
  std::memcpy(&record, bytes.data() + recordOffset(header, 0), sizeof(record));
  record.nameOff = header.nameLen - 4;
  std::memcpy(bytes.data() + recordOffset(header, 0), &record, sizeof(record));
  const auto corrupted = bytes;

  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, corrupted);
  EXPECT_FALSE(Storage.exists("/.crosspoint/library.stage"));
  EXPECT_FALSE(Storage.exists("/.crosspoint/library.stage.f"));
}

TEST_F(LibraryBuilderTest, PartialStageAndIndexWritesPreserveCommittedIndexAndCanRetry) {
  for (const char* path : {"/.crosspoint/library.stage", "/.crosspoint/library.new"}) {
    SCOPED_TRACE(path);
    fake::reset();
    fake::add("/a.epub");
    initial();
    const auto committed = fake::files[INDEX]->bytes;
    fake::add("/b.epub");
    fake::partialWritePath = path;
    fake::partialWriteBytes = 7;
    EXPECT_FALSE(buildLibraryIndex("/", stats, true));
    ASSERT_TRUE(fake::failureTriggered);
    ASSERT_TRUE(Storage.exists(INDEX));
    EXPECT_EQ(fake::files[INDEX]->bytes, committed);
    EXPECT_FALSE(Storage.exists("/.crosspoint/library.bak"));
    ASSERT_TRUE(buildLibraryIndex("/", stats, true));
    EXPECT_EQ(stats.books, 2);
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(INDEX));
    EXPECT_EQ(index.bookCount(), 2);
  }
}

TEST_F(LibraryBuilderTest, FailedInstallAndRollbackRetainBackupUntilNextRebuild) {
  initial();
  const auto committed = fake::files[INDEX]->bytes;
  fake::add("/c.epub");
  constexpr char BACKUP[] = "/.crosspoint/library.bak";
  constexpr char TEMP[] = "/.crosspoint/library.new";
  fake::blockedRenames = {{TEMP, INDEX}, {BACKUP, INDEX}};
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_FALSE(Storage.exists(INDEX));
  ASSERT_TRUE(Storage.exists(BACKUP));
  EXPECT_EQ(fake::files[BACKUP]->bytes, committed);
  EXPECT_FALSE(Storage.exists(TEMP));
  fake::blockedRenames.clear();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.books, 3);
  EXPECT_FALSE(Storage.exists(BACKUP));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.bookCount(), 3);
}

TEST_F(LibraryBuilderTest, MalformedLiveRecordRetainsAUsableCommittedIndex) {
  initial();
  constexpr char BACKUP[] = "/.crosspoint/library.bak";
  const auto committed = fake::files[INDEX]->bytes;
  fake::add(BACKUP);
  fake::files[BACKUP]->bytes = committed;
  ClixHeader header{};
  std::memcpy(&header, committed.data(), sizeof(header));
  auto& live = fake::files[INDEX]->bytes;
  live[recordOffset(header, header.bookCount - 1) + offsetof(ClixRecord, metadataStatus)] = 255;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  ASSERT_TRUE(Storage.exists(INDEX));
  EXPECT_EQ(fake::files[INDEX]->bytes, committed);
  EXPECT_FALSE(Storage.exists(BACKUP));
  EXPECT_EQ(stats.metadataReused, 2);
  LibraryIndexFile recovered;
  ASSERT_TRUE(recovered.open(INDEX));
  ASSERT_EQ(recovered.bookCount(), 2);
  for (uint16_t ordinal = 0; ordinal < recovered.bookCount(); ++ordinal) {
    ClixRecord record{};
    ASSERT_TRUE(recovered.readRecord(ordinal, record));
    std::string path;
    ASSERT_TRUE(recovered.readPath(record, path));
    EXPECT_TRUE(Storage.exists(path.c_str()));
  }
}

TEST_F(LibraryBuilderTest, RecoveryRecordIoFailuresPreserveBothCopiesAndCanRetry) {
  constexpr char BACKUP[] = "/.crosspoint/library.bak";
  for (const bool validateBackup : {false, true}) {
    for (const bool readFailure : {false, true}) {
      for (unsigned operation = 0; operation < 4; ++operation) {
        SCOPED_TRACE(validateBackup);
        SCOPED_TRACE(readFailure);
        SCOPED_TRACE(operation);
        fake::reset();
        fake::add("/a.epub");
        fake::add("/b.epub");
        initial();
        const auto committed = fake::files[INDEX]->bytes;
        fake::add(BACKUP);
        fake::files[BACKUP]->bytes = committed;
        if (validateBackup) {
          ClixHeader header{};
          std::memcpy(&header, committed.data(), sizeof(header));
          fake::files[INDEX]->bytes[recordOffset(header, 0) + offsetof(ClixRecord, metadataStatus)] = 255;
        }
        const auto live = fake::files[INDEX]->bytes;

        // Each record has a record read followed by a path-hash read. Backup
        // validation follows the rejected live header and first record.
        if (readFailure)
          fake::failRead = static_cast<int>(operation + (validateBackup ? 3 : 1));
        else
          fake::failSeek = static_cast<int>(operation + (validateBackup ? 1 : 0));
        EXPECT_FALSE(buildLibraryIndex("/", stats, true));
        ASSERT_TRUE(fake::failureTriggered);
        ASSERT_TRUE(Storage.exists(INDEX));
        ASSERT_TRUE(Storage.exists(BACKUP));
        EXPECT_EQ(fake::files[INDEX]->bytes, live);
        EXPECT_EQ(fake::files[BACKUP]->bytes, committed);
        EXPECT_FALSE(stats.indexReplaced);
        EXPECT_FALSE(Storage.exists("/.crosspoint/library.stage"));
        EXPECT_FALSE(Storage.exists("/.crosspoint/library.new"));

        ASSERT_TRUE(buildLibraryIndex("/", stats, true));
        EXPECT_EQ(fake::files[INDEX]->bytes, committed);
        EXPECT_FALSE(Storage.exists(BACKUP));
        EXPECT_EQ(stats.metadataReused, 2);
      }
    }
  }
}

TEST_F(LibraryBuilderTest, LibrariesPastOldGateAndAtFormatCeilingKeepAllOrders) {
  for (const unsigned count : {513u, static_cast<unsigned>(CLIX_MAX_RECORDS)}) {
    fake::reset();
    bookMetadata.clear();
    std::vector<unsigned> authorOrder(count);
    std::iota(authorOrder.begin(), authorOrder.end(), 0u);
    for (unsigned i = 0; i < count; i++) {
      const std::string path = "/book" + numbered("", i) + ".epub";
      fake::add(path);
      bookMetadata[path].title = numbered("Title ", count - 1 - i);
      bookMetadata[path].author = numbered("Writer ", (i * (count == 513 ? 257u : 2053u)) % count);
    }

    ASSERT_TRUE(buildLibraryIndex("/", stats, true)) << count;
    ASSERT_EQ(stats.books, count);
    EXPECT_FALSE(stats.ranksDegraded);

    if (count == CLIX_MAX_RECORDS) {
      const auto old = fake::files[INDEX]->bytes;
      fake::parses = 0;
      fake::resetIoCounters();
      ASSERT_TRUE(buildLibraryIndex("/", stats, true));
      EXPECT_EQ(fake::parses, 0u);
      EXPECT_EQ(stats.metadataReused, CLIX_MAX_RECORDS);
      EXPECT_FALSE(stats.indexReplaced);
      EXPECT_EQ(fake::files[INDEX]->bytes, old);
      EXPECT_LT(fake::delays, 10000u);
    }

    std::sort(authorOrder.begin(), authorOrder.end(), [count](const unsigned a, const unsigned b) {
      return (a * (count == 513 ? 257u : 2053u)) % count < (b * (count == 513 ? 257u : 2053u)) % count;
    });
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(INDEX));
    for (uint16_t row = 0; row < count; row++) {
      EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, row), "/book" + numbered("", row) + ".epub") << count << ':' << row;
      EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, row), "/book" + numbered("", count - 1 - row) + ".epub")
          << count << ':' << row;
      EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, row), "/book" + numbered("", authorOrder[row]) + ".epub")
          << count << ':' << row;
    }
  }
}

TEST_F(LibraryBuilderTest, SortAllocationFailureProducesValidDegradedIndex) {
  fake::reset();
  for (unsigned i = 0; i < 513; i++) fake::add("/book" + numbered("", i) + ".txt");
  fake::failAlloc = 6;

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_TRUE(stats.ranksDegraded);
  EXPECT_TRUE(stats.indexReplaced);

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.bookCount(), 513);
}

TEST_F(LibraryBuilderTest, ExactRecordCapIsCompleteButOneMoreBookIsPartial) {
  fake::reset();
  bookMetadata.clear();
  for (unsigned i = 0; i < CLIX_MAX_RECORDS; ++i) fake::add("/" + numbered("book", i) + ".epub");
  fake::add("/z-unrelated.dat");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(stats.books, CLIX_MAX_RECORDS);
  EXPECT_FALSE(stats.limitsReached);
  EXPECT_GT(fake::delays, 0u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_FALSE(index.limitsReached());
  EXPECT_EQ(index.bookCount(), CLIX_MAX_RECORDS);
  EXPECT_FALSE(pathAt(index, SortOrder::TitleAsc, CLIX_MAX_RECORDS - 1).empty());
  index.close();
  fake::add("/zz-extra.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(stats.books, CLIX_MAX_RECORDS);
  EXPECT_TRUE(stats.limitsReached);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_TRUE(index.limitsReached());
}

TEST_F(LibraryBuilderTest, DeepFoldersAreReportedAsPartialWithoutRecursingIndefinitely) {
  std::string path;
  for (int i = 0; i <= LIBRARY_MAX_DEPTH; ++i) path += "/deep";
  fake::add(path + "/hidden.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(stats.books, 2);
  EXPECT_TRUE(stats.limitsReached);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_TRUE(index.limitsReached());
}

TEST_F(LibraryBuilderTest, SeriesPositionsAndStandaloneBooksHaveStableOrder) {
  bookMetadata["/a.epub"] = {"Z title", "Author", "The Earthsea", "2"};
  bookMetadata["/b.epub"] = {"A title", "Author", "The Earthsea", "0.5"};
  fake::add("/c.epub");
  bookMetadata["/c.epub"] = {"Standalone", "Author", "", ""};
  initial();
  EXPECT_EQ(stats.series, 1);
  EXPECT_EQ(stats.inSeries, 2);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 0), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 1), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 2), "/c.epub");
  ClixSeriesRef ref{};
  ASSERT_TRUE(index.readSeriesRef(index.ordinalForRow(SortOrder::SeriesAsc, 0), ref));
  EXPECT_EQ(ref.seriesIndex, 50);
}

TEST_F(LibraryBuilderTest, LongSeriesNamesDoNotMergeAfterDisplayTruncationOrReuse) {
  const std::string common(80, 'a');
  bookMetadata["/a.epub"] = {"First", "Author", common + " first", "2"};
  bookMetadata["/b.epub"] = {"Second", "Author", common + " second", "1"};
  initial();
  EXPECT_EQ(stats.series, 2);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ClixSeriesRef first{}, second{};
  ASSERT_TRUE(index.readSeriesRef(0, first));
  ASSERT_TRUE(index.readSeriesRef(1, second));
  EXPECT_NE(first.seriesId, second.seriesId);
  index.close();
  fake::add("/c.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.metadataReused, 2);
  EXPECT_EQ(stats.series, 2);
  ASSERT_TRUE(index.open(INDEX));
  ASSERT_TRUE(index.readSeriesRef(0, first));
  ASSERT_TRUE(index.readSeriesRef(1, second));
  EXPECT_NE(first.seriesId, second.seriesId);
}

TEST_F(LibraryBuilderTest, SeriesAllocationsAndStorageFailuresNeverReplaceCommittedIndex) {
  bookMetadata["/a.epub"].series = "Earthsea";
  bookMetadata["/b.epub"].series = "Earthsea";
  initial();
  const auto snapshot = fake::files;
  const auto committed = fake::files[INDEX]->bytes;
  fake::add("/c.epub");
  fake::resetIoCounters();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  const size_t allocations = fake::allocations.size();
  const unsigned readOperations = fake::reads;
  const unsigned seekOperations = fake::seeks;
  unsigned refused = 0;
  for (size_t attempt = 0; attempt < allocations; ++attempt) {
    fake::reset();
    fake::files = snapshot;
    fake::add("/c.epub");
    fake::failAlloc = static_cast<int>(attempt);
    if (!buildLibraryIndex("/", stats, true)) {
      ++refused;
      EXPECT_EQ(fake::files[INDEX]->bytes, committed) << attempt;
    }
  }
  EXPECT_GT(refused, 8u);
  for (const bool readFailure : {false, true}) {
    const unsigned count = readFailure ? readOperations : seekOperations;
    for (unsigned failure = 0; failure < count; ++failure) {
      SCOPED_TRACE(failure);
      fake::reset();
      fake::files = snapshot;
      fake::add("/c.epub");
      if (readFailure)
        fake::failRead = static_cast<int>(failure);
      else
        fake::failSeek = static_cast<int>(failure);
      if (!buildLibraryIndex("/", stats, true)) {
        EXPECT_EQ(fake::files[INDEX]->bytes, committed);
      } else {
        EXPECT_EQ(stats.series, 1);
        EXPECT_EQ(stats.inSeries, 2);
      }
    }
  }
}

TEST_F(LibraryBuilderTest, CorruptSeriesReferenceRefusesReconciliationInsteadOfDroppingMembership) {
  bookMetadata["/a.epub"].series = "Earthsea";
  initial();
  ClixHeader header{};
  memcpy(&header, fake::files[INDEX]->bytes.data(), sizeof(header));
  const uint16_t invalidSeries = 10;
  memcpy(fake::files[INDEX]->bytes.data() + header.seriesRefStart, &invalidSeries, sizeof(invalidSeries));
  const auto corrupt = fake::files[INDEX]->bytes;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, corrupt);
}

TEST_F(LibraryBuilderTest, StandaloneBuildDoesNotAllocatePerBookSeriesKeys) {
  for (unsigned i = 0; i < 100; ++i) fake::add("/" + numbered("book", i) + ".epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(stats.series, 0);
  EXPECT_EQ(stats.inSeries, 0);
  EXPECT_EQ(std::count(fake::allocations.begin(), fake::allocations.end(), stats.books * 24u), 0);
}

TEST_F(LibraryBuilderTest, MigratesVersionTwoArrivalHistoryAndReparsesMetadata) {
  initial();
  const auto current = fake::files[INDEX]->bytes;
  ClixHeader header{};
  memcpy(&header, current.data(), sizeof(header));
  const uint32_t legacyNames = alignUp(header.permStart + header.bookCount * 2u * sizeof(uint16_t));
  const uint32_t legacySize = legacyNames + header.nameLen;
  std::vector<uint8_t> legacy(legacySize, 0);
  std::copy_n(current.begin(), header.permStart + header.bookCount * 2u * sizeof(uint16_t), legacy.begin());
  std::copy_n(current.begin() + header.nameStart, header.nameLen, legacy.begin() + legacyNames);
  legacy[4] = 2;
  legacy[14] = legacy[15] = 0;
  memcpy(legacy.data() + 32, &legacyNames, 4);
  memcpy(legacy.data() + 36, &header.nameLen, 4);
  memcpy(legacy.data() + 40, &legacySize, 4);
  std::fill(legacy.begin() + 44, legacy.begin() + 64, 0);
  const uint16_t arrival = 127;
  const uint16_t nextArrival = 128;
  memcpy(legacy.data() + header.recordStart + offsetof(ClixRecord, firstSeen), &arrival, 2);
  memcpy(legacy.data() + 12, &nextArrival, 2);
  fake::files[INDEX]->bytes = legacy;
  LibraryIndexFile index;
  EXPECT_FALSE(index.open(INDEX));
  ASSERT_TRUE(index.openForReconciliation(INDEX));
  EXPECT_EQ(index.header().formatVersion, 2);
  EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, 0), "/a.epub");
  index.close();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.unchanged, 2);
  EXPECT_EQ(stats.parsed, 2);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().formatVersion, CLIX_FORMAT_VERSION);
  EXPECT_EQ(index.header().nextFirstSeen, nextArrival);
  ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  EXPECT_EQ(record.firstSeen, arrival);
}

TEST_F(LibraryBuilderTest, MigratesVersionThreeWithoutReusingTruncatedSeriesIdentity) {
  bookMetadata["/a.epub"].series = "Earthsea";
  initial();
  fake::files[INDEX]->bytes[4] = 3;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.unchanged, 2);
  EXPECT_EQ(stats.parsed, 2);
  EXPECT_EQ(stats.series, 1);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().formatVersion, CLIX_FORMAT_VERSION);
}

TEST_F(LibraryBuilderTest, MixedLibraryAllocatesSeriesKeysOnlyForGroupedBooks) {
  for (unsigned i = 0; i < 100; ++i) fake::add("/" + numbered("book", i) + ".epub");
  bookMetadata["/a.epub"].series = "Earthsea";
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.inSeries, 1);
  EXPECT_EQ(std::count(fake::allocations.begin(), fake::allocations.end(), 24u), 1);
  EXPECT_EQ(std::count(fake::allocations.begin(), fake::allocations.end(), stats.books * 24u), 0);
}

TEST_F(LibraryBuilderTest, AuthorFingerprintCollisionRetainsPriorIndexAndCanRetry) {
  bookMetadata["/a.epub"].author = "Christopher Tolkien";
  bookMetadata["/b.epub"].author = "Christopher Priest";
  initial();
  const auto good = fake::files[INDEX]->bytes;
  auto& bytes = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  ClixRecord first{}, second{};
  std::memcpy(&first, bytes.data() + header.recordStart, sizeof(first));
  std::memcpy(&second, bytes.data() + header.recordStart + sizeof(first), sizeof(second));
  std::memcpy(second.authorKey, first.authorKey, sizeof(second.authorKey));
  second.authorKeyLen = first.authorKeyLen;
  std::memcpy(bytes.data() + header.recordStart + sizeof(first), &second, sizeof(second));
  const auto collided = bytes;
  fake::add("/c.epub");
  bookMetadata["/c.epub"].author = "Jane Austen";

  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, collided);
  EXPECT_FALSE(Storage.exists("/.crosspoint/library.new"));
  fake::files[INDEX]->bytes = good;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(stats.indexReplaced);
}

TEST_F(LibraryBuilderTest, FullAuthorFingerprintSurvivesDisplayTruncationAndReuse) {
  const std::string common(128, 'x');
  bookMetadata["/a.epub"].author = common + " Smith";
  bookMetadata["/b.epub"].author = common + " Tolkien";
  initial();
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ClixRecord first{}, second{};
  ASSERT_TRUE(index.readRecord(0, first));
  ASSERT_TRUE(index.readRecord(1, second));
  const std::string firstKey(first.authorKey, first.authorKeyLen);
  const std::string secondKey(second.authorKey, second.authorKeyLen);
  EXPECT_NE(firstKey, secondKey);
  std::string author;
  ASSERT_TRUE(index.readAuthor(first, author));
  EXPECT_EQ(author, common);
  ASSERT_TRUE(index.readAuthor(second, author));
  EXPECT_EQ(author, common);
  index.close();

  fake::add("/c.epub");
  bookMetadata["/c.epub"].author = "Jane Austen";
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  ASSERT_TRUE(index.open(INDEX));
  ASSERT_TRUE(index.readRecord(0, first));
  ASSERT_TRUE(index.readRecord(1, second));
  EXPECT_EQ(std::string(first.authorKey, first.authorKeyLen), firstKey);
  EXPECT_EQ(std::string(second.authorKey, second.authorKeyLen), secondKey);
}

TEST_F(LibraryBuilderTest, AuthorIdentityWithManySharedPrefixesKeepsLinearStagingReads) {
  const auto measure = [this](const unsigned count) {
    fake::reset();
    bookMetadata.clear();
    for (unsigned i = 0; i < count; ++i) {
      const std::string path = "/" + numbered("book", i) + ".epub";
      fake::add(path);
      bookMetadata[path] = {numbered("Title", i), "Christopher " + numbered("Surname", i / 2), "", ""};
    }
    fake::resetIoCounters();
    if (!buildLibraryIndex("/", stats, true)) {
      ADD_FAILURE() << "failed at " << count;
      return 0u;
    }
    EXPECT_FALSE(stats.ranksDegraded);
    EXPECT_LE(*std::max_element(fake::allocations.begin(), fake::allocations.end()),
              std::max<size_t>(count * 14u, 8192u));
    const unsigned work = fake::reads + fake::seeks;
    LibraryIndexFile index;
    EXPECT_TRUE(index.open(INDEX));
    ClixRecord record{};
    std::string author;
    for (uint16_t row = 0; row < count; ++row) {
      if (!index.readRecord(index.ordinalForRow(SortOrder::AuthorAsc, row), record) ||
          !index.readAuthor(record, author)) {
        ADD_FAILURE() << "unreadable row " << row;
        break;
      }
      EXPECT_EQ(author, "Christopher " + numbered("Surname", row / 2));
    }
    return work;
  };
  const unsigned small = measure(256);
  const unsigned large = measure(512);
  EXPECT_LT(large, small * 3u);
}

TEST_F(LibraryBuilderTest, DirtyMarkerInvalidatesWarmSessionAndClearsOnlyAfterSuccessfulBuild) {
  initial();
  librarySession.reconciled(true, librarySession.refreshToken());
  ASSERT_TRUE(markLibraryIndexDirty());
  EXPECT_TRUE(isLibraryIndexDirty());
  EXPECT_TRUE(librarySession.needsRefresh(true, true));
  fake::failAlloc = 0;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(isLibraryIndexDirty());
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_FALSE(isLibraryIndexDirty());
  EXPECT_EQ(stats.metadataReused, 2);
}

TEST_F(LibraryBuilderTest, DirtyMarkerWriteFailureStillInvalidatesInMemory) {
  initial();
  librarySession.reconciled(true, librarySession.refreshToken());
  fake::failOpenPath = "/.crosspoint/library.dirty";
  ASSERT_TRUE(markLibraryIndexDirty());
  EXPECT_TRUE(isLibraryIndexDirty());
  EXPECT_TRUE(librarySession.needsRefresh(true, true));
  fake::failOpenPath.clear();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_FALSE(isLibraryIndexDirty());
}

TEST_F(LibraryBuilderTest, MutationDuringBuildKeepsDirtyMarkerAndRefreshTokenPending) {
  initial();
  const uint32_t token = librarySession.refreshToken();
  librarySession.reconciled(true, token);
  fake::onNextEntry = [] { markLibraryIndexDirty(); };
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  librarySession.reconciled(true, token);
  EXPECT_TRUE(isLibraryIndexDirty());
  EXPECT_TRUE(librarySession.needsRefresh(true, true));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_FALSE(isLibraryIndexDirty());
}

TEST_F(LibraryBuilderTest, ArrivalOrderFollowsModificationTimeOverDiscoveryOrder) {
  // a and b exist with the default time; c lands with an older timestamp and d
  // with the newest, so file times, not walk or firstSeen order, decide.
  fake::add("/c.epub", "book c", /*time=*/0);
  fake::add("/d.epub", "book d", /*time=*/9);
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/c.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 2), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 3), "/d.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentDesc, 0), "/d.epub");
}

TEST_F(LibraryBuilderTest, PreviousArrivalSortRebuildsOnceAndKeepsFirstSeenValues) {
  fake::files["/a.epub"]->time = 9;
  initial();
  auto& bytes = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  header.foldVersion = 4;
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t oldOrder[] = {0, 1};
  std::memcpy(bytes.data() + arrivalOrderOffset(header, 0), oldOrder, sizeof(oldOrder));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(stats.indexReplaced);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().foldVersion, CLIX_FOLD_VERSION);
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 1), "/a.epub");
  ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  EXPECT_EQ(record.firstSeen, 0);
  ASSERT_TRUE(index.readRecord(1, record));
  EXPECT_EQ(record.firstSeen, 1);
  index.close();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(stats.metadataReused, 2);
}

TEST_F(LibraryBuilderTest, EveryDirectoryReadFailureRetainsCommittedIndexAndCanRetry) {
  // Include a nested folder so errors in child scans cannot publish a partial root.
  fake::add("/nested/c.epub");
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::add("/new.epub");
  bool reachedCompleteScan = false;
  unsigned injected = 0;
  for (int at = 0; at < 30; ++at) {
    fake::failNext = at;
    fake::failureTriggered = false;
    const bool ok = buildLibraryIndex("/", stats, true);
    if (!fake::failureTriggered) {
      EXPECT_TRUE(ok);
      reachedCompleteScan = true;
      break;
    }
    ++injected;
    EXPECT_FALSE(ok) << at;
    EXPECT_FALSE(stats.indexReplaced) << at;
    EXPECT_EQ(fake::files[INDEX]->bytes, old) << at;
    EXPECT_FALSE(Storage.exists("/.crosspoint/library.stage"));
    EXPECT_FALSE(Storage.exists("/.crosspoint/library.stage.f"));
  }
  fake::failNext = -1;
  EXPECT_TRUE(reachedCompleteScan);
  EXPECT_GT(injected, 5U);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().bookCount, 4);
}

TEST_F(LibraryBuilderTest, LeadingWordsDistinguishSeriesAndControlTitleOrder) {
  bookMetadata["/a.epub"] = {"The Apple", "Author", "The Earthsea", "1"};
  bookMetadata["/b.epub"] = {"Banana", "Author", "Earthsea", "2"};
  initial();
  EXPECT_EQ(stats.series, 2);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, 0), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, 1), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 0), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 1), "/a.epub");
}

TEST_F(LibraryBuilderTest, PreviousArticleKeysRebuildOnceWithoutLosingArrivalHistory) {
  bookMetadata["/a.epub"] = {"The Apple", "Author", "", ""};
  bookMetadata["/b.epub"] = {"Banana", "Author", "", ""};
  initial();
  auto& bytes = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  header.foldVersion = 5;
  std::memcpy(bytes.data(), &header, sizeof(header));
  ClixRecord apple{};
  std::memcpy(&apple, bytes.data() + header.recordStart + sizeof(ClixRecord), sizeof(apple));
  const auto firstSeen = apple.firstSeen;
  std::memset(apple.fold, 0, sizeof(apple.fold));
  std::memcpy(apple.fold, "apple", 5);
  apple.foldLen = 5;
  ClixRecord banana{};
  std::memcpy(&banana, bytes.data() + header.recordStart, sizeof(banana));
  // Revision 5 stored Apple before Banana and used those ordinals in each rank.
  std::memcpy(bytes.data() + header.recordStart, &apple, sizeof(apple));
  std::memcpy(bytes.data() + header.recordStart + sizeof(ClixRecord), &banana, sizeof(banana));
  const uint16_t oldOrder[] = {0, 1};
  for (const auto offset :
       {authorOrderOffset(header, 0), arrivalOrderOffset(header, 0), seriesOrderOffset(header, 0)}) {
    std::memcpy(bytes.data() + offset, oldOrder, sizeof(oldOrder));
  }
  {
    LibraryIndexFile previous;
    ASSERT_TRUE(previous.openForReconciliation(INDEX));
    EXPECT_EQ(pathAt(previous, SortOrder::TitleAsc, 0), "/a.epub");
    EXPECT_EQ(pathAt(previous, SortOrder::RecentAsc, 0), "/a.epub");
  }
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(stats.indexReplaced);
  EXPECT_EQ(stats.metadataReused, 0);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().foldVersion, CLIX_FOLD_VERSION);
  EXPECT_EQ(index.header().nextFirstSeen, header.nextFirstSeen);
  EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, 0), "/b.epub");
  ASSERT_TRUE(index.readRecord(1, apple));
  EXPECT_EQ(std::string(apple.fold, apple.foldLen), "the apple");
  EXPECT_EQ(apple.firstSeen, firstSeen);
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/b.epub");
  index.close();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(stats.metadataReused, 2);
}

namespace {
void measureAuthorEmit(const bool shared) {
  constexpr unsigned COUNT = 512;
  fake::reset();
  bookMetadata.clear();
  for (unsigned i = 0; i < COUNT; ++i) {
    const std::string path = "/" + numbered("book", i) + ".epub";
    fake::add(path);
    const std::string author =
        shared ? (i % 3 == 0 ? "Gabriel Garcia Marquez" : "Gabriel García Márquez") : "Writer " + numbered("Family", i);
    bookMetadata[path] = {numbered("Title", i), author, "", ""};
  }
  fake::resetIoCounters();
  BuildStats stats;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  const unsigned reads = fake::reads;
  const unsigned seeks = fake::seeks;
  const size_t bytes = fake::bytesRead;
  const auto& image = fake::files[INDEX]->bytes;
  uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : image) hash = (hash ^ byte) * 1099511628211ULL;
  std::printf("AUTHOR_EMIT shared=%d books=%u reads=%u seeks=%u bytes=%zu image_bytes=%zu hash=%016llx delays=%u\n",
              shared, COUNT, reads, seeks, bytes, image.size(), static_cast<unsigned long long>(hash), fake::delays);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ASSERT_EQ(index.bookCount(), COUNT);
  EXPECT_FALSE(stats.ranksDegraded);
  for (uint16_t i = 0; i < COUNT; ++i) {
    ClixRecord record{};
    ASSERT_TRUE(index.readRecord(i, record));
    std::string title, author, source, path;
    ASSERT_TRUE(index.readTitleAndAuthor(record, title, author));
    ASSERT_TRUE(index.readSourceAuthor(record, source));
    ASSERT_TRUE(index.readPath(record, path));
    EXPECT_EQ(title, numbered("Title", i));
    EXPECT_EQ(path, "/" + numbered("book", i) + ".epub");
    EXPECT_EQ(author, shared ? "Gabriel García Márquez" : "Writer " + numbered("Family", i));
    EXPECT_EQ(source, bookMetadata[path].author);
  }
}
}  // namespace

TEST_F(LibraryBuilderTest, EmitUniqueAuthorsRetainsEveryRecord) { measureAuthorEmit(false); }
TEST_F(LibraryBuilderTest, EmitSharedAuthorRetainsCanonicalAndSourceSpellings) { measureAuthorEmit(true); }

TEST_F(LibraryBuilderTest, ShortCanonicalAuthorReadsRetainIndexAndAllowRetry) {
  fake::add("/c.epub");
  bookMetadata["/a.epub"] = {"Alpha", "Gabriel Garcia Marquez", "", ""};
  bookMetadata["/b.epub"] = {"Beta", "Gabriel García Márquez", "", ""};
  bookMetadata["/c.epub"] = {"Gamma", "Gabriel García Márquez", "", ""};
  initial();
  const auto snapshot = fake::files;
  const auto committed = fake::files[INDEX]->bytes;
  // The surname pass reads the chosen spelling once; two non-canonical books need
  // it again in each of the two output passes.
  for (int fault = 0; fault < 5; ++fault) {
    fake::reset();
    fake::files = snapshot;
    fake::files["/c.epub"]->time = 2;
    fake::shortReadPath = "/.crosspoint/library.stage";
    fake::shortReadSize = 258;  // the bounded 128-byte author and author sort, each with its length
    fake::shortReadMatch = fault;
    EXPECT_FALSE(buildLibraryIndex("/", stats, true)) << fault;
    ASSERT_TRUE(fake::failureTriggered) << fault;
    EXPECT_FALSE(stats.indexReplaced);
    EXPECT_EQ(fake::files[INDEX]->bytes, committed) << fault;
    fake::shortReadPath.clear();
    ASSERT_TRUE(buildLibraryIndex("/", stats, true)) << fault;
    EXPECT_TRUE(stats.indexReplaced);
    EXPECT_EQ(stats.books, 3);
    // Restore the original index for the next independent fault.
    fake::files = snapshot;
  }
}

TEST_F(LibraryBuilderTest, SharedSurnameKeysPreserveInterleavedCanonicalSources) {
  constexpr unsigned GROUPS = 32;
  constexpr unsigned PER_GROUP = 8;
  fake::reset();
  bookMetadata.clear();
  for (unsigned i = 0; i < GROUPS * PER_GROUP; ++i) {
    const unsigned group = i % GROUPS;
    const unsigned occurrence = i / GROUPS;
    const bool chosenSpelling = group % 2 == 0 ? occurrence < 5 : occurrence >= 3;
    const std::string path = "/" + numbered("book", i) + ".epub";
    fake::add(path);
    bookMetadata[path] = {numbered("Title", i),
                          std::string(chosenSpelling ? "Émile " : "Emile ") + numbered("Family", group), "", ""};
  }
  fake::resetIoCounters();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_FALSE(stats.ranksDegraded);
  const auto reads = fake::reads;
  const auto seeks = fake::seeks;
  const auto bytes = fake::bytesRead;
  const auto& image = fake::files[INDEX]->bytes;
  uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : image) hash = (hash ^ byte) * 1099511628211ULL;
  std::printf("SURNAME_REUSE books=%u groups=%u reads=%u seeks=%u bytes=%zu image_bytes=%zu hash=%016llx delays=%u\n",
              GROUPS * PER_GROUP, GROUPS, reads, seeks, bytes, image.size(), static_cast<unsigned long long>(hash),
              fake::delays);
  EXPECT_EQ(reads, 2530u);
  EXPECT_EQ(seeks, 2529u);
  EXPECT_EQ(hash, 0xe14a1d1ddfb4e9fbULL);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ASSERT_EQ(index.bookCount(), GROUPS * PER_GROUP);
  for (uint16_t row = 0; row < GROUPS * PER_GROUP; ++row) {
    const unsigned group = row / PER_GROUP;
    const unsigned occurrence = row % PER_GROUP;
    const uint16_t expectedOrdinal = group + occurrence * GROUPS;
    EXPECT_EQ(index.ordinalForRow(SortOrder::AuthorAsc, row), expectedOrdinal);
    EXPECT_EQ(index.ordinalForRow(SortOrder::AuthorDesc, GROUPS * PER_GROUP - 1 - row), expectedOrdinal);
    ClixRecord record{};
    ASSERT_TRUE(index.readRecord(expectedOrdinal, record));
    std::string title, author, source, path;
    ASSERT_TRUE(index.readTitleAndAuthor(record, title, author));
    ASSERT_TRUE(index.readSourceAuthor(record, source));
    ASSERT_TRUE(index.readPath(record, path));
    EXPECT_EQ(title, numbered("Title", expectedOrdinal));
    EXPECT_EQ(author, "Émile " + numbered("Family", group));
    EXPECT_EQ(path, "/" + numbered("book", expectedOrdinal) + ".epub");
    EXPECT_EQ(source, bookMetadata[path].author);
  }
}

TEST_F(LibraryBuilderTest, PriorScanPreservesIdentityAndYieldBudgetAcrossTitleOrder) {
  for (const unsigned count : {1u, 33u, 512u, 4096u}) {
    fake::reset();
    bookMetadata.clear();
    for (unsigned i = 0; i < count; ++i) {
      const std::string path = "/" + numbered("book", i) + ".epub";
      fake::add(path);
      // Reverse title order so record ordinal cannot substitute for path identity.
      bookMetadata[path] = {numbered("Title ", count - i), "Example Writer", "", ""};
    }
    initial();
    const auto committed = fake::files[INDEX]->bytes;
    fake::resetIoCounters();
    fake::parses = 0;
    ASSERT_TRUE(buildLibraryIndex("/", stats, true)) << count;
    EXPECT_EQ(stats.metadataReused, count);
    EXPECT_EQ(stats.unchanged, count);
    EXPECT_EQ(fake::parses, 0u);
    EXPECT_FALSE(stats.indexReplaced);
    EXPECT_EQ(fake::files[INDEX]->bytes, committed);
    EXPECT_EQ(fake::reads, 5u * count + 1u);
    EXPECT_EQ(fake::seeks, 4u * count + 1u);
    EXPECT_EQ(fake::bytesRead, 332u * count + 44u);
    EXPECT_EQ(fake::delays, 3u * count / 32u);
    std::printf("PRIOR_SCAN books=%u reads=%u seeks=%u bytes=%zu delays=%u\n", count, fake::reads, fake::seeks,
                fake::bytesRead, fake::delays);
  }
}

TEST_F(LibraryBuilderTest, CalibreSortKeysOrderTitlesAndAuthors) {
  fake::add("/c.epub");
  fake::add("/d.epub");
  bookMetadata["/a.epub"] = {"The Hobbit",
                             "J. R. R. Tolkien",
                             "",
                             "",
                             true,
                             "Hobbit, The",
                             "Tolkien, J. R. R.",
                             "0f3c2b1a-0000-4000-8000-00000000000a"};
  bookMetadata["/b.epub"] = {"Gormenghast", "Mervyn Peake", "", ""};
  bookMetadata["/c.epub"] = {"A Wizard of Earthsea",  "Ursula K. Le Guin", "", "", true,
                             "Wizard of Earthsea, A", "Le Guin, Ursula K."};
  bookMetadata["/d.epub"] = {"Pandora's Star", "Peter F. Hamilton", "", "", true, "", "Hamilton, Peter F."};
  initial();

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  // Leading articles move behind the title exactly as the Calibre library sorts them.
  EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, 0), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, 1), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, 2), "/d.epub");
  EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, 3), "/c.epub");
  // "Le Guin" files under L, where the last-word guess would put her under G.
  EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, 0), "/d.epub");
  EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, 1), "/c.epub");
  EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, 2), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, 3), "/a.epub");

  ClixRecord record{};
  std::string title, author, sort, uuid;
  ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::TitleAsc, 1), record));
  ASSERT_TRUE(index.readTitleAuthorAndSort(record, title, author, sort));
  EXPECT_EQ(title, "The Hobbit");
  EXPECT_EQ(author, "J. R. R. Tolkien");
  EXPECT_EQ(sort, "Tolkien, J. R. R.");
  ASSERT_TRUE(index.readUuid(record, uuid));
  EXPECT_EQ(uuid, std::string("\x0f\x3c\x2b\x1a\x00\x00\x40\x00\x80\x00\x00\x00\x00\x00\x00\x0a", 16));
  EXPECT_EQ(foldedGroupInitial(std::string_view(record.fold, record.foldLen)), static_cast<uint32_t>('h'));

  ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::TitleAsc, 0), record));
  ASSERT_TRUE(index.readAuthorSort(record, sort));
  EXPECT_TRUE(sort.empty());
  EXPECT_FALSE(index.readUuid(record, uuid));
  index.close();

  // An unchanged rebuild carries every Calibre field across without parsing.
  const auto committed = fake::files[INDEX]->bytes;
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 0u);
  EXPECT_EQ(fake::files[INDEX]->bytes, committed);
}

TEST_F(LibraryBuilderTest, UuidMatchesARenamedBookWhoseSizeChanged) {
  bookMetadata["/a.epub"] = {"Old Title", "Writer", "", "", true, "", "", "1731e1ca-38a6-47da-9c5b-6f324ab6a3bf"};
  bookMetadata["/b.epub"] = {"Other", "Writer", "", "", true, "", "", "ee1cc8b6-8ba2-44c7-bd80-0d01b0896268"};
  initial();

  // Calibre rewrites the book when its title changes, so the renamed file differs in size.
  ASSERT_TRUE(Storage.remove("/a.epub"));
  fake::add("/new title.epub", "a rewritten book", 5);
  bookMetadata["/new title.epub"] = {"New Title", "Writer", "", "",
                                     true,        "",       "", "1731e1ca-38a6-47da-9c5b-6f324ab6a3bf"};
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.renamed, 1);
  EXPECT_EQ(stats.added, 0);
  EXPECT_EQ(stats.removed, 0);

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ClixRecord renamed{};
  ClixRecord kept{};
  ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::TitleAsc, 0), renamed));
  ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::TitleAsc, 1), kept));
  EXPECT_LT(renamed.firstSeen, kept.firstSeen);
}

TEST_F(LibraryBuilderTest, PreviousFormatReparsesMetadataButKeepsArrivalOrder) {
  bookMetadata["/a.epub"] = {"The Hobbit", "J. R. R. Tolkien", "", "", true, "Hobbit, The"};
  initial();
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ClixRecord before{};
  ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::AddedAsc, 0), before));
  index.close();

  auto& bytes = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  header.formatVersion = CLIX_FORMAT_VERSION - 1;
  std::memcpy(bytes.data(), &header, sizeof(header));
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 2u);
  EXPECT_EQ(stats.metadataReused, 0);
  EXPECT_TRUE(stats.indexReplaced);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().formatVersion, CLIX_FORMAT_VERSION);
  EXPECT_EQ(pathAt(index, SortOrder::AddedAsc, 0), "/a.epub");
  ClixRecord after{};
  ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::AddedAsc, 0), after));
  EXPECT_EQ(after.firstSeen, before.firstSeen);
}

TEST_F(LibraryBuilderTest, UuidRenameWinsOverAnEarlierBookOfTheSameSize) {
  bookMetadata["/a.epub"] = {"Old Title", "Writer", "", "", true, "", "", "1731e1ca-38a6-47da-9c5b-6f324ab6a3bf"};
  initial();
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ClixRecord original{};
  ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::TitleAsc, 0), original));
  index.close();

  // The new book is walked first and has the removed book's exact size.
  ASSERT_TRUE(Storage.remove("/a.epub"));
  fake::add("/0 new.epub", "book", 5);
  fake::add("/z renamed.epub", "a rewritten book", 5);
  bookMetadata["/z renamed.epub"] = {"New Title", "Writer", "", "",
                                     true,        "",       "", "1731e1ca-38a6-47da-9c5b-6f324ab6a3bf"};
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.renamed, 1);
  EXPECT_EQ(stats.added, 1);
  ASSERT_TRUE(index.open(INDEX));
  ASSERT_EQ(pathAt(index, SortOrder::TitleAsc, 0), "/z renamed.epub");
  ClixRecord renamed{};
  ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::TitleAsc, 0), renamed));
  EXPECT_EQ(renamed.firstSeen, original.firstSeen);
}

TEST_F(LibraryBuilderTest, SameSizeBookWithADifferentUuidIsNew) {
  bookMetadata["/a.epub"] = {"Old", "Writer", "", "", true, "", "", "1731e1ca-38a6-47da-9c5b-6f324ab6a3bf"};
  initial();
  ASSERT_TRUE(Storage.remove("/a.epub"));
  fake::add("/c.epub", "book", 5);
  bookMetadata["/c.epub"] = {"Other", "Writer", "", "", true, "", "", "ee1cc8b6-8ba2-44c7-bd80-0d01b0896268"};
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.renamed, 0);
  EXPECT_EQ(stats.added, 1);
  EXPECT_EQ(stats.removed, 1);
}

namespace {
std::vector<std::pair<std::string, std::string>> renameCalls;
bool renameResult = true;
bool recordRename(const std::string& oldPath, const std::string& newPath) {
  renameCalls.emplace_back(oldPath, newPath);
  return renameResult;
}
std::string uuidFor(const char letter) {
  std::string uuid = "00000000-0000-4000-8000-000000000000";
  uuid[0] = letter;
  return uuid;
}
constexpr char RENAME_JOURNAL[] = "/.crosspoint/library.stage.r";
}  // namespace

class LibraryRenameRelinkTest : public LibraryBuilderTest {
 protected:
  void SetUp() override {
    LibraryBuilderTest::SetUp();
    renameCalls.clear();
    renameResult = true;
    for (const char letter : {'a', 'b', 'c', 'd', 'e'}) {
      const std::string path = std::string("/") + letter + ".epub";
      fake::add(path, std::string("book ") + letter);
      bookMetadata[path] = {std::string("Title ") + letter, "Writer", "", "", true, "", "", uuidFor(letter)};
    }
  }
  static void rename(const char from, const std::string& to, const char uuidLetter) {
    ASSERT_TRUE(Storage.remove((std::string("/") + from + ".epub").c_str()));
    fake::add(to, std::string("a rewritten book ") + from, 5);
    bookMetadata[to] = {std::string("Retitled ") + from, "Writer", "", "", true, "", "", uuidFor(uuidLetter)};
  }
};

TEST_F(LibraryRenameRelinkTest, UuidRenamesAreReportedWithOldAndNewPathsAfterInstall) {
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, recordRename));
  EXPECT_TRUE(renameCalls.empty());
  rename('a', "/moved/zulu.epub", 'a');
  rename('c', "/moved/alpha.epub", 'c');
  ASSERT_TRUE(Storage.remove("/d.epub"));
  fake::add("/f.epub", "new book");
  bookMetadata["/f.epub"] = {"Title f", "Writer", "", "", true, "", "", uuidFor('f')};
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, recordRename));
  EXPECT_EQ(stats.uuidRenamed, 2);
  EXPECT_EQ(stats.relinked, 2);
  EXPECT_EQ(stats.removed, 1);
  EXPECT_EQ(stats.added, 1);
  std::sort(renameCalls.begin(), renameCalls.end());
  ASSERT_EQ(renameCalls.size(), 2u);
  EXPECT_EQ(renameCalls[0], (std::pair<std::string, std::string>{"/a.epub", "/moved/zulu.epub"}));
  EXPECT_EQ(renameCalls[1], (std::pair<std::string, std::string>{"/c.epub", "/moved/alpha.epub"}));
  EXPECT_FALSE(fake::files.contains(RENAME_JOURNAL));
}

TEST_F(LibraryRenameRelinkTest, RenamesMatchedBySizeAloneAreNeverReported) {
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, recordRename));
  for (const char letter : {'a', 'b', 'c', 'd', 'e'}) bookMetadata[std::string("/") + letter + ".epub"].uuid.clear();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, recordRename));
  ASSERT_TRUE(Storage.rename("/a.epub", "/renamed.epub"));
  bookMetadata["/renamed.epub"] = bookMetadata["/a.epub"];
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, recordRename));
  EXPECT_EQ(stats.renamed, 1);
  EXPECT_EQ(stats.uuidRenamed, 0);
  EXPECT_TRUE(renameCalls.empty());
}

TEST_F(LibraryRenameRelinkTest, UnhandledPairsAreNotCountedAsRelinked) {
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, recordRename));
  rename('b', "/b renamed.epub", 'b');
  renameResult = false;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, recordRename));
  EXPECT_EQ(renameCalls.size(), 1u);
  EXPECT_EQ(stats.uuidRenamed, 1);
  EXPECT_EQ(stats.relinked, 0);
}

TEST_F(LibraryRenameRelinkTest, WithoutAHandlerNothingIsJournaled) {
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  rename('b', "/b renamed.epub", 'b');
  fake::writesByPath.clear();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.renamed, 1);
  EXPECT_EQ(stats.uuidRenamed, 0);
  EXPECT_EQ(fake::writesByPath.count(RENAME_JOURNAL), 0u);
}

TEST_F(LibraryRenameRelinkTest, FailedInstallReportsNothing) {
  ASSERT_TRUE(buildLibraryIndex("/", stats, true, recordRename));
  rename('b', "/b renamed.epub", 'b');
  fake::failRename = 0;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true, recordRename));
  EXPECT_TRUE(renameCalls.empty());
  EXPECT_FALSE(fake::files.contains(RENAME_JOURNAL));
}
