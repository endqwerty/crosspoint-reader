#include <ZipFile.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

namespace {
const std::string path = "/test.epub";
template <typename T>
void append(std::vector<uint8_t>& bytes, T value) {
  for (size_t i = 0; i < sizeof(T); ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
void directoryEntry(std::vector<uint8_t>& bytes, const std::string& name, uint32_t size) {
  append(bytes, uint32_t{0x02014b50});
  append(bytes, uint16_t{20});
  append(bytes, uint16_t{20});
  append(bytes, uint16_t{0});
  append(bytes, uint16_t{0});
  append(bytes, uint32_t{0});
  append(bytes, uint32_t{0});
  append(bytes, size);
  append(bytes, size);
  append(bytes, static_cast<uint16_t>(name.size()));
  append(bytes, uint16_t{0});
  append(bytes, uint16_t{0});
  append(bytes, uint16_t{0});
  append(bytes, uint16_t{0});
  append(bytes, uint32_t{0});
  append(bytes, uint32_t{0});
  bytes.insert(bytes.end(), name.begin(), name.end());
}
void makeDirectory(size_t padding = 0) {
  cache_test::files.clear();
  cache_test::resetFaults();
  auto& bytes = cache_test::files[path];
  bytes.resize(padding, 0);
  directoryEntry(bytes, "a.xhtml", 100);
  directoryEntry(bytes, "b.xhtml", 200);
  const auto directorySize = static_cast<uint32_t>(bytes.size() - padding);
  append(bytes, uint32_t{0x06054b50});
  append(bytes, uint32_t{0});
  append(bytes, uint16_t{2});
  append(bytes, uint16_t{2});
  append(bytes, directorySize);
  append(bytes, static_cast<uint32_t>(padding));
  append(bytes, uint16_t{0});
}
ZipFile::SizeTarget target(const char* name, uint16_t index) {
  return {ZipFile::fnvHash64(name, strlen(name)), static_cast<uint16_t>(strlen(name)), index};
}
template <size_t N>
void sort(std::array<ZipFile::SizeTarget, N>& targets) {
  std::sort(targets.begin(), targets.end(),
            [](const auto& a, const auto& b) { return a.hash < b.hash || (a.hash == b.hash && a.len < b.len); });
}
}  // namespace
TEST(ZipFileTest, BatchSpansPreserveDuplicateTargetsAndIgnoreInvalidOutputIndices) {
  makeDirectory();
  std::array targets{target("b.xhtml", 0), target("a.xhtml", 1), target("a.xhtml", 2), target("missing.xhtml", 3),
                     target("a.xhtml", 20)};
  sort(targets);
  std::array<uint32_t, 4> sizes{};
  ZipFile zip(path);
  EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 3);
  EXPECT_EQ(sizes, (std::array<uint32_t, 4>{200, 100, 100, 0}));
  EXPECT_FALSE(zip.isOpen());
  EXPECT_EQ(cache_test::handles, 0);
}
TEST(ZipFileTest, RepeatedChunkLookupsRewindDirectoryAndPreserveOpenHandle) {
  makeDirectory();
  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  for (auto name : {"b.xhtml", "a.xhtml"}) {
    const std::array targets{target(name, 0)};
    std::array<uint32_t, 1> sizes{};
    EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 1);
    EXPECT_EQ(sizes[0], name[0] == 'a' ? 100u : 200u);
    EXPECT_TRUE(zip.isOpen());
  }
}
TEST(ZipFileTest, EmptySpansDoNotOpenStorage) {
  makeDirectory();
  ZipFile zip(path);
  EXPECT_EQ(zip.fillUncompressedSizes({}, {}), 0);
  EXPECT_EQ(cache_test::opens, 0);
}
TEST(ZipFileTest, DirectoryTrailerCanBeginAtAnyByteAlignment) {
  for (size_t padding = 0; padding < 4; ++padding) {
    makeDirectory(padding);
    ZipFile zip(path);
    const std::array targets{target("a.xhtml", 0)};
    std::array<uint32_t, 1> sizes{};
    ASSERT_EQ(zip.fillUncompressedSizes(targets, sizes), 1);
    EXPECT_EQ(sizes[0], 100u);
  }
}

