#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "LibraryIndexFile.h"
#include "Memory.h"

namespace library {

std::string joinLibraryPath(const std::string_view folder, const std::string_view name) {
  return std::string(folder) + "/" + std::string(name);
}

}  // namespace library

namespace {

std::vector<uint8_t> makeBlob(const uint64_t pathHash, const std::initializer_list<uint8_t> fields) {
  std::vector<uint8_t> blob(sizeof(pathHash) + fields.size());
  std::memcpy(blob.data(), &pathHash, sizeof(pathHash));
  std::copy(fields.begin(), fields.end(), blob.begin() + sizeof(pathHash));
  return blob;
}

library::ClixHeader storeMetadata(const std::string& author, const std::string& title,
                                  const std::string& sourceAuthor) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  auto blob = makeBlob(1, {'x'});
  for (const auto* value : {&author, &title, &sourceAuthor}) {
    blob.push_back(static_cast<uint8_t>(value->size()));
    blob.insert(blob.end(), value->begin(), value->end());
  }
  library::layoutSections(header, 0, blob.size());
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
  Storage.setFile("/library.clx", std::move(bytes));
  return header;
}

void storeRecords(const uint16_t count) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = count;
  library::layoutSections(header, 0, 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  for (uint16_t ordinal = 0; ordinal < count; ++ordinal) {
    library::ClixRecord record{};
    record.firstSeen = ordinal;
    std::memcpy(bytes.data() + library::recordOffset(header, ordinal), &record, sizeof(record));
  }
  Storage.setFile("/library.clx", std::move(bytes));
}

void storeIdentities(const uint16_t count, const unsigned fault = 0) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = count;
  library::layoutSections(header, 0, count * sizeof(uint64_t));
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  for (uint16_t i = 0; i < count; ++i) {
    library::ClixRecord record{};
    record.fileSize = 100 + i;
    record.nameOff = fault == 1 ? UINT32_MAX : i * sizeof(uint64_t);
    const uint64_t hash = i + 1;
    std::memcpy(bytes.data() + library::recordOffset(header, i), &record, sizeof(record));
    std::memcpy(bytes.data() + header.nameStart + i * sizeof(uint64_t), &hash, sizeof(hash));
    const uint16_t ordinal = fault == 3 ? count : fault == 2 ? 0 : count - i - 1;
    std::memcpy(bytes.data() + library::arrivalOrderOffset(header, i), &ordinal, sizeof(ordinal));
  }
  Storage.setFile("/library.clx", std::move(bytes));
}

}  // namespace

TEST(LibraryIndexFile, MissingIndexDoesNotCloseAnUninitializedHandle) {
  Storage.clearFile();
  HalFile::resetInvalidCloseCount();

  {
    library::LibraryIndexFile index;
    EXPECT_FALSE(index.open("/missing.clx"));
  }

  EXPECT_EQ(HalFile::invalidCloseCount(), 0);
}

TEST(LibraryIndexFile, ReadsEveryStoredOrderInBothDirections) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 3;
  library::layoutSections(header, 0, 0);

  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t authorOrder[] = {2, 0, 1};
  const uint16_t arrivalOrder[] = {1, 2, 0};
  std::memcpy(bytes.data() + library::authorOrderOffset(header, 0), authorOrder, sizeof(authorOrder));
  std::memcpy(bytes.data() + library::arrivalOrderOffset(header, 0), arrivalOrder, sizeof(arrivalOrder));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));

  const auto expectOrder = [&](const library::SortOrder order, const uint16_t a, const uint16_t b, const uint16_t c) {
    EXPECT_EQ(index.ordinalForRow(order, 0), a);
    EXPECT_EQ(index.ordinalForRow(order, 1), b);
    EXPECT_EQ(index.ordinalForRow(order, 2), c);
    EXPECT_EQ(index.ordinalForRow(order, 3), 0xFFFF);
  };
  expectOrder(library::SortOrder::AddedAsc, 1, 2, 0);
  expectOrder(library::SortOrder::AddedDesc, 0, 2, 1);
  expectOrder(library::SortOrder::TitleAsc, 0, 1, 2);
  expectOrder(library::SortOrder::TitleDesc, 2, 1, 0);
  expectOrder(library::SortOrder::AuthorAsc, 2, 0, 1);
  expectOrder(library::SortOrder::AuthorDesc, 1, 0, 2);
}

TEST(LibraryIndexFile, RejectsInvalidPermutationOrdinal) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  library::layoutSections(header, 0, 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t invalid = 1;
  std::memcpy(bytes.data() + library::authorOrderOffset(header, 0), &invalid, sizeof(invalid));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0xFFFF);
}

TEST(LibraryIndexFile, ReadsPathHashAndEveryPublicBlobField) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  const uint8_t folder[] = {6, '/', 'b', 'o', 'o', 'k', 's'};
  constexpr uint64_t PATH_HASH = 0x0123456789ABCDEFULL;
  const auto blob = makeBlob(PATH_HASH, {'x', 1, 'a', 1, 't', 8, 'O', 'r', 'i', 'g', 'i', 'n', 'a', 'l'});
  header.folderCount = 1;
  library::layoutSections(header, sizeof(folder), blob.size());
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folder, sizeof(folder));
  std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
  library::ClixRecord record{};
  record.nameLen = 1;
  Storage.setFile("/library.clx", bytes);

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  uint64_t pathHash = 0;
  ASSERT_TRUE(index.readPathHash(record, pathHash));
  EXPECT_EQ(pathHash, PATH_HASH);
  std::string name;
  ASSERT_TRUE(index.readName(record, name));
  EXPECT_EQ(name, "x");
  std::string author;
  ASSERT_TRUE(index.readAuthor(record, author));
  EXPECT_EQ(author, "a");
  std::string title;
  ASSERT_TRUE(index.readTitle(record, title));
  EXPECT_EQ(title, "t");
  ASSERT_TRUE(index.readSourceAuthor(record, author));
  EXPECT_EQ(author, "Original");
  std::string path;
  ASSERT_TRUE(index.readPath(record, path));
  EXPECT_EQ(path, "/books/x");
  index.close();

  bytes[header.nameStart + sizeof(PATH_HASH) + 5] = 255;
  Storage.setFile("/library.clx", std::move(bytes));
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_FALSE(index.readSourceAuthor(record, author));
}

