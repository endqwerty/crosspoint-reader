// SD-operation fixture for the read-only unchanged-library probe.
//
// A 750-book Author/Title/book.epub card is built once; each scenario then
// changes it and rebuilds. Counts are SD operations from the storage fake, not
// timings. Index digests were recorded from the staging-only builder at
// b020e905, so a changed card must still produce byte-identical output.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "Epub.h"
#include "LibraryBuilder.h"
#include "LibraryIndexFile.h"
#include "LibrarySession.h"

using namespace library;

namespace {

constexpr char INDEX[] = "/.crosspoint/library.idx";
constexpr char STAGE[] = "/.crosspoint/library.stage";
constexpr char FOLDER_STAGE[] = "/.crosspoint/library.stage.f";
constexpr unsigned AUTHORS = 75;
constexpr unsigned TITLES_PER_AUTHOR = 10;
constexpr unsigned BOOKS = AUTHORS * TITLES_PER_AUTHOR;

std::string authorDir(const unsigned author) {
  char text[32];
  std::snprintf(text, sizeof(text), "/Author %02u", author);
  return text;
}

std::string bookPath(const unsigned author, const unsigned title) {
  char text[64];
  std::snprintf(text, sizeof(text), "%s/Title %03u/Title %03u.epub", authorDir(author).c_str(), title, title);
  return text;
}

std::string uuidFor(const unsigned book) {
  char text[40];
  std::snprintf(text, sizeof(text), "%08x-0000-4000-8000-%012x", 0x51ed0000u + book, book);
  return text;
}

uint64_t digest(const std::vector<uint8_t>& bytes) {
  uint64_t hash = 14695981039346656037ULL;
  for (const uint8_t b : bytes) {
    hash ^= b;
    hash *= 1099511628211ULL;
  }
  return hash;
}

struct SdOps {
  unsigned reads = 0;
  size_t bytesRead = 0;
  unsigned seeks = 0;
  unsigned writeOpens = 0;
  unsigned writes = 0;
  size_t bytesWritten = 0;
  unsigned stageWriteOpens = 0;
  unsigned stageWrites = 0;
  unsigned directoryOpens = 0;
  unsigned entries = 0;
  unsigned removes = 0;
  unsigned parses = 0;
};

SdOps captureOps() {
  SdOps ops;
  ops.reads = fake::reads;
  ops.bytesRead = fake::bytesRead;
  ops.seeks = fake::seeks;
  for (const auto& [path, count] : fake::writeOpensByPath) {
    ops.writeOpens += count;
    if (path == STAGE || path == FOLDER_STAGE) ops.stageWriteOpens += count;
  }
  for (const auto& [path, count] : fake::writesByPath) {
    ops.writes += count;
    if (path == STAGE || path == FOLDER_STAGE) ops.stageWrites += count;
  }
  for (const auto& [path, bytes] : fake::bytesWrittenByPath) ops.bytesWritten += bytes;
  for (const auto& [path, count] : fake::directoryEntriesByPath) ops.entries += count;
  ops.directoryOpens = fake::directoryOpens;
  ops.removes = fake::removes;
  ops.parses = fake::parses;
  return ops;
}

void clearOps() {
  fake::resetIoCounters();
  fake::writeOpensByPath.clear();
  fake::writesByPath.clear();
  fake::bytesWrittenByPath.clear();
  fake::directoryEntriesByPath.clear();
  fake::directoryOpens = 0;
  fake::removes = 0;
  fake::parses = 0;
}

void printOps(const char* scenario, const SdOps& ops, const uint64_t indexDigest) {
  std::printf(
      "[probe-ops] %-18s reads=%u bytesRead=%zu seeks=%u writeOpens=%u writes=%u bytesWritten=%zu "
      "stageWriteOpens=%u stageWrites=%u dirOpens=%u entries=%u removes=%u parses=%u index=%016llx\n",
      scenario, ops.reads, ops.bytesRead, ops.seeks, ops.writeOpens, ops.writes, ops.bytesWritten, ops.stageWriteOpens,
      ops.stageWrites, ops.directoryOpens, ops.entries, ops.removes, ops.parses,
      static_cast<unsigned long long>(indexDigest));
}

class LibraryProbeTest : public ::testing::Test {
 protected:
  BuildStats stats;
  std::map<std::string, std::shared_ptr<fake::Node>> committedCard;
  std::vector<uint8_t> committedIndex;