TEST(ZipFileTest, TrailerReadFailureDoesNotExposeUninitializedDirectory) {
  for (bool read : {false, true}) {
    makeDirectory();
    if (read)
      cache_test::failRead = 0;
    else
      cache_test::failSeek = 0;
    ZipFile zip(path);
    const std::array targets{target("a.xhtml", 0)};
    std::array<uint32_t, 1> sizes{};
    EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), -1);
    EXPECT_EQ(sizes[0], 0u);
    EXPECT_FALSE(zip.isOpen());
    EXPECT_EQ(cache_test::handles, 0);
  }
}

namespace {
void write16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}
void write32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
int lookupBoth(ZipFile& zip) {
  std::array targets{target("a.xhtml", 0), target("b.xhtml", 1)};
  sort(targets);
  std::array<uint32_t, 2> sizes{};
  return zip.fillUncompressedSizes(targets, sizes);
}
bool scan(ZipFile& zip, int mode) {
  switch (mode) {
    case 0:
      return lookupBoth(zip) == 2;
    case 1: {
      size_t size = 0;
      return zip.getInflatedFileSize("b.xhtml", &size);
    }
    case 2:
      return zip.loadAllFileStatSlims();
    default:
      return zip.enumerateFileEntries([](std::string_view, uint32_t, uint32_t) {});
  }
}
}  // namespace

TEST(ZipFileTest, EveryPersistentSdFailureStopsAllDirectoryReaders) {
  for (int mode = 0; mode < 4; ++mode) {
    makeDirectory();
    int operations;
    {
      ZipFile zip(path);
      ASSERT_TRUE(scan(zip, mode));
      operations = cache_test::ioOperations;
    }
    for (int failAt = 1; failAt <= operations; ++failAt) {
      SCOPED_TRACE(mode);
      SCOPED_TRACE(failAt);
      makeDirectory();
      cache_test::persistentFailureAt = failAt;
      ZipFile zip(path);
      EXPECT_NO_THROW(EXPECT_FALSE(scan(zip, mode)));
      EXPECT_LE(cache_test::ioOperations, failAt + 1);
      EXPECT_FALSE(zip.isOpen());
      EXPECT_EQ(cache_test::handles, 0);
    }
  }
}

TEST(ZipFileTest, EveryTransientReadAndSeekFailureRejectsTheScanAndCanRetry) {
  for (int mode = 0; mode < 4; ++mode) {
    makeDirectory();
    int reads, seeks;
    {
      ZipFile zip(path);
      ASSERT_TRUE(scan(zip, mode));
      reads = cache_test::reads;
      seeks = cache_test::seeks;
    }
    for (bool read : {false, true}) {
      for (int failAt = 0; failAt < (read ? reads : seeks); ++failAt) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(read);
        SCOPED_TRACE(failAt);
        makeDirectory();
        if (read)
          cache_test::failRead = failAt;
        else
          cache_test::failSeek = failAt;
        ZipFile zip(path);
        EXPECT_FALSE(scan(zip, mode));
        EXPECT_FALSE(zip.isOpen());
        cache_test::resetFaults();
        EXPECT_TRUE(scan(zip, mode));
      }
    }
  }
}

TEST(ZipFileTest, TwoEntryScanUsesFiveReadsAndTwoSeeks) {
  makeDirectory();
  ZipFile zip(path);
  EXPECT_EQ(lookupBoth(zip), 2);
  EXPECT_EQ(cache_test::reads, 5);
  EXPECT_EQ(cache_test::seeks, 2);
}