TEST(LibraryIndexFile, RejectsTruncatedAndOverflowingPathHashes) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  library::layoutSections(header, 0, sizeof(uint64_t) - 1);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  uint64_t hash = 1;
  EXPECT_FALSE(index.readPathHash(record, hash));
  EXPECT_EQ(hash, 0u);
  EXPECT_TRUE(index.ioFailed());

  record.nameOff = UINT32_MAX;
  EXPECT_FALSE(index.readPathHash(record, hash));
}

TEST(LibraryIndexFile, RejectsFolderRecordBeyondFolderBlob) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  const uint8_t folder[] = {5, '/'};
  const auto blob = makeBlob(1, {'x', 0, 0, 0});
  library::layoutSections(header, sizeof(folder), blob.size());
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folder, sizeof(folder));
  std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
  library::ClixRecord record{};
  record.nameLen = 1;
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  std::string path;
  EXPECT_FALSE(index.readPath(record, path));
}

TEST(LibraryIndexFile, ReadAndSeekFaultsLatchAfterOpenUntilExplicitReopen) {
  for (int fault = 0; fault < 3; ++fault) {
    SCOPED_TRACE(fault);
    library::ClixHeader header{};
    std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
    header.formatVersion = library::CLIX_FORMAT_VERSION;
    header.foldVersion = library::CLIX_FOLD_VERSION;
    header.bookCount = 2;
    library::layoutSections(header, 0, 0);
    std::vector<uint8_t> bytes(header.selfSize, 0);
    std::memcpy(bytes.data(), &header, sizeof(header));
    Storage.setFile("/library.clx", std::move(bytes));
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    if (fault == 0) HalFile::nextReadLimit = 7;
    if (fault == 1) HalFile::failRead = true;
    if (fault == 2) HalFile::failSeek = true;
    library::ClixRecord record{};
    EXPECT_FALSE(index.readRecord(1, record));
    EXPECT_TRUE(index.ioFailed());
    ASSERT_TRUE(index.readRecord(1, record));
    EXPECT_TRUE(index.ioFailed());
    ASSERT_TRUE(index.open("/library.clx"));
    EXPECT_FALSE(index.ioFailed());
    EXPECT_TRUE(index.readRecord(0, record));
  }
}

TEST(LibraryIndexFile, TruncationAfterHeaderValidationCannotReturnPartialRecord) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  library::layoutSections(header, 0, 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  Storage.setFile("/library.clx", std::move(bytes));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  Storage.truncateFile(library::recordOffset(header, 0) + sizeof(library::ClixRecord) - 1);
  library::ClixRecord record{};
  EXPECT_FALSE(index.readRecord(0, record));
  EXPECT_TRUE(index.ioFailed());
  EXPECT_FALSE(index.open("/library.clx"));
  EXPECT_FALSE(index.isOpen());
  EXPECT_EQ(index.bookCount(), 0);
}

TEST(LibraryIndexFile, SequentialRecordScanUsesOneSeekAndNoReadAheadBuffer) {
  storeRecords(library::CLIX_MAX_RECORDS);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  HalFile::resetIoCounters();
  for (uint16_t ordinal = 0; ordinal < index.bookCount(); ++ordinal) {
    library::ClixRecord record{};
    ASSERT_TRUE(index.readRecord(ordinal, record));
    ASSERT_EQ(record.firstSeen, ordinal);
  }
  EXPECT_EQ(HalFile::readCalls, library::CLIX_MAX_RECORDS);
  EXPECT_EQ(HalFile::seekCalls, 1u);
  EXPECT_EQ(HalFile::bytesRead, library::CLIX_MAX_RECORDS * sizeof(library::ClixRecord));
}

TEST(LibraryIndexFile, PartialReadInvalidatesCursorBeforeRetry) {
  storeRecords(3);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  HalFile::resetIoCounters();
  HalFile::nextReadLimit = 7;
  EXPECT_FALSE(index.readRecord(1, record));
  EXPECT_EQ(HalFile::seekCalls, 0u);
  ASSERT_TRUE(index.readRecord(1, record));
  EXPECT_EQ(record.firstSeen, 1u);
  EXPECT_EQ(HalFile::seekCalls, 1u);
  EXPECT_TRUE(index.ioFailed());
}

TEST(LibraryIndexFile, FailedSeekInvalidatesCursorEvenWhenRetryTargetsPreviousPosition) {
  storeRecords(3);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  HalFile::resetIoCounters();
  HalFile::failSeek = true;
  EXPECT_FALSE(index.readRecord(2, record));
  ASSERT_TRUE(index.readRecord(1, record));
  EXPECT_EQ(record.firstSeen, 1u);
  EXPECT_EQ(HalFile::seekCalls, 2u);
  EXPECT_TRUE(index.ioFailed());
}

TEST(LibraryIndexFile, CloseAndReopenResetTrackedPositionAndReadErrors) {
  storeRecords(3);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  index.close();
  EXPECT_FALSE(index.readRecord(1, record));
  ASSERT_TRUE(index.open("/library.clx"));
  HalFile::resetIoCounters();
  ASSERT_TRUE(index.readRecord(1, record));
  EXPECT_EQ(record.firstSeen, 1u);
  EXPECT_EQ(HalFile::seekCalls, 1u);
  HalFile::failRead = true;
  EXPECT_FALSE(index.readRecord(2, record));
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_FALSE(index.ioFailed());
  HalFile::resetIoCounters();
  ASSERT_TRUE(index.readRecord(0, record));
  EXPECT_EQ(record.firstSeen, 0u);
  EXPECT_EQ(HalFile::seekCalls, 1u);
}

TEST(LibraryIndexFile, CombinedMetadataUsesOneReadAndPreservesSourceSpelling) {
  const auto header = storeMetadata("Example Writer", "Example Book", "Writer, Example");
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  record.nameLen = 1;
  std::string title, author;
  HalFile::resetIoCounters();
  ASSERT_TRUE(index.readTitleAndAuthor(record, title, author));
  EXPECT_EQ(title, "Example Book");
  EXPECT_EQ(author, "Example Writer");
  EXPECT_EQ(HalFile::readCalls, 1u);
  EXPECT_EQ(HalFile::seekCalls, 1u);
  EXPECT_EQ(HalFile::bytesRead, header.nameLen - sizeof(uint64_t) - record.nameLen);
  HalFile::resetIoCounters();
  ASSERT_TRUE(index.readTitleAndSourceAuthor(record, title, author));
  EXPECT_EQ(title, "Example Book");
  EXPECT_EQ(author, "Writer, Example");
  EXPECT_EQ(HalFile::readCalls, 1u);
  EXPECT_EQ(HalFile::seekCalls, 1u);
}