  void SetUp() override {
    fake::reset();
    bookMetadata.clear();
    unsigned book = 0;
    for (unsigned author = 0; author < AUTHORS; ++author) {
      for (unsigned title = 0; title < TITLES_PER_AUTHOR; ++title, ++book) {
        const std::string path = bookPath(author, title);
        // Unique sizes keep size-matched renames unambiguous, as on a real card.
        fake::add(path, std::string(200 + book, 'x'), 1000 + book);
        FakeMetadata& metadata = bookMetadata[path];
        metadata.title = "Title " + std::to_string(title) + " of " + std::to_string(author);
        metadata.author = "Writer" + std::to_string(author) + " Surname" + std::to_string(author);
        if (title % 3 == 0) {
          metadata.series = "Series " + std::to_string(author);
          metadata.seriesIndexText = std::to_string(title / 3 + 1);
        }
        if (book % 2 == 0) metadata.uuid = uuidFor(book);
      }
    }
    // Non-book files are filtered by both passes and never staged.
    fake::add("/Author 00/Title 000/cover.jpg", "jpeg");
    fake::add("/Author 00/Title 000/metadata.opf", "opf");
    ASSERT_TRUE(buildLibraryIndex("/", stats, true));
    ASSERT_EQ(stats.books, BOOKS);
    committedCard = deepCopy(fake::files);
    committedIndex = fake::files[INDEX]->bytes;
  }

  static std::map<std::string, std::shared_ptr<fake::Node>> deepCopy(
      const std::map<std::string, std::shared_ptr<fake::Node>>& files) {
    std::map<std::string, std::shared_ptr<fake::Node>> copy;
    for (const auto& [path, node] : files) copy[path] = std::make_shared<fake::Node>(*node);
    return copy;
  }

  void restoreCard() {
    fake::reset();
    fake::files = deepCopy(committedCard);
  }

  // Rebuild after `change` and report what it cost.
  template <typename Change>
  SdOps rebuild(const char* scenario, Change change, uint64_t& indexDigest) {
    restoreCard();
    change();
    clearOps();
    EXPECT_TRUE(buildLibraryIndex("/", stats, true)) << scenario;
    const SdOps ops = captureOps();
    indexDigest = digest(fake::files[INDEX]->bytes);
    printOps(scenario, ops, indexDigest);
    EXPECT_FALSE(Storage.exists(STAGE)) << scenario;
    EXPECT_FALSE(Storage.exists(FOLDER_STAGE)) << scenario;
    return ops;
  }
};

}  // namespace

TEST_F(LibraryProbeTest, UnchangedRefreshOpensNoStagingFileAndWritesNothing) {
  uint64_t indexDigest = 0;
  const SdOps ops = rebuild("unchanged", [] {}, indexDigest);

  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(stats.metadataReused, BOOKS);
  EXPECT_EQ(stats.unchanged, BOOKS);
  EXPECT_EQ(stats.books, BOOKS);
  EXPECT_EQ(stats.series, AUTHORS);
  EXPECT_EQ(ops.parses, 0u);
  EXPECT_EQ(ops.stageWriteOpens, 0u);
  EXPECT_EQ(ops.writeOpens, 0u);
  EXPECT_EQ(ops.writes, 0u);
  EXPECT_EQ(ops.bytesWritten, 0u);
  EXPECT_EQ(fake::files[INDEX]->bytes, committedIndex);
  // One enumeration of the card: every book, sidecar and folder entry once.
  EXPECT_EQ(ops.entries, BOOKS + 2u + AUTHORS + BOOKS + 1u);
}

TEST_F(LibraryProbeTest, ChangedCardsMatchTheStagingBuilderByteForByte) {
  struct Scenario {
    const char* name;
    void (*change)();
    uint64_t expectedIndex;
    unsigned expectedParses;
  };
  // Digests recorded from the staging-only builder at b020e905.
  const Scenario scenarios[] = {
      {"first-modified", [] { fake::files[bookPath(0, 0)]->time++; }, 0xde7af24d01e6e907ULL, 1},
      {"last-modified", [] { fake::files[bookPath(AUTHORS - 1, TITLES_PER_AUTHOR - 1)]->time++; },
       0x4d2c3901bdbb9b99ULL, 1},
      {"middle-grown", [] { fake::files[bookPath(37, 4)]->bytes.push_back('y'); }, 0x7d496c11cd3e1adfULL, 1},
      {"unknown-mtime", [] { fake::files[bookPath(20, 1)]->time = 0; }, 0xe8144ae2c836b3cfULL, 1},
      {"added-in-folder", [] { fake::add(authorDir(40) + "/Title 010/Title 010.epub", std::string(5000, 'n'), 9000); },
       0x9cc98e34c6eeb281ULL, 1},
      {"added-new-folder", [] { fake::add("/Zed Author/New Book/New Book.epub", std::string(5001, 'n'), 9001); },
       0x2942ef411eda9e6cULL, 1},
      {"removed-first", [] { Storage.remove(bookPath(0, 0).c_str()); }, 0x8bf17332344c7621ULL, 0},
      {"removed-last", [] { Storage.remove(bookPath(AUTHORS - 1, TITLES_PER_AUTHOR - 1).c_str()); },
       0x541a9c3a053c4247ULL, 0},
      {"renamed-file",
       [] { Storage.rename(bookPath(12, 5).c_str(), (authorDir(12) + "/Title 005/Renamed.epub").c_str()); },
       0x26aa169f7bf99ce6ULL, 1},
      {"renamed-folder", [] { Storage.rename(authorDir(30).c_str(), "/Author 30 renamed"); }, 0x3ce574eedbaba2e7ULL,
       TITLES_PER_AUTHOR},
  };
  for (const Scenario& scenario : scenarios) {
    SCOPED_TRACE(scenario.name);
    uint64_t indexDigest = 0;
    const SdOps ops = rebuild(scenario.name, scenario.change, indexDigest);
    EXPECT_TRUE(stats.indexReplaced);
    EXPECT_EQ(ops.parses, scenario.expectedParses);
    EXPECT_EQ(indexDigest, scenario.expectedIndex);
  }
}