TEST(ZipFileTest, DeclaredTrailerRangesAndCountsAreCheckedBeforeScanning) {
  for (int variant = 0; variant < 7; ++variant) {
    makeDirectory();
    auto& bytes = cache_test::files[path];
    const size_t end = bytes.size() - 22;
    switch (variant) {
      case 0:
        write32(bytes, end + 16, UINT32_MAX);
        break;
      case 1:
        write32(bytes, end + 12, UINT32_MAX);
        break;
      case 2:
        write16(bytes, end + 8, 65535);
        write16(bytes, end + 10, 65535);
        break;
      case 3:
        write16(bytes, end + 4, 1);
        break;
      case 4:
        write16(bytes, end + 6, 1);
        break;
      case 5:
        write16(bytes, end + 8, 1);
        break;
      case 6:
        write16(bytes, end + 20, 5);
        break;
    }
    ZipFile zip(path);
    EXPECT_EQ(lookupBoth(zip), -1);
    EXPECT_LE(cache_test::reads, 1);
  }
}

TEST(ZipFileTest, EntryFieldsCannotExtendPastTheDeclaredDirectory) {
  for (size_t field : {size_t{28}, size_t{30}, size_t{32}}) {
    makeDirectory();
    write16(cache_test::files[path], field, 65535);
    ZipFile zip(path);
    EXPECT_EQ(lookupBoth(zip), -1);
    EXPECT_LE(cache_test::reads, 2);
  }
}

TEST(ZipFileTest, BadSecondSignatureDoesNotReturnPartialResultsAsSuccess) {
  makeDirectory();
  cache_test::files[path][46 + 7] = 0;
  ZipFile zip(path);
  EXPECT_EQ(lookupBoth(zip), -1);
  EXPECT_FALSE(zip.isOpen());
}

TEST(ZipFileTest, SequentialLookupWrapsAndMissesRemainBounded) {
  makeDirectory();
  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  for (const auto* name : {"b.xhtml", "a.xhtml", "b.xhtml", "a.xhtml"}) {
    size_t size = 0;
    ASSERT_TRUE(zip.getInflatedFileSize(name, &size));
    EXPECT_EQ(size, name[0] == 'a' ? 100u : 200u);
  }
  size_t size = 1234;
  EXPECT_FALSE(zip.getInflatedFileSize("missing.xhtml", &size));
  EXPECT_EQ(size, 1234u);
  EXPECT_TRUE(zip.getInflatedFileSize("a.xhtml", &size));
  EXPECT_EQ(size, 100u);
  EXPECT_TRUE(zip.isOpen());
}

TEST(ZipFileTest, MaximumSupportedNameIsPassedAsAnExactLengthView) {
  makeDirectory();
  auto& bytes = cache_test::files[path];
  bytes.clear();
  const std::string name(255, 'n');
  directoryEntry(bytes, name, 123);
  const auto dirSize = static_cast<uint32_t>(bytes.size());
  append(bytes, uint32_t{0x06054b50});
  append(bytes, uint32_t{0});
  append(bytes, uint16_t{1});
  append(bytes, uint16_t{1});
  append(bytes, dirSize);
  append(bytes, uint32_t{0});
  append(bytes, uint16_t{0});
  ZipFile zip(path);
  int called = 0;
  EXPECT_TRUE(zip.enumerateFilePaths([&](std::string_view value) {
    EXPECT_EQ(value, name);
    ++called;
  }));
  EXPECT_EQ(called, 1);
  size_t size = 0;
  EXPECT_TRUE(zip.getInflatedFileSize(name.c_str(), &size));
  EXPECT_EQ(size, 123u);
}

TEST(ZipFileTest, ShortDirectoryReadsFailWithoutUsingPartialFields) {
  for (int mode = 0; mode < 4; ++mode) {
    makeDirectory();
    int reads;
    {
      ZipFile zip(path);
      ASSERT_TRUE(scan(zip, mode));
      reads = cache_test::reads;
    }
    for (int call = 0; call < reads; ++call) {
      makeDirectory();
      cache_test::shortRead = call;
      ZipFile zip(path);
      EXPECT_FALSE(scan(zip, mode));
    }
  }
}

TEST(ZipFileTest, SignatureBytesInsideACommentDoNotHideTheRealTrailer) {
  makeDirectory();
  auto& bytes = cache_test::files[path];
  write16(bytes, bytes.size() - 2, 32);
  append(bytes, uint32_t{0x06054b50});
  bytes.resize(bytes.size() + 28, 0);
  ZipFile zip(path);
  EXPECT_EQ(lookupBoth(zip), 2);
}