TEST(LibraryIndexFile, CombinedMetadataStreamsMaximumFieldsInBoundedContiguousReads) {
  const std::string canonical(255, 'a');
  const std::string bookTitle(255, 't');
  const std::string original(255, 's');
  const auto header = storeMetadata(canonical, bookTitle, original);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  record.nameLen = 1;
  std::string title, author;
  HalFile::resetIoCounters();
  ASSERT_TRUE(index.readTitleAndAuthor(record, title, author));
  EXPECT_EQ(title, bookTitle);
  EXPECT_EQ(author, canonical);
  EXPECT_EQ(HalFile::readCalls, 4u);
  EXPECT_EQ(HalFile::seekCalls, 1u);
  EXPECT_EQ(HalFile::bytesRead, 512u);
  HalFile::resetIoCounters();
  ASSERT_TRUE(index.readTitleAndSourceAuthor(record, title, author));
  EXPECT_EQ(title, bookTitle);
  EXPECT_EQ(author, original);
  EXPECT_EQ(HalFile::readCalls, 5u);
  EXPECT_EQ(HalFile::seekCalls, 2u);
  EXPECT_EQ(HalFile::bytesRead, header.nameLen - sizeof(uint64_t) - 1u - 192u);
}

TEST(LibraryIndexFile, CombinedMetadataKeepsEmptyFieldsDistinctFromCorruption) {
  for (const bool source : {false, true}) {
    SCOPED_TRACE(source);
    storeMetadata("", "", "");
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    library::ClixRecord record{};
    record.nameLen = 1;
    std::string title = "old title", author = "old author";
    EXPECT_TRUE(source ? index.readTitleAndSourceAuthor(record, title, author)
                       : index.readTitleAndAuthor(record, title, author));
    EXPECT_TRUE(title.empty());
    EXPECT_TRUE(author.empty());
    EXPECT_FALSE(index.ioFailed());
    record.nameOff = UINT32_MAX;
    title = author = "stale";
    EXPECT_FALSE(source ? index.readTitleAndSourceAuthor(record, title, author)
                        : index.readTitleAndAuthor(record, title, author));
    EXPECT_TRUE(title.empty());
    EXPECT_TRUE(author.empty());
  }
}

TEST(LibraryIndexFile, CombinedMetadataRejectsEveryTruncatedFieldAndNeverReadsPastBlob) {
  for (size_t size = 0; size < 9; ++size) {
    SCOPED_TRACE(size);
    library::ClixHeader truncated{};
    std::memcpy(truncated.magic, library::CLIX_MAGIC, sizeof(truncated.magic));
    truncated.formatVersion = library::CLIX_FORMAT_VERSION;
    truncated.foldVersion = library::CLIX_FOLD_VERSION;
    truncated.bookCount = 1;
    library::layoutSections(truncated, 0, sizeof(uint64_t) + 1 + size);
    auto blob = makeBlob(1, {'x', 2, 'a', 'a', 2, 't', 't', 2, 's', 's'});
    blob.resize(sizeof(uint64_t) + 1 + size);
    std::vector<uint8_t> bytes(truncated.selfSize, 0);
    std::memcpy(bytes.data(), &truncated, sizeof(truncated));
    std::memcpy(bytes.data() + truncated.nameStart, blob.data(), blob.size());
    Storage.setFile("/library.clx", std::move(bytes));
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    library::ClixRecord record{};
    record.nameLen = 1;
    std::string title, author;
    HalFile::resetIoCounters();
    EXPECT_FALSE(index.readTitleAndSourceAuthor(record, title, author));
    EXPECT_TRUE(title.empty());
    EXPECT_TRUE(author.empty());
    EXPECT_LE(HalFile::bytesRead, size);
    EXPECT_FALSE(index.ioFailed());
  }
}

TEST(LibraryIndexFile, CombinedMetadataReadFailuresDiscardPartialOutputAndCanRetry) {
  for (const bool shortRead : {false, true}) {
    SCOPED_TRACE(shortRead);
    for (int failure = 0; failure < 5; ++failure) {
      SCOPED_TRACE(failure);
      storeMetadata(std::string(255, 'a'), std::string(255, 't'), std::string(255, 's'));
      library::LibraryIndexFile index;
      ASSERT_TRUE(index.open("/library.clx"));
      library::ClixRecord record{};
      record.nameLen = 1;
      std::string title, author;
      if (shortRead)
        HalFile::shortReadAfter = failure;
      else
        HalFile::failReadAfter = failure;
      EXPECT_FALSE(index.readTitleAndSourceAuthor(record, title, author));
      EXPECT_TRUE(title.empty());
      EXPECT_TRUE(author.empty());
      EXPECT_TRUE(index.ioFailed());
      ASSERT_TRUE(index.readTitleAndSourceAuthor(record, title, author));
      EXPECT_EQ(title, std::string(255, 't'));
      EXPECT_EQ(author, std::string(255, 's'));
    }
  }
}

TEST(LibraryIndexFile, ResolvesRecentRowsByIdentity) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 3;
  // One 8-byte path hash blob per record.
  library::layoutSections(header, 0, 3 * sizeof(uint64_t));
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));

  // Ordinals 0 and 2 share a size, so only the hash can tell them apart.
  constexpr uint64_t HASHES[] = {11, 22, 33};
  constexpr uint32_t SIZES[] = {100, 200, 100};
  for (uint16_t ordinal = 0; ordinal < 3; ordinal++) {
    library::ClixRecord record{};
    record.fileSize = SIZES[ordinal];
    record.nameOff = ordinal * sizeof(uint64_t);
    std::memcpy(bytes.data() + library::recordOffset(header, ordinal), &record, sizeof(record));
    std::memcpy(bytes.data() + header.nameStart + record.nameOff, &HASHES[ordinal], sizeof(uint64_t));
  }
  const uint16_t arrivalOrder[] = {1, 2, 0};
  std::memcpy(bytes.data() + library::arrivalOrderOffset(header, 0), arrivalOrder, sizeof(arrivalOrder));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::BookIdentity books[] = {
      {HASHES[0], SIZES[0]},  // ordinal 0 -> ascending row 2
      {HASHES[2], SIZES[2]},  // same size as ordinal 0, hash picks ordinal 2 -> row 1
      {99, SIZES[0]},         // size matches, hash does not: absent
      {HASHES[1], 999},       // hash matches, size does not: absent
      {HASHES[1], 0},         // size unknown: the hash alone matches -> row 0
  };
  uint16_t rows[5] = {};
  ASSERT_TRUE(index.recentRowsFor(books, 5, rows));
  EXPECT_EQ(rows[0], 2);
  EXPECT_EQ(rows[1], 1);
  EXPECT_EQ(rows[2], 0xFFFF);
  EXPECT_EQ(rows[3], 0xFFFF);
  EXPECT_EQ(rows[4], 0);
}