TEST_F(LibraryProbeTest, FailedMetadataIsRetriedOnEveryRefresh) {
  const std::string failing = bookPath(50, 2);
  bookMetadata[failing].success = false;
  uint64_t indexDigest = 0;
  rebuild("failed-metadata", [&failing] { fake::files[failing]->time++; }, indexDigest);
  const auto failedCard = deepCopy(fake::files);
  committedCard = failedCard;

  const SdOps ops = rebuild("retry-failed", [] {}, indexDigest);
  bookMetadata[failing].success = true;

  EXPECT_EQ(ops.parses, 1u);
  EXPECT_EQ(stats.metadataReused, BOOKS - 1);
  EXPECT_TRUE(stats.indexReplaced);
}

TEST_F(LibraryProbeTest, DuplicateEntriesAndSidecarsStillCountAsUnchanged) {
  uint64_t indexDigest = 0;
  const SdOps ops = rebuild(
      "duplicate-entry",
      [] {
        fake::duplicateDirectoryEntry(bookPath(3, 3));
        fake::add("/Author 03/notes.txt.bak", "not a book");
        fake::add("/.hidden/ghost.epub", "hidden");
      },
      indexDigest);

  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(stats.duplicatesDropped, 1);
  EXPECT_EQ(ops.writes, 0u);
  EXPECT_EQ(fake::files[INDEX]->bytes, committedIndex);
}

TEST_F(LibraryProbeTest, ReadFailuresInEitherPassKeepTheCommittedIndex) {
  for (const bool changed : {false, true}) {
    SCOPED_TRACE(changed ? "changed" : "unchanged");
    const auto change = [changed] {
      if (changed) {
        fake::files[bookPath(AUTHORS - 1, 0)]->time++;
      }
    };
    uint64_t expected = 0;
    rebuild("read-fault-probe", change, expected);
    const unsigned reads = fake::reads;
    // About forty faults spread over both passes, the reuse reads and the emit.
    for (unsigned at = 0; at < reads; at += reads / 41 + 1) {
      SCOPED_TRACE(at);
      restoreCard();
      change();
      fake::failRead = static_cast<int>(at);
      const bool ok = buildLibraryIndex("/", stats, true);
      fake::failRead = -1;
      if (ok) {
        EXPECT_EQ(digest(fake::files[INDEX]->bytes), expected);
      } else {
        EXPECT_EQ(fake::files[INDEX]->bytes, committedIndex);
      }
      EXPECT_FALSE(Storage.exists(STAGE));
      EXPECT_FALSE(Storage.exists(FOLDER_STAGE));
    }
  }
}

TEST_F(LibraryProbeTest, DirectoryFailuresInEitherPassKeepTheCommittedIndex) {
  for (const bool changed : {false, true}) {
    SCOPED_TRACE(changed ? "changed" : "unchanged");
    const auto change = [changed] {
      if (changed) {
        fake::add("/Zed Author/New Book/New Book.epub", std::string(5001, 'n'), 9001);
      }
    };
    unsigned failures = 0;
    for (int at = 0; at < 4000; at += 211) {
      SCOPED_TRACE(at);
      restoreCard();
      change();
      fake::failNext = at;
      const bool ok = buildLibraryIndex("/", stats, true);
      if (!fake::failureTriggered) {
        EXPECT_TRUE(ok);
        break;
      }
      ++failures;
      EXPECT_FALSE(ok);
      EXPECT_EQ(fake::files[INDEX]->bytes, committedIndex);
      EXPECT_FALSE(Storage.exists(STAGE));
    }
    EXPECT_GT(failures, 3u);
  }
}