TEST(ZipFileTest, DirectorySignatureDoesNotBreakSequentialLookupWraparound) {
  makeDirectory();
  auto& bytes = cache_test::files[path];
  const auto end = bytes.size() - 22;
  bytes.insert(bytes.begin() + end, {0x50, 0x4b, 0x05, 0x05, 0, 0});
  write32(bytes, end + 6 + 12, static_cast<uint32_t>(end + 6));
  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  size_t size = 0;
  EXPECT_TRUE(zip.getInflatedFileSize("b.xhtml", &size));
  EXPECT_EQ(size, 200u);
  EXPECT_TRUE(zip.getInflatedFileSize("a.xhtml", &size));
  EXPECT_EQ(size, 100u);
}

namespace {
const std::string storedText = "chapter one";
const std::string deflatedText = "chapter two chapter two";
void makeCompleteArchive(bool compressed) {
  cache_test::files.clear();
  cache_test::resetFaults();
  auto& bytes = cache_test::files[path];
  static constexpr uint8_t deflate[] = {75, 206, 72, 44, 40, 73, 45, 82, 40, 41, 207, 87, 72, 70, 176, 1};
  const auto method = static_cast<uint16_t>(compressed ? 8 : 0);
  const auto size = static_cast<uint32_t>((compressed ? deflatedText : storedText).size());
  const auto packed = static_cast<uint32_t>(compressed ? sizeof(deflate) : storedText.size());
  const uint32_t crc = compressed ? 0xad22e8d7u : 0x61da2885u;
  const std::string name = "a.xhtml";
  append(bytes, uint32_t{0x04034b50});
  append(bytes, uint16_t{20});
  append(bytes, uint16_t{0});
  append(bytes, method);
  append(bytes, uint32_t{0});
  append(bytes, crc);
  append(bytes, packed);
  append(bytes, size);
  append(bytes, static_cast<uint16_t>(name.size()));
  append(bytes, uint16_t{0});
  bytes.insert(bytes.end(), name.begin(), name.end());
  if (compressed)
    bytes.insert(bytes.end(), std::begin(deflate), std::end(deflate));
  else
    bytes.insert(bytes.end(), storedText.begin(), storedText.end());
  const auto directory = static_cast<uint32_t>(bytes.size());
  directoryEntry(bytes, name, size);
  write16(bytes, directory + 10, method);
  write32(bytes, directory + 16, crc);
  write32(bytes, directory + 20, packed);
  const auto length = static_cast<uint32_t>(bytes.size()) - directory;
  append(bytes, uint32_t{0x06054b50});
  append(bytes, uint32_t{0});
  append(bytes, uint16_t{1});
  append(bytes, uint16_t{1});
  append(bytes, length);
  append(bytes, directory);
  append(bytes, uint16_t{0});
}
class Sink : public Print {
 public:
  std::string text;
  bool oversized = false;
  size_t write(uint8_t value) override {
    text.push_back(static_cast<char>(value));
    return 1;
  }
  size_t write(const uint8_t* bytes, size_t size) override {
    if (size > 1024) {
      oversized = true;
      return 0;
    }
    text.append(reinterpret_cast<const char*>(bytes), size);
    return size;
  }
};
}  // namespace

TEST(ZipFileTest, StoredAndDeflatedEntriesExtractToMemoryAndStream) {
  for (bool compressed : {false, true}) {
    makeCompleteArchive(compressed);
    ZipFile zip(path);
    size_t size = 0;
    auto* bytes = zip.readFileToMemory("a.xhtml", &size, true);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(std::string(reinterpret_cast<char*>(bytes), size), compressed ? deflatedText : storedText);
    EXPECT_EQ(bytes[size], 0);
    free(bytes);
    Sink sink;
    ASSERT_TRUE(zip.readFileToStream("a.xhtml", sink, 8));
    EXPECT_EQ(sink.text, compressed ? deflatedText : storedText);
    EXPECT_FALSE(sink.oversized);
    EXPECT_FALSE(zip.isOpen());
    EXPECT_TRUE(zip.enumerateFileEntries([&](std::string_view name, uint32_t crc, uint32_t packed) {
      EXPECT_EQ(name, "a.xhtml");
      EXPECT_EQ(crc, compressed ? 0xad22e8d7u : 0x61da2885u);
      EXPECT_EQ(packed, compressed ? 16u : 11u);
    }));
    ASSERT_TRUE(zip.loadAllFileStatSlims());
    EXPECT_TRUE(zip.getInflatedFileSize("a.xhtml", &size));
    EXPECT_EQ(size, compressed ? deflatedText.size() : storedText.size());
  }
}