TEST(LibraryIndexFile, RecentIdentityLookupIsBoundedAndBatchesRecordReads) {
  storeIdentities(35);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::BookIdentity books[] = {{2, 101}, {35, 134}};
  uint16_t rows[2];
  HalFile::resetIoCounters();
  ASSERT_TRUE(index.recentRowsFor(books, 2, rows));
  EXPECT_EQ(rows[0], 33);
  EXPECT_EQ(rows[1], 0);
  EXPECT_EQ(HalFile::readCalls, 5u);  // two record chunks, two hashes, one permutation chunk
  library::ClixRecord record;
  ASSERT_TRUE(index.readRecord(34, record));
  EXPECT_EQ(record.fileSize, 134u);
}

TEST(LibraryIndexFile, RecentIdentityLookupRejectsInvalidArgumentsWithoutWritingOutput) {
  storeIdentities(1);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::BookIdentity book{1, 100};
  uint16_t row = 123;
  EXPECT_FALSE(index.recentRowsFor(&book, library::LibraryIndexFile::MAX_IDENTITY_LOOKUPS + 1, &row));
  EXPECT_EQ(row, 123);
  EXPECT_FALSE(index.recentRowsFor(nullptr, 1, &row));
  EXPECT_FALSE(index.recentRowsFor(&book, 1, nullptr));
  EXPECT_TRUE(index.recentRowsFor(nullptr, 0, nullptr));
  index.close();
  EXPECT_FALSE(index.recentRowsFor(&book, 1, &row));
  EXPECT_EQ(row, 0xFFFF);
}

TEST(LibraryIndexFile, RecentIdentityLookupRejectsAllocationReadAndSeekFailures) {
  storeIdentities(1);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::BookIdentity book{1, 100};
  uint16_t row = 123;
  failNextIndexAllocation = true;
  EXPECT_FALSE(index.recentRowsFor(&book, 1, &row));
  EXPECT_EQ(row, 0xFFFF);
  HalFile::failRead = true;
  EXPECT_FALSE(index.recentRowsFor(&book, 1, &row));
  EXPECT_TRUE(index.ioFailed());
  ASSERT_TRUE(index.open("/library.clx"));
  HalFile::failSeek = true;
  EXPECT_FALSE(index.recentRowsFor(&book, 1, &row));
  EXPECT_TRUE(index.ioFailed());
  ASSERT_TRUE(index.open("/library.clx"));
  ASSERT_TRUE(index.recentRowsFor(&book, 1, &row));
  EXPECT_EQ(row, 0);
}

TEST(LibraryIndexFile, RecentIdentityLookupRejectsCorruptHashesAndArrivalRows) {
  const library::BookIdentity book{1, 100};
  uint16_t row;
  for (unsigned fault = 1; fault <= 3; ++fault) {
    storeIdentities(2, fault);
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    EXPECT_FALSE(index.recentRowsFor(&book, 1, &row)) << fault;
    EXPECT_TRUE(index.ioFailed()) << fault;
  }
}

namespace {
// Equal-length names exercise the worst case for the basename-length prefilter.
std::string anchorName(uint16_t ordinal) {
  char name[24];
  snprintf(name, sizeof(name), "book-%04u.epub", ordinal);
  return name;
}
std::vector<uint8_t> pathIndexBytes(uint16_t count, bool equalSizes = false, bool equalHashes = false) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = count;
  header.folderCount = 1;
  const uint8_t folder[] = {6, '/', 'b', 'o', 'o', 'k', 's'};
  const size_t stride = sizeof(uint64_t) + anchorName(0).size();
  library::layoutSections(header, sizeof(folder), count * stride);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folder, sizeof(folder));
  for (uint16_t i = 0; i < count; ++i) {
    library::ClixRecord record{};
    record.fileSize = equalSizes ? 100 : 100 + i;
    record.nameOff = i * stride;
    record.nameLen = anchorName(i).size();
    const uint64_t hash = equalHashes ? 1 : i + 1;
    std::memcpy(bytes.data() + library::recordOffset(header, i), &record, sizeof(record));
    std::memcpy(bytes.data() + header.nameStart + record.nameOff, &hash, sizeof(hash));
    std::memcpy(bytes.data() + header.nameStart + record.nameOff + sizeof(hash), anchorName(i).data(), record.nameLen);
    const uint16_t author = count - i - 1, arrival = (i + 1) % count, series = i;
    std::memcpy(bytes.data() + library::authorOrderOffset(header, i), &author, sizeof(author));
    std::memcpy(bytes.data() + library::arrivalOrderOffset(header, i), &arrival, sizeof(arrival));
    std::memcpy(bytes.data() + library::seriesOrderOffset(header, i), &series, sizeof(series));
  }
  return bytes;
}
}  // namespace

TEST(LibraryIndexFile, RefreshAnchorsResolveEverySortOrderAndDirection) {
  Storage.setFile("/library.clx", pathIndexBytes(4));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::PathIdentity paths[] = {{"/books/book-0000.epub", 1, 100}, {"/books/book-0003.epub", 4, 103}};
  const auto check = [&](library::SortOrder order, uint16_t first, uint16_t second) {
    uint16_t rows[2]{};
    ASSERT_TRUE(index.rowsForPaths(order, paths, 2, rows));
    EXPECT_EQ(rows[0], first);
    EXPECT_EQ(rows[1], second);
  };
  check(library::SortOrder::TitleAsc, 0, 3);
  check(library::SortOrder::TitleDesc, 3, 0);
  check(library::SortOrder::AuthorAsc, 3, 0);
  check(library::SortOrder::AuthorDesc, 0, 3);
  check(library::SortOrder::AddedAsc, 3, 2);
  check(library::SortOrder::AddedDesc, 0, 1);
  check(library::SortOrder::SeriesAsc, 0, 3);
  check(library::SortOrder::SeriesDesc, 3, 0);
}

TEST(LibraryIndexFile, RefreshAnchorsRequireExactPathEvenWithHashCollision) {
  Storage.setFile("/library.clx", pathIndexBytes(4, true, true));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::PathIdentity paths[] = {{"/books/book-0003.epub", 1, 100}, {"/other/book-0000.epub", 1, 100}};
  uint16_t rows[2]{};
  ASSERT_TRUE(index.rowsForPaths(library::SortOrder::TitleAsc, paths, 2, rows));
  EXPECT_EQ(rows[0], 3);
  EXPECT_EQ(rows[1], 0xFFFF);
}

