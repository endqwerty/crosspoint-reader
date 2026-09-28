#include <gtest/gtest.h>

#include "../huge_book_index/HeapCap.h"
// Keep the private staging function unchanged and test it in its own translation unit.
#include "../../lib/LibraryIndex/LibraryBuilder.cpp"

namespace {

class LibraryStagingTest : public testing::Test {
 protected:
  library::WalkState state;
  library::StagedEntry staged{};
  library::PriorEntry prior{};
  library::ClixRecord oldRecord{};
  library::LibraryIndexFile previous;
  library::BuildStats stats;
  std::unique_ptr<serialization::BufferedFileWriter> writer;
  std::string name;
  std::string path;

  void SetUp() override {
    fake::reset();
    bookMetadata.clear();
    name = std::string(180, 'n') + ".epub";
    path = "/" + name;
  }

  void prepare(const FakeMetadata& metadata, bool reuse, bool enabled = true) {
    bookMetadata[path] = metadata;
    fake::add(path);
    if (reuse) {
      ASSERT_TRUE(library::buildLibraryIndex("/", stats, enabled));
      ASSERT_TRUE(previous.open("/.crosspoint/library.idx"));
      ASSERT_TRUE(previous.readRecord(0, oldRecord));
      prior = {library::fnv1a64(path.data(), path.size()), 4, oldRecord.firstSeen, 0};
      state.previous = &previous;
      state.prior = &prior;
      state.priorCount = 1;
    }
    ASSERT_TRUE(Storage.openFileForWrite("TEST", "/stage", state.stage));
    writer = std::make_unique<serialization::BufferedFileWriter>(state.stage, sizeof(staged) + 1);
    state.stageOut = writer.get();
    state.stagedEntry = &staged;
    state.stats = &stats;
    state.readMetadata = enabled;
    stats = {};
    fake::resetIoCounters();
    fake::parses = 0;
  }

  bool stage() { return library::stageRecord(state, name, 4, 0, path, 1); }

  void measureReuse(const bool emptyTitle) {
    prepare({emptyTitle ? "" : "Title", "A", "", ""}, true);
    ASSERT_TRUE(writer);
    constexpr unsigned COUNT = 2000;
    size_t calls = 0;
    for (unsigned i = 0; i < COUNT; ++i) {
      prior.ordinalAndMatched = 0;
      heapcap::reset(SIZE_MAX);
      const bool ok = stage();
      heapcap::stop();
      calls += heapcap::allocationCalls();
      ASSERT_TRUE(ok);
      ASSERT_EQ(heapcap::live(), 0u);
      ASSERT_TRUE(writer->flush());
    }
    std::printf("STAGING_REUSE empty=%d books=%u allocations=%zu reads=%u seeks=%u bytes=%zu\n", emptyTitle, COUNT,
                calls, fake::reads, fake::seeks, fake::bytesRead);
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(stats.metadataReused, COUNT);
    EXPECT_EQ(fake::parses, 0u);
    EXPECT_EQ(staged.record.foldLen, oldRecord.foldLen);
    EXPECT_EQ(std::memcmp(staged.record.fold, oldRecord.fold, sizeof(oldRecord.fold)), 0);
    EXPECT_EQ(staged.titleLen, emptyTitle ? 0 : 5);
    EXPECT_EQ(staged.authorLen, 1);
    EXPECT_EQ(staged.author[0], 'A');
    EXPECT_EQ(staged.record.nameLen, name.size());
    EXPECT_EQ(std::memcmp(staged.name, name.data(), name.size()), 0);
  }
};

TEST_F(LibraryStagingTest, ReusedTitleAvoidsFilenameAllocation) { measureReuse(false); }
TEST_F(LibraryStagingTest, ReusedMissingTitleAvoidsFilenameAllocation) { measureReuse(true); }

TEST_F(LibraryStagingTest, AllocationCounterObservesLongString) {
  heapcap::reset(SIZE_MAX);
  {
    std::string control;
    control.reserve(512);
    control.assign(180, 'x');
    heapcap::observeByte(control.data());
  }
  heapcap::stop();
  EXPECT_GT(heapcap::allocationCalls(), 0u);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST_F(LibraryStagingTest, FreshMetadataAndFilenameFallbackKeepStagedBytes) {
  // Includes disabled extraction, missing title, failed extraction, multiple dots and UTF-8.
  const char* names[] = {"Multiple.parts.name.epub", "Livre-\xC3\x89t\xC3\xA9.epub", "NoExtension", ".hidden"};
  for (const char* fileName : names) {
    for (unsigned mode = 0; mode < 4; ++mode) {
      writer.reset();
      fake::reset();
      bookMetadata.clear();
      state = {};
      name = fileName;
      path = "/" + name;
      const bool hasTitle = mode == 0;
      prepare({hasTitle ? "Book's title" : "", "Writer", "Series", "2", mode != 2}, false, mode != 3);
      ASSERT_TRUE(writer);
      ASSERT_TRUE(stage());
      ASSERT_TRUE(writer->flush());
      const bool extraction = mode != 3 && FsHelpers::hasEpubExtension(name);
      const bool titleFromBook = extraction && hasTitle;
      const auto expectedFold = library::fold(titleFromBook ? "Book's title" : library::stemOf(name));
      EXPECT_EQ(std::string(staged.record.fold, staged.record.foldLen), expectedFold) << fileName << ":" << mode;
      EXPECT_EQ(std::string(staged.title, staged.titleLen), titleFromBook ? "Book's title" : "");
      EXPECT_EQ(std::string(staged.name, staged.record.nameLen), name);
      EXPECT_EQ(staged.record.metadataStatus, !extraction ? library::CLIX_METADATA_NOT_ATTEMPTED
                                              : mode == 2 ? library::CLIX_METADATA_FAILED
                                                          : library::CLIX_METADATA_EXTRACTED);
      const auto hash = library::fnv1a64(reinterpret_cast<const char*>(&staged), sizeof(staged));
      std::printf("STAGING_BYTES name=%s mode=%u hash=%016llx\n", fileName, mode,
                  static_cast<unsigned long long>(hash));
    }
  }
}

TEST_F(LibraryStagingTest, ReusedMetadataReadFailureDoesNotStageRecord) {
  prepare({"Title", "Author", "", ""}, true);
  ASSERT_TRUE(writer);
  fake::failRead = 1;  // Record succeeds; metadata fails.
  EXPECT_FALSE(stage());
  EXPECT_TRUE(state.failed);
  EXPECT_EQ(state.books, 0);
  EXPECT_EQ(writer->position(), 0u);
  EXPECT_EQ(stats.metadataReused, 0);
}

}  // namespace

TEST_F(LibraryStagingTest, CachedAuthorTruncationStillRequiresCleanup) {
  prepare({"Title", std::string(127, 'a') + " Z", "", ""}, true);
  ASSERT_TRUE(writer);
  std::string source;
  ASSERT_TRUE(previous.readSourceAuthor(oldRecord, source));
  ASSERT_EQ(source.size(), 128u);
  ASSERT_EQ(source.back(), ' ');
  ASSERT_TRUE(stage());
  EXPECT_EQ(staged.authorLen, 127u);
  EXPECT_EQ(std::string(staged.author, staged.authorLen), std::string(127, 'a'));
  EXPECT_EQ(stats.metadataReused, 1);
}