TEST_F(LibraryProbeTest, AllocationFailuresInEitherPassKeepTheCommittedIndex) {
  for (const bool changed : {false, true}) {
    SCOPED_TRACE(changed ? "changed" : "unchanged");
    const auto change = [changed] {
      if (changed) {
        Storage.remove(bookPath(AUTHORS - 1, TITLES_PER_AUTHOR - 1).c_str());
      }
    };
    uint64_t expected = 0;
    rebuild("alloc-fault-probe", change, expected);
    const size_t allocations = fake::allocations.size();
    for (size_t at = 0; at < allocations; ++at) {
      SCOPED_TRACE(at);
      restoreCard();
      change();
      fake::failAlloc = static_cast<int>(at);
      const bool ok = buildLibraryIndex("/", stats, true);
      fake::failAlloc = -1;
      if (!ok) {
        EXPECT_EQ(fake::files[INDEX]->bytes, committedIndex);
      } else if (!changed) {
        EXPECT_EQ(fake::files[INDEX]->bytes, committedIndex);
      } else {
        LibraryIndexFile index;
        ASSERT_TRUE(index.open(INDEX));
        EXPECT_EQ(index.bookCount(), BOOKS - 1);
      }
      EXPECT_FALSE(Storage.exists(STAGE));
    }
  }
}

TEST_F(LibraryProbeTest, DedupAllocationFailureUsesTheStagingPath) {
  restoreCard();
  clearOps();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  size_t dedupAllocation = fake::allocations.size();
  for (size_t i = 0; i < fake::allocations.size(); ++i) {
    if (fake::allocations[i] == LIBRARY_MAX_DEDUP_KEYS * sizeof(uint64_t)) {
      dedupAllocation = i;
      break;
    }
  }
  ASSERT_LT(dedupAllocation, fake::allocations.size());

  restoreCard();
  fake::failAlloc = static_cast<int>(dedupAllocation);
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  const SdOps ops = captureOps();

  EXPECT_TRUE(stats.dedupDegraded);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_GT(ops.stageWriteOpens, 0u);
  EXPECT_EQ(fake::files[INDEX]->bytes, committedIndex);
}

TEST_F(LibraryProbeTest, PartialPreviousIndexUsesTheStagingPath) {
  restoreCard();
  std::string deep;
  for (int i = 0; i <= LIBRARY_MAX_DEPTH; ++i) deep += "/deep";
  fake::add(deep + "/hidden.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  ASSERT_TRUE(stats.limitsReached);
  clearOps();

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  const SdOps ops = captureOps();

  EXPECT_TRUE(stats.limitsReached);
  EXPECT_TRUE(stats.indexReplaced);
  EXPECT_EQ(ops.parses, 0u);
  EXPECT_GT(ops.stageWriteOpens, 0u);
}

TEST_F(LibraryProbeTest, CorruptSeriesReferenceFailsBeforeAnyStagingWrite) {
  restoreCard();
  ClixHeader header{};
  std::memcpy(&header, fake::files[INDEX]->bytes.data(), sizeof(header));
  const uint16_t invalidSeries = 0x7FFE;
  std::memcpy(fake::files[INDEX]->bytes.data() + header.seriesRefStart + 400 * sizeof(ClixSeriesRef), &invalidSeries,
              sizeof(invalidSeries));
  const auto corrupt = fake::files[INDEX]->bytes;
  clearOps();

  EXPECT_FALSE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::files[INDEX]->bytes, corrupt);
  EXPECT_EQ(captureOps().stageWriteOpens, 0u);
  EXPECT_EQ(captureOps().parses, 0u);
}

namespace {
unsigned entriesBeforeMutation = 0;
void mutateAfterEntries() {
  if (entriesBeforeMutation-- == 0) {
    markLibraryIndexDirty();
    return;
  }
  fake::onNextEntry = mutateAfterEntries;
}
}  // namespace

TEST_F(LibraryProbeTest, InvalidationDuringEitherPassKeepsTheRefreshPending) {
  // The probe makes about 2,400 directory calls before the new folder stops it,
  // so the last figure lands in the staging pass that follows.
  for (const unsigned at : {10u, 2000u, 3000u}) {
    SCOPED_TRACE(at);
    restoreCard();
    fake::add("/Zed Author/New Book/New Book.epub", std::string(5001, 'n'), 9001);
    const uint32_t token = librarySession.refreshToken();
    librarySession.reconciled(true, token);
    entriesBeforeMutation = at;
    fake::onNextEntry = mutateAfterEntries;

    ASSERT_TRUE(buildLibraryIndex("/", stats, true));
    ASSERT_EQ(fake::onNextEntry, nullptr) << "the build ended before the mutation";
    librarySession.reconciled(true, token);

    EXPECT_TRUE(isLibraryIndexDirty());
    EXPECT_TRUE(librarySession.needsRefresh(true, true));
    ASSERT_TRUE(buildLibraryIndex("/", stats, true));
    EXPECT_FALSE(isLibraryIndexDirty());
  }
}