TEST(ZipFileTest, EveryExtractionReadAndSeekFailureIsReported) {
  for (bool compressed : {false, true}) {
    for (bool stream : {false, true}) {
      const auto extract = [stream](ZipFile& zip) {
        if (stream) {
          Sink sink;
          const bool ok = zip.readFileToStream("a.xhtml", sink, 8, true);
          EXPECT_FALSE(sink.oversized);
          return ok;
        }
        auto* bytes = zip.readFileToMemory("a.xhtml");
        const bool ok = bytes != nullptr;
        free(bytes);
        return ok;
      };
      makeCompleteArchive(compressed);
      int reads, seeks;
      {
        ZipFile zip(path);
        ASSERT_TRUE(extract(zip));
        reads = cache_test::reads;
        seeks = cache_test::seeks;
      }
      for (bool read : {false, true}) {
        for (int fail = 0; fail < (read ? reads : seeks); ++fail) {
          SCOPED_TRACE(compressed);
          SCOPED_TRACE(stream);
          SCOPED_TRACE(read);
          SCOPED_TRACE(fail);
          makeCompleteArchive(compressed);
          if (read)
            cache_test::failRead = fail;
          else
            cache_test::failSeek = fail;
          ZipFile zip(path);
          EXPECT_FALSE(extract(zip));
          EXPECT_FALSE(zip.isOpen());
        }
      }
    }
  }
}

TEST(ZipFileTest, ZeroSizedStreamBuffersAreRejectedBeforeStorageAccess) {
  makeCompleteArchive(true);
  ZipFile zip(path);
  Sink sink;
  EXPECT_FALSE(zip.readFileToStream("a.xhtml", sink, 0));
  EXPECT_EQ(cache_test::opens, 0);
}

TEST(ZipFileTest, OverlongNamesAndExtraFieldsDoNotMisalignFollowingEntries) {
  makeDirectory();
  auto& bytes = cache_test::files[path];
  bytes.clear();
  directoryEntry(bytes, std::string(256, 'n'), 100);
  write16(bytes, 30, 3);
  write16(bytes, 32, 5);
  bytes.insert(bytes.end(), 8, 0);
  directoryEntry(bytes, "b.xhtml", 200);
  const auto length = static_cast<uint32_t>(bytes.size());
  append(bytes, uint32_t{0x06054b50});
  append(bytes, uint32_t{0});
  append(bytes, uint16_t{2});
  append(bytes, uint16_t{2});
  append(bytes, length);
  append(bytes, uint32_t{0});
  append(bytes, uint16_t{0});
  ZipFile zip(path);
  size_t size = 0;
  EXPECT_TRUE(zip.getInflatedFileSize("b.xhtml", &size));
  EXPECT_EQ(size, 200u);
  int names = 0;
  EXPECT_TRUE(zip.enumerateFilePaths([&](std::string_view name) {
    EXPECT_EQ(name, "b.xhtml");
    ++names;
  }));
  EXPECT_EQ(names, 1);
}

TEST(ZipFileTest, InvalidLocalSignaturesAreRejectedWithoutSignedShiftOverflow) {
  makeCompleteArchive(false);
  cache_test::files[path][3] = 255;
  ZipFile zip(path);
  EXPECT_EQ(zip.readFileToMemory("a.xhtml"), nullptr);
  Sink sink;
  EXPECT_FALSE(zip.readFileToStream("a.xhtml", sink, 8));
}
