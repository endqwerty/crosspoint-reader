#include <BookMetadataCache.h>
#include <Serialization.h>
#include <gtest/gtest.h>

#include "ReaderContract.h"
#include "ScopedAllocationFailure.h"

class BookMetadataCacheTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(cache_test::handles, 0);
    cache_test::files.clear();
    cache_test::resetFaults();
  }
  void TearDown() override { EXPECT_EQ(cache_test::handles, 0); }
  static void makeBook(int chapters = 3, const std::string& tocTitle = "First chapter", bool includeToc = true) {
    BookMetadataCache writer("/book");
    ASSERT_TRUE(writer.beginWrite());
    ASSERT_TRUE(writer.beginContentOpfPass());
    for (int i = 0; i < chapters; ++i) writer.createSpineEntry("chapter" + std::to_string(i) + ".xhtml");
    ASSERT_TRUE(writer.endContentOpfPass());
    ASSERT_TRUE(writer.beginTocPass());
    if (includeToc) writer.createTocEntry(tocTitle, "chapter0.xhtml", "anchor", 0);
    ASSERT_TRUE(writer.endTocPass());
    ASSERT_TRUE(writer.endWrite());
    ASSERT_TRUE(writer.buildBookBin("/book.epub", {"Title", "Author", "en", "cover.jpg", "chapter0.xhtml"}));
  }
};
TEST_F(BookMetadataCacheTest, ExistingWriterRoundTripsWithoutChangingFormat) {
  makeBook();
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
  EXPECT_EQ(reader.coreMetadata.title, "Title");
  EXPECT_EQ(reader.getSpineCount(), 3);
  EXPECT_EQ(reader.getTocCount(), 1);
  EXPECT_EQ(reader.getSpineEntry(2).href, "chapter2.xhtml");
  EXPECT_EQ(reader.getCumulativeSize(2), 300u);
  EXPECT_EQ(reader.getTocEntry(0).anchor, "anchor");
  EXPECT_EQ(cache_test::files["/book/book.bin"][0], 10);
}
TEST_F(BookMetadataCacheTest, TruncatedMetadataPayloadCannotPublishLoadedCache) {
  HalFile out;
  ASSERT_TRUE(out.open("/book/book.bin", true));
  serialization::writePod(out, uint8_t{10});
  serialization::writePod(out, uint32_t{29});
  serialization::writePod(out, uint16_t{0});
  serialization::writePod(out, uint16_t{0});
  for (int i = 0; i < 4; ++i) serialization::writeString(out, "");
  serialization::writePod(out, uint32_t{4});
  out.write("ab", 2);
  out.close();
  BookMetadataCache reader("/book");
  EXPECT_FALSE(reader.load());
  EXPECT_FALSE(reader.isLoaded());
}