TEST(LibraryIndexFile, RefreshAnchorSizeHintCannotHideAnEditedBook) {
  Storage.setFile("/library.clx", pathIndexBytes(4));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::PathIdentity paths[] = {{"/books/book-0003.epub", 4, 999}, {"/books/book-0001.epub", 2, 0}};
  uint16_t rows[2]{};
  ASSERT_TRUE(index.rowsForPaths(library::SortOrder::AddedDesc, paths, 2, rows));
  EXPECT_EQ(rows[0], 1);
  EXPECT_EQ(rows[1], 3);
}

TEST(LibraryIndexFile, RefreshAnchorLookupPublishesNoPartialResultsOnAnyReadFailure) {
  const library::PathIdentity paths[] = {{"/books/book-0000.epub", 1, 100}, {"/books/book-0003.epub", 4, 999}};
  Storage.setFile("/library.clx", pathIndexBytes(4));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  HalFile::resetIoCounters();
  uint16_t rows[2]{};
  ASSERT_TRUE(index.rowsForPaths(library::SortOrder::AuthorAsc, paths, 2, rows));
  const size_t reads = HalFile::readCalls;
  index.close();
  for (size_t fail = 0; fail < reads; ++fail) {
    SCOPED_TRACE(fail);
    Storage.setFile("/library.clx", pathIndexBytes(4));
    ASSERT_TRUE(index.open("/library.clx"));
    rows[0] = rows[1] = 42;
    HalFile::failReadAfter = fail;
    EXPECT_FALSE(index.rowsForPaths(library::SortOrder::AuthorAsc, paths, 2, rows));
    EXPECT_EQ(rows[0], 0xFFFF);
    EXPECT_EQ(rows[1], 0xFFFF);
    index.close();
  }
  HalFile::resetFaults();
}

TEST(LibraryIndexFile, RefreshAnchorLookupRejectsOomAndShortReads) {
  const library::PathIdentity path{"/books/book-0003.epub", 4, 103};
  for (int fault = 0; fault < 3; ++fault) {
    Storage.setFile("/library.clx", pathIndexBytes(4));
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    uint16_t row = 123;
    if (fault == 0) failNextIndexAllocation = true;
    if (fault == 1) HalFile::nextReadLimit = 1;
    if (fault == 2) HalFile::failSeek = true;
    EXPECT_FALSE(index.rowsForPaths(library::SortOrder::TitleAsc, &path, 1, &row));
    EXPECT_EQ(row, 0xFFFF);
  }
  HalFile::resetFaults();
}

TEST(LibraryIndexFile, RefreshAnchorLookupRejectsCorruptPermutationsAndPathBounds) {
  const library::PathIdentity paths[] = {{"/books/book-0000.epub", 1, 100}, {"/books/book-0003.epub", 4, 103}};
  for (int fault = 0; fault < 5; ++fault) {
    auto bytes = pathIndexBytes(4);
    library::ClixHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (fault < 3) {
      // Duplicate a matched ordinal, omit it, or use an out-of-range ordinal.
      const uint16_t ordinal = fault == 0 ? 3 : fault == 1 ? 2 : 4;
      const size_t row = fault == 0 ? 1 : 0;
      std::memcpy(bytes.data() + library::authorOrderOffset(header, row), &ordinal, sizeof(ordinal));
    } else {
      library::ClixRecord record;
      std::memcpy(&record, bytes.data() + library::recordOffset(header, 0), sizeof(record));
      if (fault == 3)
        record.nameOff = UINT32_MAX;
      else
        record.folderId = 1;
      std::memcpy(bytes.data() + library::recordOffset(header, 0), &record, sizeof(record));
    }
    Storage.setFile("/library.clx", std::move(bytes));
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    uint16_t rows[2]{};
    EXPECT_FALSE(index.rowsForPaths(library::SortOrder::AuthorAsc, paths, 2, rows)) << fault;
    EXPECT_EQ(rows[0], 0xFFFF);
    EXPECT_EQ(rows[1], 0xFFFF);
  }
}

TEST(LibraryIndexFile, RefreshAnchorLookupValidatesInputsAndClosedIndex) {
  library::LibraryIndexFile index;
  uint16_t rows[2]{};
  library::PathIdentity path{"/books/book-0000.epub", 1, 100};
  EXPECT_FALSE(index.rowsForPaths(library::SortOrder::TitleAsc, &path, 1, rows));
  EXPECT_EQ(rows[0], 0xFFFF);
  Storage.setFile("/library.clx", pathIndexBytes(4));
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_TRUE(index.rowsForPaths(library::SortOrder::TitleAsc, nullptr, 0, nullptr));
  EXPECT_FALSE(index.rowsForPaths(library::SortOrder::TitleAsc, &path, 3, rows));
  EXPECT_FALSE(index.rowsForPaths(library::SortOrder::TitleAsc, nullptr, 1, rows));
  EXPECT_FALSE(index.rowsForPaths(library::SortOrder::TitleAsc, &path, 1, nullptr));
  EXPECT_FALSE(index.rowsForPaths(static_cast<library::SortOrder>(255), &path, 1, rows));
  for (std::string_view invalid : {std::string_view(), std::string_view("relative.epub"), std::string_view("/books/"),
                                   std::string_view("/a\0b", 4)}) {
    path.path = invalid;
    EXPECT_FALSE(index.rowsForPaths(library::SortOrder::TitleAsc, &path, 1, rows));
  }
}

TEST(LibraryIndexFile, RefreshAnchor4096BookReadBudgetUsesSizeHintsAndBoundedChunks) {
  for (bool equalSizes : {false, true}) {
    Storage.setFile("/library.clx", pathIndexBytes(4096, equalSizes));
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    const library::PathIdentity paths[] = {{"/books/book-4094.epub", 4095, equalSizes ? 100u : 4194u},
                                           {"/books/book-4095.epub", 4096, equalSizes ? 100u : 4195u}};
    uint16_t rows[2]{};
    HalFile::resetIoCounters();
    ASSERT_TRUE(index.rowsForPaths(library::SortOrder::AuthorAsc, paths, 2, rows));
    EXPECT_EQ(rows[0], 1);
    EXPECT_EQ(rows[1], 0);
    EXPECT_LE(HalFile::readCalls, equalSizes ? 4240u : 144u);
    EXPECT_LE(HalFile::bytesRead, 570000u);
    RecordProperty(equalSizes ? "same_size_reads" : "distinct_size_reads", static_cast<int>(HalFile::readCalls));
    RecordProperty(equalSizes ? "same_size_bytes" : "distinct_size_bytes", static_cast<int>(HalFile::bytesRead));
  }
}