namespace {
uint32_t readU32(const std::vector<uint8_t>& bytes, size_t at) {
  uint32_t value;
  std::memcpy(&value, bytes.data() + at, sizeof(value));
  return value;
}
template <typename T>
void put(std::vector<uint8_t>& bytes, size_t at, T value) {
  ASSERT_LE(at + sizeof(value), bytes.size());
  std::memcpy(bytes.data() + at, &value, sizeof(value));
}
}  // namespace
TEST_F(BookMetadataCacheTest, EveryTruncationFailsLoadOrBoundedEntryAccessWithoutWrites) {
  makeBook();
  const auto valid = cache_test::files["/book/book.bin"];
  for (size_t cut = 0; cut < valid.size(); ++cut) {
    SCOPED_TRACE(cut);
    cache_test::files["/book/book.bin"] = {valid.begin(), valid.begin() + cut};
    cache_test::resetFaults();
    BookMetadataCache reader("/book");
    if (reader.load()) {
      for (int i = 0; i < 3; ++i) EXPECT_FALSE(reader.getSpineEntry(i).href.empty());
      const auto entry = reader.getTocEntry(0);
      EXPECT_TRUE(entry.title.empty());
      EXPECT_TRUE(entry.href.empty());
      EXPECT_TRUE(entry.anchor.empty());
      EXPECT_EQ(entry.spineIndex, -1);
      EXPECT_TRUE(reader.isLoaded());
      EXPECT_EQ(reader.getSpineCount(), 3);
      EXPECT_EQ(reader.getCumulativeSize(2), 300u);
    } else {
      EXPECT_FALSE(reader.isLoaded());
      EXPECT_EQ(reader.getSpineCount(), 0);
      EXPECT_EQ(reader.getTocCount(), 0);
      EXPECT_TRUE(reader.coreMetadata.title.empty());
      EXPECT_EQ(reader.getCumulativeSize(0), 0u);
      EXPECT_EQ(cache_test::handles, 0);
    }
    EXPECT_EQ(cache_test::writes, 0);
  }
}
TEST_F(BookMetadataCacheTest, HeaderOffsetsCountsAndMetadataLengthsCannotEscapeFile) {
  makeBook();
  const auto valid = cache_test::files["/book/book.bin"];
  for (int fault = 0; fault < 8; ++fault) {
    SCOPED_TRACE(fault);
    auto bytes = valid;
    switch (fault) {
      case 0:
        bytes[0] = 9;
        break;
      case 1:
        put(bytes, 1, UINT32_MAX);
        break;
      case 2:
        put(bytes, 1, uint32_t{8});
        break;
      case 3:
        put(bytes, 5, UINT16_MAX);
        break;
      case 4:
        put(bytes, 7, UINT16_MAX);
        break;
      case 5:
        put(bytes, 9, UINT32_MAX);
        break;
      case 6:
        put(bytes, 9, uint32_t{4097});
        break;
      case 7:
        bytes[13] = 0;
        break;
    }
    cache_test::files["/book/book.bin"] = bytes;
    BookMetadataCache reader("/book");
    EXPECT_FALSE(reader.load());
    EXPECT_FALSE(reader.isLoaded());
    EXPECT_EQ(cache_test::files["/book/book.bin"], bytes);
  }
}
TEST_F(BookMetadataCacheTest, SpineLutBoundsPayloadSizesAndCumulativeOrderingAreValidated) {
  makeBook();
  const auto valid = cache_test::files["/book/book.bin"];
  const uint32_t lut = readU32(valid, 1);
  const uint32_t second = readU32(valid, lut + 4);
  for (int fault = 0; fault < 7; ++fault) {
    SCOPED_TRACE(fault);
    auto bytes = valid;
    switch (fault) {
      case 0:
        put(bytes, lut, uint32_t{0});
        break;
      case 1:
        put(bytes, lut + 4, readU32(valid, lut));
        break;
      case 2:
        put(bytes, lut + 12, UINT32_MAX);
        break;
      case 3:
        put(bytes, second, UINT32_MAX);
        break;
      case 4:
        put(bytes, second, uint32_t{0});
        break;
      case 5:
        put(bytes, second + 4 + readU32(valid, second), uint32_t{99});
        break;
      case 6:
        put(bytes, second + 8 + readU32(valid, second), int16_t{1});
        break;
    }
    cache_test::files["/book/book.bin"] = bytes;
    BookMetadataCache reader("/book");
    EXPECT_FALSE(reader.load());
    EXPECT_EQ(reader.getCumulativeSize(0), 0u);
  }
}
TEST_F(BookMetadataCacheTest, EveryLoadReadAndSeekFailureDiscardsAllPublishedState) {
  makeBook(1000);
  int reads = 0, seeks = 0;
  {
    BookMetadataCache reader("/book");
    cache_test::resetFaults();
    ASSERT_TRUE(reader.load());
    reads = cache_test::reads;
    seeks = cache_test::seeks;
  }
  for (bool read : {false, true}) {
    for (int call = 0; call < (read ? reads : seeks); ++call) {
      SCOPED_TRACE(read);
      SCOPED_TRACE(call);
      BookMetadataCache reader("/book");
      cache_test::resetFaults();
      if (read)
        cache_test::failRead = call;
      else
        cache_test::failSeek = call;
      EXPECT_FALSE(reader.load());
      EXPECT_FALSE(reader.isLoaded());
      EXPECT_EQ(reader.getSpineCount(), 0);
      EXPECT_TRUE(reader.coreMetadata.title.empty());
      EXPECT_EQ(cache_test::handles, 0);
    }
  }
  cache_test::resetFaults();
}
TEST_F(BookMetadataCacheTest, FailedReloadClearsOldMetadataAndCanRecover) {
  makeBook();
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
  cache_test::failOpen = true;
  EXPECT_FALSE(reader.load());
  EXPECT_FALSE(reader.isLoaded());
  EXPECT_EQ(reader.getCumulativeSize(2), 0u);
  EXPECT_TRUE(reader.coreMetadata.title.empty());
  EXPECT_EQ(cache_test::handles, 0);
  cache_test::resetFaults();
  EXPECT_TRUE(reader.load());
  EXPECT_EQ(reader.getCumulativeSize(2), 300u);
}
TEST_F(BookMetadataCacheTest, ChapterSizeAllocationFailureIsRecoverable) {
  makeBook();
  BookMetadataCache reader("/book");
  {
    parser_test::ScopedAllocationFailure fail(parser_test::ScopedAllocationFailure::Kind::Array, 3 * sizeof(uint32_t));
    EXPECT_FALSE(reader.load());
    EXPECT_EQ(fail.failures(), 1u);
  }
  EXPECT_FALSE(reader.isLoaded());
  EXPECT_EQ(cache_test::handles, 0);
  EXPECT_TRUE(reader.load());
}
TEST_F(BookMetadataCacheTest, OptionalBufferAllocationFailureUsesCheckedUnbufferedReads) {
  makeBook();
  BookMetadataCache reader("/book");
  parser_test::ScopedAllocationFailure fail(parser_test::ScopedAllocationFailure::Kind::Array, 4096);
  ASSERT_TRUE(reader.load());
  EXPECT_EQ(fail.failures(), 1u);
  EXPECT_EQ(reader.getCumulativeSize(2), 300u);
  EXPECT_EQ(reader.getSpineEntry(2).href, "chapter2.xhtml");
}
TEST_F(BookMetadataCacheTest, ThousandChapterLoadBatchesReadsAndProgressQueriesDoNoIo) {
  makeBook(1000);
  BookMetadataCache reader("/book");
  cache_test::resetFaults();
  ASSERT_TRUE(reader.load());
  RecordProperty("load_hal_reads", cache_test::reads);
  RecordProperty("load_hal_seeks", cache_test::seeks);
  EXPECT_LT(cache_test::reads, 40);
  EXPECT_LT(cache_test::seeks, 20);
  cache_test::resetFaults();
  for (int i = 0; i < 1000; ++i) EXPECT_EQ(reader.getCumulativeSize(i), static_cast<uint32_t>((i + 1) * 100));
  EXPECT_EQ(reader.getCumulativeSize(-1), 0u);
  EXPECT_EQ(reader.getCumulativeSize(1000), 0u);
  EXPECT_EQ(cache_test::reads, 0);
  EXPECT_EQ(cache_test::seeks, 0);
}
TEST_F(BookMetadataCacheTest, EntryFaultsReturnEmptyAndPreserveValidatedBookStateForRetry) {
  makeBook();
  for (bool toc : {false, true}) {
    int reads = 0, seeks = 0;
    {
      BookMetadataCache reader("/book");
      ASSERT_TRUE(reader.load());
      cache_test::resetFaults();
      if (toc)
        reader.getTocEntry(0);
      else
        reader.getSpineEntry(2);
      reads = cache_test::reads;
      seeks = cache_test::seeks;
    }
    for (bool read : {false, true}) {
      for (int call = 0; call < (read ? reads : seeks); ++call) {
        SCOPED_TRACE(toc);
        SCOPED_TRACE(read);
        SCOPED_TRACE(call);
        cache_test::resetFaults();
        BookMetadataCache reader("/book");
        ASSERT_TRUE(reader.load());
        cache_test::resetFaults();
        if (read)
          cache_test::failRead = call;
        else
          cache_test::failSeek = call;
        if (toc)
          EXPECT_TRUE(reader.getTocEntry(0).href.empty());
        else
          EXPECT_TRUE(reader.getSpineEntry(2).href.empty());
        EXPECT_TRUE(reader.isLoaded());
        EXPECT_EQ(reader.getSpineCount(), 3);
        EXPECT_EQ(reader.getTocCount(), 1);
        EXPECT_EQ(reader.getCumulativeSize(2), 300u);
        EXPECT_EQ(reader.coreMetadata.title, "Title");
        cache_test::resetFaults();
        if (toc)
          EXPECT_EQ(reader.getTocEntry(0).anchor, "anchor");
        else
          EXPECT_EQ(reader.getSpineEntry(2).href, "chapter2.xhtml");
        EXPECT_EQ(cache_test::writes, 0);
      }
    }
  }
  cache_test::resetFaults();
}
TEST_F(BookMetadataCacheTest, OnDemandTocValidatesLengthsOffsetsAndDestination) {
  makeBook();
  const auto valid = cache_test::files["/book/book.bin"];
  const uint32_t lut = readU32(valid, 1);
  const uint32_t toc = readU32(valid, lut + 12);
  for (int fault = 0; fault < 4; ++fault) {
    cache_test::files["/book/book.bin"] = valid;
    BookMetadataCache reader("/book");
    ASSERT_TRUE(reader.load());
    auto& bytes = cache_test::files["/book/book.bin"];
    if (fault == 0) put(bytes, lut + 12, uint32_t{0});
    if (fault == 1) put(bytes, toc, UINT32_MAX);
    if (fault == 2) put(bytes, bytes.size() - 2, int16_t{3});
    if (fault == 3) bytes[toc + 4] = 0;
    const auto entry = reader.getTocEntry(0);
    EXPECT_TRUE(entry.title.empty());
    EXPECT_TRUE(entry.href.empty());
    EXPECT_TRUE(entry.anchor.empty());
    EXPECT_EQ(entry.spineIndex, -1);
    EXPECT_TRUE(reader.isLoaded());
    EXPECT_EQ(reader.getSpineCount(), 3);
    EXPECT_EQ(reader.getCumulativeSize(2), 300u);
  }
}