namespace {
library::ClixHeader storeSortedRecords(uint16_t count, uint16_t salt = 0, int invalidRow = -1) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = count;
  library::layoutSections(header, 0, 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  for (uint16_t row = 0; row < count; ++row) {
    library::ClixRecord record{};
    record.firstSeen = row;
    std::memcpy(bytes.data() + library::recordOffset(header, row), &record, sizeof(record));
    for (unsigned table = 0; table < 3; ++table) {
      uint16_t ordinal = static_cast<uint16_t>((row * 73u + table * 7u + salt) % count);
      if (table == 0 && row == invalidRow) ordinal = count;
      const uint32_t offset = header.permStart + (table * count + row) * sizeof(ordinal);
      std::memcpy(bytes.data() + offset, &ordinal, sizeof(ordinal));
    }
  }
  Storage.setFile("/library.clx", std::move(bytes));
  return header;
}
}  // namespace

TEST(LibraryIndexFile, StoredOrderScansBatchRanksWithoutReadingExtraBytes) {
  constexpr uint16_t count = library::CLIX_MAX_RECORDS;
  storeSortedRecords(count);
  const library::SortOrder orders[] = {library::SortOrder::AuthorAsc, library::SortOrder::AuthorDesc,
                                       library::SortOrder::AddedAsc,  library::SortOrder::AddedDesc,
                                       library::SortOrder::SeriesAsc, library::SortOrder::SeriesDesc};
  for (unsigned order = 0; order < 6; ++order) {
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    HalFile::resetIoCounters();
    for (uint16_t row = 0; row < count; ++row) {
      const auto physical = order % 2 ? count - 1 - row : row;
      const auto expected = (physical * 73u + (order / 2) * 7u) % count;
      const auto ordinal = index.ordinalForRow(orders[order], row);
      ASSERT_EQ(ordinal, expected) << order << ':' << row;
      library::ClixRecord record{};
      ASSERT_TRUE(index.readRecord(ordinal, record));
      ASSERT_EQ(record.firstSeen, expected);
    }
    EXPECT_EQ(HalFile::readCalls, 4224u) << order;
    EXPECT_EQ(HalFile::bytesRead, count * (sizeof(library::ClixRecord) + sizeof(uint16_t))) << order;
    EXPECT_FALSE(index.ioFailed());
    RecordProperty("order_" + std::to_string(order) + "_reads", static_cast<int>(HalFile::readCalls));
  }
}

TEST(LibraryIndexFile, SortWindowSharesDirectionsAndBoundsFinalBatch) {
  storeSortedRecords(37);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  HalFile::resetIoCounters();
  for (uint16_t row = 0; row < 32; ++row) {
    EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, row), row * 73u % 37u);
    EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorDesc, 36 - row), row * 73u % 37u);
  }
  EXPECT_EQ(HalFile::readCalls, 1u);
  EXPECT_EQ(HalFile::bytesRead, 64u);
  for (uint16_t row = 32; row < 37; ++row)
    EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, row), row * 73u % 37u);
  EXPECT_EQ(HalFile::readCalls, 2u);
  EXPECT_EQ(HalFile::bytesRead, 74u);
}

TEST(LibraryIndexFile, SortWindowCannotAliasDifferentOrderTables) {
  storeSortedRecords(37);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  HalFile::resetIoCounters();
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AddedAsc, 0), 7u);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 0), 14u);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u);
  EXPECT_EQ(HalFile::readCalls, 4u);
}

TEST(LibraryIndexFile, CloseAndFailedOpenDiscardSortWindow) {
  storeSortedRecords(37);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u);
  index.close();
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0xffffu);
  storeSortedRecords(37, 9);
  EXPECT_FALSE(index.open("/missing.clx"));
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0xffffu);
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 9u);
  EXPECT_FALSE(index.ioFailed());
}

TEST(LibraryIndexFile, FailedOrShortSortFillDiscardsOldAndPartialEntries) {
  for (int failure = 0; failure < 3; ++failure) {
    storeSortedRecords(37);
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    ASSERT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u);
    library::ClixRecord interleaved{};
    ASSERT_TRUE(index.readRecord(0, interleaved));
    if (failure == 0)
      HalFile::failRead = true;
    else if (failure == 1)
      HalFile::nextReadLimit = 9;
    else
      HalFile::failSeek = true;
    EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 32), 0xffffu) << failure;
    EXPECT_TRUE(index.ioFailed()) << failure;
    // A new failed read must be observed even when returning to the preceding window.
    HalFile::failRead = true;
    EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0xffffu) << failure;
    EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u) << failure;
    EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 32), 32u * 73u % 37u) << failure;
  }
}

TEST(LibraryIndexFile, OtherIndexReadFailureAlsoDiscardsSortWindow) {
  storeSortedRecords(37);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  ASSERT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u);
  HalFile::failRead = true;
  library::ClixRecord record{};
  EXPECT_FALSE(index.readRecord(0, record));
  HalFile::failRead = true;
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0xffffu);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u);
}

TEST(LibraryIndexFile, InvalidCachedOrdinalIsRejectedOnlyWhenSelected) {
  storeSortedRecords(37, 0, 7);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  HalFile::resetIoCounters();
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 7), 0xffffu);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 8), 8u * 73u % 37u);
  EXPECT_EQ(HalFile::readCalls, 1u);
}

TEST(LibraryIndexFile, TitleOrderAndInvalidRowsDoNotLoadOrEvictSortWindow) {
  storeSortedRecords(37);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  ASSERT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u);
  HalFile::resetIoCounters();
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::TitleAsc, 17), 17u);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::TitleDesc, 17), 19u);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 37), 0xffffu);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0xffff), 0xffffu);
  EXPECT_EQ(index.ordinalForRow(static_cast<library::SortOrder>(255), 0), 0xffffu);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0u);
  EXPECT_EQ(HalFile::readCalls, 0u);
}