TEST_F(BookMetadataCacheTest, LongTocCaptionTruncatesOnUtf8BoundaryWithoutLosingDestination) {
  makeBook(3, std::string(4095, 'a') + "é beyond the display bound");
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
  auto entry = reader.getTocEntry(0);
  EXPECT_EQ(entry.title, std::string(4095, 'a'));
  EXPECT_EQ(entry.href, "chapter0.xhtml");
  EXPECT_EQ(entry.anchor, "anchor");
  EXPECT_EQ(entry.spineIndex, 0);
  EXPECT_TRUE(reader.isLoaded());
}
TEST_F(BookMetadataCacheTest, StartingRebuildRetiresLoadedMetadataAndChapterSizes) {
  makeBook();
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
  ASSERT_TRUE(reader.beginWrite());
  EXPECT_FALSE(reader.isLoaded());
  EXPECT_EQ(reader.getSpineCount(), 0);
  EXPECT_EQ(reader.getCumulativeSize(2), 0u);
  EXPECT_EQ(cache_test::handles, 0);
}
TEST_F(BookMetadataCacheTest, EmptySpineAndUnknownTocDestinationRemainValid) {
  makeBook(0);
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
  EXPECT_EQ(reader.getSpineCount(), 0);
  EXPECT_EQ(reader.getCumulativeSize(0), 0u);
  EXPECT_EQ(reader.getTocEntry(0).spineIndex, -1);
  EXPECT_TRUE(reader.isLoaded());
}

TEST_F(BookMetadataCacheTest, BooksWithoutTocKeepChapterSizesAndUnknownTocIndices) {
  makeBook(3, "", false);
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
  EXPECT_EQ(reader.getTocCount(), 0);
  EXPECT_EQ(reader.getSpineEntry(2).tocIndex, -1);
  EXPECT_EQ(reader.getCumulativeSize(2), 300u);
}
TEST_F(BookMetadataCacheTest, OutOfRangeRequestsDoNotInvalidateAHealthyCache) {
  makeBook();
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
  EXPECT_TRUE(reader.getSpineEntry(-1).href.empty());
  EXPECT_TRUE(reader.getSpineEntry(3).href.empty());
  EXPECT_TRUE(reader.getTocEntry(-1).href.empty());
  EXPECT_TRUE(reader.getTocEntry(1).href.empty());
  EXPECT_TRUE(reader.isLoaded());
  EXPECT_EQ(reader.getCumulativeSize(2), 300u);
}

TEST_F(BookMetadataCacheTest, EntryFailureCannotTurnCurrentChapterIntoEndOfBook) {
  makeBook();
  auto cache = std::make_shared<BookMetadataCache>("/book");
  ASSERT_TRUE(cache->load());
  Epub epub{cache};
  EpubReaderActivity reader{&epub, 1};
  ASSERT_FALSE(reader.isAtEndOfBook());
  for (bool toc : {false, true}) {
    cache_test::resetFaults();
    cache_test::failRead = 0;
    if (toc)
      EXPECT_TRUE(cache->getTocEntry(0).href.empty());
    else
      EXPECT_TRUE(cache->getSpineEntry(1).href.empty());
    EXPECT_FALSE(reader.isAtEndOfBook());
    EXPECT_EQ(epub.getSpineItemsCount(), 3);
    EXPECT_EQ(epub.getCumulativeSpineItemSize(2), 300u);
    EXPECT_EQ(reader.currentSpineIndex, 1);
    cache_test::resetFaults();
    EXPECT_EQ(cache->getSpineEntry(1).href, "chapter1.xhtml");
  }
  reader.currentSpineIndex = 3;
  EXPECT_TRUE(reader.isAtEndOfBook());
}

class MetadataBuildFault : public BookMetadataCacheTest {
 protected:
  static void stageSpines(BookMetadataCache& writer, int count) {
    ASSERT_TRUE(writer.beginWrite());
    ASSERT_TRUE(writer.beginContentOpfPass());
    for (int i = 0; i < count; ++i) writer.createSpineEntry("chapter" + std::to_string(i) + ".xhtml");
    ASSERT_TRUE(writer.endContentOpfPass());
  }
  static void stage(BookMetadataCache& writer, int count = 3, const std::string& first = "First") {
    stageSpines(writer, count);
    ASSERT_TRUE(writer.beginTocPass());
    for (int i = 0; i < count; ++i)
      writer.createTocEntry(i ? "Next" : first, "chapter" + std::to_string(i) + ".xhtml", "anchor", 0);
    ASSERT_TRUE(writer.endTocPass());
    ASSERT_TRUE(writer.endWrite());
    cache_test::resetFaults();
  }
  static bool build(BookMetadataCache& writer) {
    return writer.buildBookBin("/book.epub", {"Title", "Author", "en", "cover.jpg", "chapter0.xhtml"});
  }
};

TEST_F(MetadataBuildFault, RejectsTrailingBytesInEitherStagingFile) {
  for (const char* path : {"/book/spine.bin.tmp", "/book/toc.bin.tmp"}) {
    BookMetadataCache writer("/book");
    stage(writer);
    cache_test::files[path].push_back(0);
    EXPECT_FALSE(build(writer));
    EXPECT_FALSE(cache_test::files.contains("/book/book.bin"));
    EXPECT_EQ(0, cache_test::handles);
  }
}

TEST_F(MetadataBuildFault, BoundsAssembledCaptionsAndKeepsEveryDestinationOffset) {
  BookMetadataCache writer("/book");
  stage(writer, 3, std::string(4095, 'a') + "é" + std::string(8192, 'z'));
  ASSERT_TRUE(build(writer));
  EXPECT_LT(cache_test::files["/book/book.bin"].size(), 5000u);
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
  EXPECT_EQ(std::string(4095, 'a'), reader.getTocEntry(0).title);
  for (int i = 0; i < 3; ++i) {
    const auto entry = reader.getTocEntry(i);
    EXPECT_EQ("chapter" + std::to_string(i) + ".xhtml", entry.href);
    EXPECT_EQ(i, entry.spineIndex);
    EXPECT_EQ("anchor", entry.anchor);
    if (i) EXPECT_EQ("Next", entry.title);
  }
}