namespace {
struct FolderScanFixture {
  library::ClixHeader header{};
  library::ClixRecord record{};
  std::vector<uint8_t> bytes;
  std::vector<std::string> folders;
  std::vector<uint32_t> offsets;
};
FolderScanFixture folderScanFixture(uint16_t count, bool varied = false) {
  FolderScanFixture fixture;
  auto& header = fixture.header;
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = header.folderCount = count;
  fixture.folders.reserve(count);
  fixture.offsets.reserve(count);
  uint32_t folderBytes = 0;
  static constexpr size_t LENGTHS[] = {1, 62, 63, 64, 65, 127, 128, 254, 255};
  for (uint16_t i = 0; i < count; ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "/dir%04u", i);
    std::string path = name;
    if (varied) {
      const size_t length = LENGTHS[i % 9];
      path = length > 8 ? "/Café" : "/";
      path.append(length - path.size(), 'x');
    }
    fixture.offsets.push_back(library::CLIX_ALIGN + folderBytes);
    folderBytes += 1 + path.size();
    fixture.folders.push_back(std::move(path));
  }
  constexpr char NAME[] = "book.epub";
  library::layoutSections(header, folderBytes, sizeof(uint64_t) + sizeof(NAME) - 1);
  fixture.bytes.resize(header.selfSize);
  std::memcpy(fixture.bytes.data(), &header, sizeof(header));
  for (uint16_t i = 0; i < count; ++i) {
    auto at = fixture.offsets[i];
    fixture.bytes[at++] = fixture.folders[i].size();
    std::memcpy(fixture.bytes.data() + at, fixture.folders[i].data(), fixture.folders[i].size());
  }
  std::memcpy(fixture.bytes.data() + header.nameStart + sizeof(uint64_t), NAME, sizeof(NAME) - 1);
  fixture.record.folderId = count - 1;
  fixture.record.nameLen = sizeof(NAME) - 1;
  return fixture;
}
}  // namespace

TEST(LibraryIndexFile, FolderScanBatches4096LengthReads) {
  auto fixture = folderScanFixture(4096);
  Storage.setFile("/library.clx", fixture.bytes);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  HalFile::resetIoCounters();
  std::string path;
  ASSERT_TRUE(index.readPath(fixture.record, path));
  EXPECT_EQ(path, fixture.folders.back() + "/book.epub");
  EXPECT_LE(HalFile::readCalls, 600u);
  EXPECT_LE(HalFile::seekCalls, 600u);
  std::printf("FOLDER_SCAN_4096 reads=%zu seeks=%zu bytes=%zu\n", HalFile::readCalls, HalFile::seekCalls,
              HalFile::bytesRead);
}

TEST(LibraryIndexFile, FolderScanPreservesBoundaryAndUtf8PathsWithoutCrossSectionReadAhead) {
  auto fixture = folderScanFixture(73, true);
  Storage.setFile("/library.clx", fixture.bytes);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  for (uint16_t i = 0; i < fixture.header.folderCount; ++i) {
    fixture.record.folderId = i;
    HalFile::resetIoCounters();
    HalFile::captureReads = true;
    std::string path;
    ASSERT_TRUE(index.readPath(fixture.record, path));
    HalFile::captureReads = false;
    EXPECT_EQ(path, fixture.folders[i] + "/book.epub") << i;
    for (const auto& [offset, length] : HalFile::readRanges) {
      if (offset >= fixture.header.folderStart && offset < fixture.header.folderStart + fixture.header.folderLen) {
        EXPECT_LE(offset + length, fixture.header.folderStart + fixture.header.folderLen);
        if (offset != fixture.offsets[i] + 1) {
          EXPECT_LE(length, 64u);
          EXPECT_LE(offset % library::CLIX_ALIGN + length, library::CLIX_ALIGN);
        }
      } else {
        EXPECT_EQ(offset, fixture.header.nameStart + sizeof(uint64_t));
        EXPECT_EQ(length, fixture.record.nameLen);
      }
    }
  }
}

TEST(LibraryIndexFile, FolderScanEveryReadShortReadAndSeekFailureClearsPathAndRecovers) {
  auto fixture = folderScanFixture(73, true);
  Storage.setFile("/library.clx", fixture.bytes);
  size_t reads, seeks;
  {
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    HalFile::resetIoCounters();
    std::string path;
    ASSERT_TRUE(index.readPath(fixture.record, path));
    reads = HalFile::readCalls;
    seeks = HalFile::seekCalls;
  }
  for (int fault = 0; fault < 3; ++fault) {
    for (size_t call = 0; call < (fault == 2 ? seeks : reads); ++call) {
      SCOPED_TRACE(testing::Message() << fault << ":" << call);
      HalFile::resetFaults();
      library::LibraryIndexFile index;
      ASSERT_TRUE(index.open("/library.clx"));
      if (fault == 0) HalFile::failReadAfter = call;
      if (fault == 1) HalFile::shortReadAfter = call;
      if (fault == 2) HalFile::failSeekAfter = call;
      std::string path = "stale";
      EXPECT_FALSE(index.readPath(fixture.record, path));
      EXPECT_TRUE(path.empty());
      EXPECT_TRUE(index.ioFailed());
      HalFile::resetFaults();
      ASSERT_TRUE(index.readPath(fixture.record, path));
      EXPECT_EQ(path, fixture.folders.back() + "/book.epub");
      EXPECT_TRUE(index.ioFailed());
    }
  }
  HalFile::resetFaults();
}

TEST(LibraryIndexFile, FolderScanRejectsMalformedLengthsAndInvalidFolderOrName) {
  const auto original = folderScanFixture(12);
  for (int fault = 0; fault < 5; ++fault) {
    auto fixture = original;
    if (fault == 0) fixture.bytes[fixture.offsets[5]] = 0;
    if (fault == 1) fixture.bytes[fixture.offsets.back()] = 255;
    if (fault == 2) {
      --fixture.header.folderLen;
      std::memcpy(fixture.bytes.data(), &fixture.header, sizeof(fixture.header));
    }
    if (fault == 3) fixture.record.folderId = fixture.header.folderCount;
    if (fault == 4) fixture.record.nameOff = UINT32_MAX;
    Storage.setFile("/library.clx", fixture.bytes);
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    std::string path = "stale";
    EXPECT_FALSE(index.readPath(fixture.record, path)) << fault;
    EXPECT_TRUE(path.empty());
  }
}

TEST(LibraryIndexFile, FolderScanDoesNotRetainWindowAcrossCallsOrReopen) {
  auto first = folderScanFixture(30);
  auto second = folderScanFixture(30, true);
  Storage.setFile("/library.clx", first.bytes);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  std::string path;
  ASSERT_TRUE(index.readPath(first.record, path));
  EXPECT_EQ(path, first.folders.back() + "/book.epub");
  // The next access must actually read again, including after a successful scan.
  HalFile::failRead = true;
  EXPECT_FALSE(index.readPath(first.record, path));
  EXPECT_TRUE(path.empty());
  index.close();
  Storage.setFile("/library.clx", second.bytes);
  ASSERT_TRUE(index.open("/library.clx"));
  ASSERT_TRUE(index.readPath(second.record, path));
  EXPECT_EQ(path, second.folders.back() + "/book.epub");
}