TEST_F(MetadataBuildFault, EveryAssemblyReadSeekAndWriteFailureDiscardsOutput) {
  for (int chapters : {3, 400}) {
    int reads, seeks, writes;
    {
      BookMetadataCache writer("/book");
      stage(writer, chapters);
      ASSERT_TRUE(build(writer));
      reads = cache_test::reads;
      seeks = cache_test::seeks;
      writes = cache_test::writes;
    }
    for (int operation = 0; operation < 3; ++operation) {
      const int count = operation == 0 ? reads : operation == 1 ? seeks : writes;
      for (int call = 0; call < count; ++call) {
        cache_test::files.erase("/book/book.bin");
        BookMetadataCache writer("/book");
        stage(writer, chapters);
        if (operation == 0) cache_test::failRead = call;
        if (operation == 1) cache_test::failSeek = call;
        if (operation == 2) cache_test::failWrite = call;
        EXPECT_FALSE(build(writer)) << chapters << ':' << operation << ':' << call;
        EXPECT_FALSE(cache_test::files.contains("/book/book.bin"));
        EXPECT_EQ(0, cache_test::handles);
        cache_test::resetFaults();
      }
    }
  }
}

TEST_F(MetadataBuildFault, PartialUnderlyingReadsEitherCompleteExactlyOrDiscardOutput) {
  std::vector<uint8_t> expected;
  int calls;
  {
    BookMetadataCache writer("/book");
    stage(writer);
    ASSERT_TRUE(build(writer));
    expected = cache_test::files["/book/book.bin"];
    calls = cache_test::reads;
  }
  for (int call = 0; call < calls; ++call) {
    cache_test::files.erase("/book/book.bin");
    BookMetadataCache writer("/book");
    stage(writer);
    cache_test::shortRead = call;
    if (build(writer))
      EXPECT_EQ(expected, cache_test::files["/book/book.bin"]);
    else
      EXPECT_FALSE(cache_test::files.contains("/book/book.bin"));
    EXPECT_EQ(1, cache_test::injectedReads);
    cache_test::resetFaults();
  }
}

TEST_F(MetadataBuildFault, RawSpineReadsFailClosedForSmallAndIndexedTocPasses) {
  for (int chapters : {3, 400}) {
    for (int fault : {0, 1, 2}) {
      BookMetadataCache writer("/book");
      stageSpines(writer, chapters);
      cache_test::resetFaults();
      if (chapters == 3) ASSERT_TRUE(writer.beginTocPass());
      cache_test::resetFaults();
      if (fault == 0) cache_test::failRead = 0;
      if (fault == 1) cache_test::shortRead = 0;
      if (fault == 2) cache_test::failSeek = 0;
      if (chapters == 400)
        EXPECT_FALSE(writer.beginTocPass());
      else {
        writer.createTocEntry("First", "chapter0.xhtml", "", 0);
        EXPECT_FALSE(writer.endTocPass());
      }
      EXPECT_FALSE(writer.endWrite());
      EXPECT_FALSE(build(writer));
      EXPECT_EQ(0, cache_test::handles);
      cache_test::resetFaults();
    }
  }
}

TEST_F(MetadataBuildFault, MalformedLengthsAndTocIndicesCannotPublishCache) {
  for (int fault = 0; fault < 7; ++fault) {
    BookMetadataCache writer("/book");
    stage(writer);
    auto& spine = cache_test::files["/book/spine.bin.tmp"];
    auto& toc = cache_test::files["/book/toc.bin.tmp"];
    if (fault == 0) put(spine, 0, UINT32_MAX);
    if (fault == 1) put(toc, 0, UINT32_MAX);
    if (fault == 2) spine.resize(spine.size() - 1);
    if (fault == 3) toc.resize(toc.size() - 1);
    if (fault == 4) spine[4] = 0;
    if (fault == 5) put(toc, toc.size() - 2, int16_t{3});
    if (fault == 6) put(toc, toc.size() - 2, int16_t{-2});
    EXPECT_FALSE(build(writer)) << fault;
    EXPECT_FALSE(cache_test::files.contains("/book/book.bin"));
    EXPECT_EQ(0, cache_test::handles);
  }
}

TEST_F(MetadataBuildFault, BufferOomFallbackKeepsCheckedReadsAndWrites) {
  for (bool failRead : {false, true}) {
    cache_test::files.erase("/book/book.bin");
    BookMetadataCache writer("/book");
    stage(writer);
    if (failRead) cache_test::failRead = 1;
    parser_test::ScopedAllocationFailure oom(parser_test::ScopedAllocationFailure::Kind::Array, 4096, 1, 3);
    EXPECT_EQ(!failRead, build(writer));
    EXPECT_EQ(3u, oom.failures());
    EXPECT_EQ(!failRead, cache_test::files.contains("/book/book.bin"));
    EXPECT_EQ(0, cache_test::handles);
    cache_test::resetFaults();
  }
  BookMetadataCache writer("/book");
  ASSERT_TRUE(writer.beginWrite());
  {
    parser_test::ScopedAllocationFailure oom(parser_test::ScopedAllocationFailure::Kind::Object,
                                             sizeof(serialization::BufferedFileWriter));
    ASSERT_TRUE(writer.beginContentOpfPass());
    EXPECT_EQ(1u, oom.failures());
  }
  cache_test::resetFaults();
  cache_test::failWrite = 1;
  writer.createSpineEntry("chapter0.xhtml");
  EXPECT_FALSE(writer.endContentOpfPass());
  EXPECT_FALSE(writer.endWrite());
  EXPECT_FALSE(build(writer));
  cache_test::resetFaults();
}

TEST_F(MetadataBuildFault, RawTocWriteFailureStaysFailedUntilNewBuild) {
  BookMetadataCache writer("/book");
  stageSpines(writer, 3);
  {
    parser_test::ScopedAllocationFailure oom(parser_test::ScopedAllocationFailure::Kind::Object,
                                             sizeof(serialization::BufferedFileWriter));
    ASSERT_TRUE(writer.beginTocPass());
    EXPECT_EQ(1u, oom.failures());
  }
  cache_test::resetFaults();
  cache_test::failWrite = 1;
  writer.createTocEntry("First", "chapter0.xhtml", "anchor", 0);
  EXPECT_FALSE(writer.endTocPass());
  EXPECT_FALSE(writer.endWrite());
  EXPECT_FALSE(build(writer));
  EXPECT_EQ(1, cache_test::injectedWrites);
  EXPECT_EQ(0, cache_test::handles);
  cache_test::resetFaults();
  stage(writer);
  ASSERT_TRUE(build(writer));
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
  EXPECT_EQ(3, reader.getTocCount());
}

TEST_F(MetadataBuildFault, BufferedPassFlushFailuresCannotBeForgotten) {
  for (bool toc : {false, true}) {
    BookMetadataCache writer("/book");
    if (toc) {
      stageSpines(writer, 3);
      ASSERT_TRUE(writer.beginTocPass());
      writer.createTocEntry("First", "chapter0.xhtml", "", 0);
    } else {
      ASSERT_TRUE(writer.beginWrite());
      ASSERT_TRUE(writer.beginContentOpfPass());
      writer.createSpineEntry("chapter0.xhtml");
    }
    cache_test::resetFaults();
    cache_test::failWrite = 0;
    EXPECT_FALSE(toc ? writer.endTocPass() : writer.endContentOpfPass());
    EXPECT_FALSE(writer.endWrite());
    EXPECT_FALSE(build(writer));
    EXPECT_EQ(1, cache_test::injectedWrites);
    EXPECT_EQ(0, cache_test::handles);
    cache_test::resetFaults();
  }
}

TEST_F(MetadataBuildFault, InvalidCoreMetadataLeavesPreviousCacheUntouched) {
  for (const std::string& title : {std::string(4097, 'x'), std::string("bad\0title", 9)}) {
    BookMetadataCache writer("/book");
    stage(writer);
    ASSERT_TRUE(build(writer));
    const auto before = cache_test::files["/book/book.bin"];
    EXPECT_FALSE(writer.buildBookBin("/book.epub", {title, "Author", "en", "", ""}));
    EXPECT_EQ(before, cache_test::files["/book/book.bin"]);
    EXPECT_EQ(0, cache_test::handles);
  }
}

TEST_F(MetadataBuildFault, OversizedStagingFilesAreRejectedBeforeDecoding) {
  for (uint64_t reported : {uint64_t{UINT32_MAX}, uint64_t{UINT32_MAX} + 1}) {
    BookMetadataCache writer("/book");
    stage(writer);
    cache_test::reportedSize = reported;
    EXPECT_FALSE(build(writer));
    EXPECT_EQ(0, cache_test::reads);
    EXPECT_FALSE(cache_test::files.contains("/book/book.bin"));
    EXPECT_EQ(0, cache_test::handles);
    cache_test::resetFaults();
  }
  for (int chapters : {3, 400}) {
    BookMetadataCache writer("/book");
    stageSpines(writer, chapters);
    cache_test::resetFaults();
    cache_test::reportedSize = uint64_t{UINT32_MAX} + 1;
    if (chapters == 400)
      EXPECT_FALSE(writer.beginTocPass());
    else {
      ASSERT_TRUE(writer.beginTocPass());
      writer.createTocEntry("First", "chapter0.xhtml", "", 0);
      EXPECT_FALSE(writer.endTocPass());
    }
    EXPECT_EQ(0, cache_test::reads);
    EXPECT_FALSE(writer.endWrite());
    EXPECT_FALSE(build(writer));
    EXPECT_EQ(0, cache_test::handles);
    cache_test::resetFaults();
  }
}