TEST(LibraryIndexFile, AuthorContractEmptyFieldIsReadableWithoutExtraIo) {
  storeMetadata("", "Title", "");
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  record.nameLen = 1;
  std::string author = "stale";
  HalFile::resetIoCounters();
  EXPECT_TRUE(index.readAuthor(record, author));
  EXPECT_TRUE(author.empty());
  EXPECT_FALSE(index.ioFailed());
  EXPECT_EQ(HalFile::readCalls, 1u);
  EXPECT_EQ(HalFile::bytesRead, 1u);
}

TEST(LibraryIndexFile, AuthorContractEmptyAuthorAndAbsentTitleRemainDistinct) {
  storeMetadata("", "", "");
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  record.nameLen = 1;
  std::string value = "stale";
  EXPECT_TRUE(index.readAuthor(record, value));
  EXPECT_TRUE(value.empty());
  EXPECT_FALSE(index.readTitle(record, value));
  EXPECT_TRUE(value.empty());
  EXPECT_TRUE(index.readSourceAuthor(record, value));
  EXPECT_TRUE(value.empty());
  EXPECT_FALSE(index.ioFailed());
}

TEST(LibraryIndexFile, AuthorContractMalformedOrMissingDataStillFails) {
  for (int fault = 0; fault < 5; ++fault) {
    const auto header = storeMetadata("Author", "Title", "Source");
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    library::ClixRecord record{};
    record.nameLen = 1;
    if (fault == 0) record.nameLen = 0;
    if (fault == 1) record.nameOff = UINT32_MAX;
    if (fault == 2) record.nameLen = UINT8_MAX;
    if (fault == 3) Storage.truncateFile(header.nameStart + sizeof(uint64_t) + 2);
    if (fault == 4) index.close();
    std::string author;
    EXPECT_FALSE(index.readAuthor(record, author)) << fault;
  }
}

TEST(LibraryIndexFile, AuthorContractReadAndSeekFailuresRemainErrors) {
  for (int fault = 0; fault < 5; ++fault) {
    storeMetadata("Author", "Title", "Source");
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    library::ClixRecord record{};
    record.nameLen = 1;
    HalFile::resetFaults();
    if (fault < 2)
      HalFile::failReadAfter = fault;
    else if (fault < 4)
      HalFile::shortReadAfter = fault - 2;
    else
      HalFile::failSeekAfter = 0;
    std::string author;
    EXPECT_FALSE(index.readAuthor(record, author)) << fault;
    EXPECT_TRUE(index.ioFailed());
    HalFile::resetFaults();
    ASSERT_TRUE(index.open("/library.clx"));
    ASSERT_TRUE(index.readAuthor(record, author));
    EXPECT_EQ(author, "Author");
    EXPECT_FALSE(index.ioFailed());
  }
}

TEST(LibraryIndexFile, MalformedAuthorBoundsFailWithoutTransportError) {
  for (int fault = 0; fault < 4; ++fault) {
    library::ClixHeader header{};
    std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
    header.formatVersion = library::CLIX_FORMAT_VERSION;
    header.foldVersion = library::CLIX_FOLD_VERSION;
    header.bookCount = 1;
    const auto blob = fault == 0 ? makeBlob(1, {'x'}) : makeBlob(1, {'x', 255});
    library::layoutSections(header, 0, blob.size());
    std::vector<uint8_t> bytes(header.selfSize, 0);
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
    Storage.setFile("/library.clx", std::move(bytes));
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    library::ClixRecord record{};
    record.nameLen = fault == 2 ? 0 : 1;
    if (fault == 3) record.nameOff = UINT32_MAX;
    std::string author = "stale";
    HalFile::resetIoCounters();
    EXPECT_FALSE(index.readAuthor(record, author)) << fault;
    EXPECT_TRUE(author.empty()) << fault;
    EXPECT_FALSE(index.ioFailed()) << fault;
    EXPECT_EQ(HalFile::readCalls, fault == 1 ? 1u : 0u) << fault;
  }
}

TEST(LibraryIndexFile, CombinedMetadataPreservesFieldsAcrossBufferBoundaries) {
  size_t reads[2] = {}, bytes[2] = {}, seeks[2] = {};
  constexpr size_t lengths[] = {0, 1, 63, 64, 65, 127, 128, 254, 255};
  for (const size_t authorLength : lengths) {
    for (const size_t titleLength : lengths) {
      for (const size_t sourceLength : lengths) {
        const std::string canonical(authorLength, 'a');
        const std::string expectedTitle(titleLength, 't');
        const std::string original(sourceLength, 's');
        const auto header = storeMetadata(canonical, expectedTitle, original);
        library::LibraryIndexFile index;
        ASSERT_TRUE(index.open("/library.clx"));
        library::ClixRecord record{};
        record.nameLen = 1;
        std::string title, author;
        for (const bool source : {false, true}) {
          SCOPED_TRACE(::testing::Message()
                       << authorLength << "/" << titleLength << "/" << sourceLength << " source=" << source);
          HalFile::resetIoCounters();
          ASSERT_TRUE(source ? index.readTitleAndSourceAuthor(record, title, author)
                             : index.readTitleAndAuthor(record, title, author));
          ASSERT_EQ(title, expectedTitle);
          ASSERT_EQ(author, source ? original : canonical);
          ASSERT_LE(HalFile::bytesRead, header.nameLen - sizeof(uint64_t) - record.nameLen);
          reads[source] += HalFile::readCalls;
          bytes[source] += HalFile::bytesRead;
          seeks[source] += HalFile::seekCalls;
        }
      }
    }
  }
  for (int source = 0; source < 2; ++source) {
    std::printf("METADATA_BOUNDARIES cases=729 source=%d reads=%zu seeks=%zu bytes=%zu\n", source, reads[source],
                seeks[source], bytes[source]);
  }
  EXPECT_EQ(reads[0], 2142u);
  EXPECT_EQ(reads[1], 2711u);
  EXPECT_EQ(seeks[0], 729u);
  EXPECT_EQ(seeks[1], 1215u);
  EXPECT_EQ(bytes[0], 173944u);
  EXPECT_EQ(bytes[1], 193023u);
}