TEST_F(MetadataBuildFault, PublicationRejectsFailedPassCloseAndAllowsFreshBuild) {
  for (int pass = 0; pass < 3; ++pass) {
    BookMetadataCache writer("/book");
    if (pass == 0) {
      ASSERT_TRUE(writer.beginWrite());
      ASSERT_TRUE(writer.beginContentOpfPass());
      writer.createSpineEntry("chapter0.xhtml");
    } else {
      stageSpines(writer, 3);
      ASSERT_TRUE(writer.beginTocPass());
      writer.createTocEntry("First", "chapter0.xhtml", "", 0);
    }
    cache_test::resetFaults();
    cache_test::failClose = pass == 2 ? 1 : 0;
    EXPECT_FALSE(pass == 0 ? writer.endContentOpfPass() : writer.endTocPass());
    EXPECT_FALSE(writer.endWrite());
    EXPECT_FALSE(build(writer));
    EXPECT_EQ(0, cache_test::handles);
    cache_test::resetFaults();
    stage(writer);
    ASSERT_TRUE(build(writer));
  }
}

TEST_F(MetadataBuildFault, PublicationRejectsFailedFinalCloseWithOrWithoutPreviousCache) {
  for (bool previous : {false, true}) {
    cache_test::files.clear();
    if (previous) makeBook();
    const auto before = cache_test::files;
    BookMetadataCache writer("/book");
    stage(writer, 3, "Replacement");
    cache_test::failClose = 0;
    EXPECT_FALSE(build(writer));
    EXPECT_GT(cache_test::closes, 0);
    EXPECT_EQ(0, cache_test::handles);
    EXPECT_FALSE(cache_test::files.contains("/book/book.bin.new"));
    EXPECT_FALSE(cache_test::files.contains("/book/book.bin.bak"));
    if (previous)
      EXPECT_EQ(before.at("/book/book.bin"), cache_test::files.at("/book/book.bin"));
    else
      EXPECT_FALSE(cache_test::files.contains("/book/book.bin"));
    cache_test::resetFaults();
  }
}

TEST_F(MetadataBuildFault, PublicationPreservesPreviousCacheAtEveryAssemblyFault) {
  makeBook();
  const auto previous = cache_test::files.at("/book/book.bin");
  int counts[3];
  {
    BookMetadataCache writer("/book");
    stage(writer, 400, "Replacement");
    ASSERT_TRUE(build(writer));
    counts[0] = cache_test::reads;
    counts[1] = cache_test::seeks;
    counts[2] = cache_test::writes;
  }
  for (int op = 0; op < 3; ++op) {
    for (int call = 0; call < counts[op]; ++call) {
      cache_test::files["/book/book.bin"] = previous;
      BookMetadataCache writer("/book");
      stage(writer, 400, "Replacement");
      if (op == 0) cache_test::failRead = call;
      if (op == 1) cache_test::failSeek = call;
      if (op == 2) cache_test::failWrite = call;
      ASSERT_FALSE(build(writer)) << op << ':' << call;
      EXPECT_EQ(previous, cache_test::files.at("/book/book.bin"));
      EXPECT_FALSE(cache_test::files.contains("/book/book.bin.new"));
      EXPECT_FALSE(cache_test::files.contains("/book/book.bin.bak"));
      EXPECT_EQ(0, cache_test::handles);
      cache_test::resetFaults();
    }
  }
}

TEST_F(MetadataBuildFault, PublicationRenameRollbackAndCommitFailuresKeepPreviousReadable) {
  makeBook();
  const auto previous = cache_test::files.at("/book/book.bin");
  for (int fault = 0; fault < 4; ++fault) {
    cache_test::files.clear();
    cache_test::files["/book/book.bin"] = previous;
    cache_test::files["/book/progress.bin"] = {42, 7};
    BookMetadataCache writer("/book");
    stage(writer, 3, "Replacement");
    if (fault < 3) cache_test::failRename = fault == 0 ? 0 : 1;
    if (fault == 2) cache_test::failRenameAgain = 2;
    if (fault == 3) cache_test::failRemove = 0;
    EXPECT_FALSE(build(writer)) << fault;
    EXPECT_EQ(0, cache_test::handles);
    const bool backup = cache_test::files.contains("/book/book.bin.bak");
    EXPECT_EQ(previous, cache_test::files.at(backup ? "/book/book.bin.bak" : "/book/book.bin"));
    cache_test::resetFaults();
    {
      BookMetadataCache reader("/book");
      ASSERT_TRUE(reader.load());
      EXPECT_EQ("First chapter", reader.getTocEntry(0).title);
    }
    stage(writer, 3, "Replacement");
    ASSERT_TRUE(build(writer));
    EXPECT_FALSE(cache_test::files.contains("/book/book.bin.new"));
    EXPECT_FALSE(cache_test::files.contains("/book/book.bin.bak"));
    EXPECT_EQ((std::vector<uint8_t>{42, 7}), cache_test::files.at("/book/progress.bin"));
    BookMetadataCache reader("/book");
    ASSERT_TRUE(reader.load());
    EXPECT_EQ("Replacement", reader.getTocEntry(0).title);
  }
}

TEST_F(MetadataBuildFault, PublicationInterruptedStatesNeverReadUncommittedReplacement) {
  makeBook();
  const auto previous = cache_test::files.at("/book/book.bin");
  {
    BookMetadataCache writer("/book");
    stage(writer, 3, "Replacement");
    ASSERT_TRUE(build(writer));
  }
  const auto replacement = cache_test::files.at("/book/book.bin");
  for (int state = 0; state < 6; ++state) {
    cache_test::files.clear();
    if (state == 0 || state == 1 || state == 2) cache_test::files["/book/book.bin.new"] = replacement;
    if (state == 1) cache_test::files["/book/book.bin"] = previous;
    if (state == 2 || state == 3 || state == 4) cache_test::files["/book/book.bin.bak"] = previous;
    if (state == 3 || state == 4 || state == 5) cache_test::files["/book/book.bin"] = replacement;
    if (state == 4) cache_test::files["/book/book.bin.bak"].resize(1);
    const auto before = cache_test::files;
    BookMetadataCache reader("/book");
    EXPECT_EQ(state != 0 && state != 4, reader.load()) << state;
    if (reader.isLoaded()) EXPECT_EQ(state == 5 ? "Replacement" : "First chapter", reader.getTocEntry(0).title);
    EXPECT_EQ(before, cache_test::files);
  }
}

TEST_F(MetadataBuildFault, PublicationRecoveryFailuresRetainAuthoritativeBackup) {
  makeBook();
  const auto previous = cache_test::files.at("/book/book.bin");
  for (bool remove : {true, false}) {
    cache_test::files["/book/book.bin.bak"] = previous;
    cache_test::files["/book/book.bin"] = {1, 2};
    BookMetadataCache writer("/book");
    stage(writer);
    if (remove)
      cache_test::failRemove = 0;
    else
      cache_test::failRename = 0;
    EXPECT_FALSE(build(writer));
    EXPECT_EQ(previous, cache_test::files.at("/book/book.bin.bak"));
    EXPECT_EQ(0, cache_test::handles);
    cache_test::resetFaults();
    BookMetadataCache reader("/book");
    ASSERT_TRUE(reader.load());
    EXPECT_EQ("First chapter", reader.getTocEntry(0).title);
  }
}

TEST_F(MetadataBuildFault, PublicationPathOomDoesNotTouchCommittedOrRecoveryFiles) {
  makeBook();
  cache_test::files["/book/book.bin.bak"] = cache_test::files.at("/book/book.bin");
  BookMetadataCache writer("/book");
  stage(writer);
  const auto before = cache_test::files;
  {
    parser_test::ScopedAllocationFailure oom(parser_test::ScopedAllocationFailure::Kind::Array);
    EXPECT_FALSE(build(writer));
    EXPECT_EQ(1u, oom.failures());
  }
  EXPECT_EQ(before, cache_test::files);
  BookMetadataCache reader("/book");
  {
    parser_test::ScopedAllocationFailure oom(parser_test::ScopedAllocationFailure::Kind::Array);
    EXPECT_FALSE(reader.load());
    EXPECT_FALSE(reader.isLoaded());
    EXPECT_EQ(1u, oom.failures());
  }
  EXPECT_EQ(before, cache_test::files);
  EXPECT_EQ(0, cache_test::handles);
}

TEST_F(MetadataBuildFault, PublicationFirstBuildRenameFailureLeavesNoReadableCache) {
  BookMetadataCache writer("/book");
  stage(writer);
  cache_test::failRename = 0;
  EXPECT_FALSE(build(writer));
  EXPECT_FALSE(cache_test::files.contains("/book/book.bin"));
  EXPECT_FALSE(cache_test::files.contains("/book/book.bin.bak"));
  EXPECT_FALSE(cache_test::files.contains("/book/book.bin.new"));
  EXPECT_EQ(0, cache_test::handles);
  cache_test::resetFaults();
  ASSERT_TRUE(build(writer));
  BookMetadataCache reader("/book");
  ASSERT_TRUE(reader.load());
}

TEST_F(MetadataBuildFault, PublicationFailedCleanupOrOpenCannotReplacePreviousCache) {
  makeBook();
  const auto previous = cache_test::files.at("/book/book.bin");
  for (bool open : {false, true}) {
    BookMetadataCache writer("/book");
    stage(writer, 3, "Replacement");
    if (open)
      cache_test::failOpen = true;
    else {
      cache_test::failClose = 0;
      cache_test::failRemove = 0;
    }
    EXPECT_FALSE(build(writer));
    EXPECT_EQ(previous, cache_test::files.at("/book/book.bin"));
    EXPECT_EQ(0, cache_test::handles);
    cache_test::resetFaults();
    {
      BookMetadataCache reader("/book");
      ASSERT_TRUE(reader.load());
      EXPECT_EQ("First chapter", reader.getTocEntry(0).title);
    }
  }
  BookMetadataCache writer("/book");
  stage(writer, 3, "Replacement");
  ASSERT_TRUE(build(writer));
  EXPECT_FALSE(cache_test::files.contains("/book/book.bin.new"));
  EXPECT_FALSE(cache_test::files.contains("/book/book.bin.bak"));
}
