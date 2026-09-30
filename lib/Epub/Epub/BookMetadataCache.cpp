#include "BookMetadataCache.h"

#include <Arduino.h>
#include <BufferedFile.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>
#include <ZipFile.h>

#include <span>

#include "FsHelpers.h"

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 10;  // v10: ignore ambiguous guide text references
constexpr char bookBinFile[] = "/book.bin";
constexpr char bookBackupFile[] = "/book.bin.bak";
constexpr char bookPendingFile[] = "/book.bin.new";

struct BookCachePaths {
  std::unique_ptr<char[]> storage;
  char* primary = nullptr;
  char* backup = nullptr;
  char* pending = nullptr;

  bool init(const std::string& directory) {
    if (directory.size() > SIZE_MAX / 3 - sizeof(bookPendingFile)) {
      LOG_ERR("BMC", "Cache path exceeds size range");
      return false;
    }
    const size_t stride = directory.size() + sizeof(bookPendingFile);
    // Variable-length paths share one fallible allocation; no resident path table.
    storage = makeUniqueNoThrow<char[]>(3 * stride);
    if (!storage) {
      LOG_ERR("BMC", "OOM staging cache paths");
      return false;
    }
    primary = storage.get();
    backup = primary + stride;
    pending = backup + stride;
    memcpy(primary, directory.data(), directory.size());
    memcpy(primary + directory.size(), bookBinFile, sizeof(bookBinFile));
    memcpy(backup, directory.data(), directory.size());
    memcpy(backup + directory.size(), bookBackupFile, sizeof(bookBackupFile));
    memcpy(pending, directory.data(), directory.size());
    memcpy(pending + directory.size(), bookPendingFile, sizeof(bookPendingFile));
    return true;
  }
};

bool restoreBookCache(const BookCachePaths& paths) {
  if (!Storage.exists(paths.backup)) return true;
  if ((Storage.exists(paths.primary) && !Storage.remove(paths.primary)) ||
      !Storage.rename(paths.backup, paths.primary)) {
    LOG_ERR("BMC", "Could not restore book.bin; backup retained");
    return false;
  }
  return true;
}
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
// Buffer size for the buildBookBin streams. 3 buffers x 4KB, transient (freed on
// return); 4KB = 8 SD sectors per transfer, enough to stop the sector-cache thrash.
constexpr size_t BUILD_IO_BUFFER_SIZE = 4096;
constexpr size_t SPINE_INDEX_IO_BUFFER_SIZE = 512;
constexpr uint32_t TOC_INDEX_HEAP_RESERVE = 16 * 1024;
constexpr uint32_t BOOK_BIN_HEAP_RESERVE = 32 * 1024;
constexpr size_t BOOK_BIN_ITEM_BYTES = sizeof(ZipFile::SizeTarget) + sizeof(uint32_t) + sizeof(int16_t);
constexpr int BOOK_BIN_MIN_CHUNK = 64;
// One field cannot consume the reader heap. OPF display metadata is capped at
// 512 bytes; paths and TOC captions get a larger bound for nested EPUB content.
constexpr uint32_t MAX_CACHE_FIELD_BYTES = 4096;
constexpr uint32_t SPINE_TAIL_BYTES = sizeof(uint32_t) + sizeof(int16_t);
constexpr uint32_t TOC_TAIL_BYTES = sizeof(uint8_t) + sizeof(int16_t);
constexpr uint32_t MIN_SPINE_BYTES = sizeof(uint32_t) + SPINE_TAIL_BYTES;
constexpr uint32_t MIN_TOC_BYTES = sizeof(uint32_t) * 3 + TOC_TAIL_BYTES;

template <typename F>
bool readCacheString(F& file, std::string& value, const uint32_t end, const bool caption = false) {
  uint32_t length = 0;
  auto pos = file.position();
  if (pos > end || sizeof(length) > end - pos ||
      static_cast<size_t>(file.read(&length, sizeof(length))) != sizeof(length))
    return false;
  pos = file.position();
  if (length > end - pos || (!caption && length > MAX_CACHE_FIELD_BYTES)) return false;
  value.resize(std::min(length, MAX_CACHE_FIELD_BYTES));
  if ((!value.empty() && static_cast<size_t>(file.read(value.data(), value.size())) != value.size()) ||
      value.find('\0') != std::string::npos)
    return false;
  if (value.size() < length) {
    value.resize(utf8SafeTruncateBuffer(value.data(), static_cast<int>(value.size())));
    return file.seek(pos + length);
  }
  return true;
}

template <typename T>
bool readBufferedCachePod(serialization::BufferedFileReader& file, T& value) {
  return file.read(&value, sizeof(value)) == sizeof(value);
}

// Buffered entry writers defer error reporting to flush(); the raw fallback
// uses checked serialization. Stage readers share bounded decoding across both.
template <typename F>
uint32_t writeSpineEntryTo(F& file, const BookMetadataCache::SpineEntry& entry) {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.href);
  serialization::writePod(file, entry.cumulativeSize);
  serialization::writePod(file, entry.tocIndex);
  return pos;
}

template <typename F>
uint32_t writeTocEntryTo(F& file, const BookMetadataCache::TocEntry& entry) {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.title);
  serialization::writeString(file, entry.href);
  serialization::writeString(file, entry.anchor);
  serialization::writePod(file, entry.level);
  serialization::writePod(file, entry.spineIndex);
  return pos;
}

template <typename F, typename T>
bool readStagePod(F& file, T& value) {
  return static_cast<size_t>(file.read(&value, sizeof(value))) == sizeof(value);
}

template <typename F>
bool readSpineEntryFrom(F& file, BookMetadataCache::SpineEntry& entry, const uint32_t end) {
  if (!readCacheString(file, entry.href, end) || entry.href.empty() || !readStagePod(file, entry.cumulativeSize) ||
      !readStagePod(file, entry.tocIndex)) {
    LOG_ERR("BMC", "Invalid or unreadable spine staging entry");
    return false;
  }
  return true;
}

template <typename F>
bool readTocEntryFrom(F& file, BookMetadataCache::TocEntry& entry, const uint32_t end, const uint16_t spineCount) {
  if (!readCacheString(file, entry.title, end, true) || !readCacheString(file, entry.href, end) ||
      !readCacheString(file, entry.anchor, end) || !readStagePod(file, entry.level) ||
      !readStagePod(file, entry.spineIndex) || entry.spineIndex < -1 || entry.spineIndex >= spineCount) {
    LOG_ERR("BMC", "Invalid or unreadable TOC staging entry");
    return false;
  }
  return true;
}
}  // namespace

/* ============= WRITING / BUILDING FUNCTIONS ================ */

bool BookMetadataCache::beginWrite() {
  invalidateReadCache();
  buildMode = true;
  passFailed = false;
  LOG_DBG("BMC", "Entering write mode");
  return true;
}

bool BookMetadataCache::beginContentOpfPass() {
  LOG_DBG("BMC", "Beginning content opf pass");

  // Open spine file for writing
  if (!Storage.openFileForWrite("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  // Wrapper OOM is fine: createSpineEntry falls back to unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(spineFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endContentOpfPass() {
  const bool flushed = !passOut || passOut->flush();
  passOut.reset();
  // Explicit close() required: member variable persists beyond function scope
  const bool closed = spineFile.close();
  if (!flushed || !closed) {
    LOG_ERR("BMC", "Failed writing spine tmp file");
  }
  passFailed |= !flushed || !closed;
  return !passFailed;
}

bool BookMetadataCache::beginTocPass() {
  LOG_DBG("BMC", "Beginning toc pass");
  tocCount = 0;
  tocScanIndex = 0;
  tocScanOffset = 0;
  spineHrefIndex.reset();
  useSpineHrefIndex = false;
  const auto failPass = [this]() {
    LOG_ERR("BMC", "Failed to prepare TOC index");
    passFailed = true;
    spineHrefIndex.reset();
    tocFile.close();
    spineFile.close();
    return false;
  };
  if (passFailed || !Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile) ||
      !Storage.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile))
    return failPass();

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    const uint32_t needed = static_cast<uint32_t>(spineCount) * sizeof(SpineHrefIndexEntry) + sizeof(SpineHrefIndex) +
                            TOC_INDEX_HEAP_RESERVE;
    if (ESP.getFreeHeap() < needed) return failPass();
    spineHrefIndex = makeUniqueNoThrow<SpineHrefIndex>();
    if (!spineHrefIndex || !spineFile.seek(0)) return failPass();
    const auto spineBytes = spineFile.fileSize64();
    if (spineBytes > UINT32_MAX) return failPass();
    // A transient sector buffer batches the sequential index scan; OOM uses direct reads.
    serialization::BufferedFileReader in(spineFile, SPINE_INDEX_IO_BUFFER_SIZE);
    SpineEntry entry;
    for (int i = 0; i < spineCount; i++) {
      if (!readSpineEntryFrom(in, entry, spineBytes)) return failPass();
      const SpineHrefIndexEntry idx{fnvHash64(entry.href), static_cast<uint16_t>(entry.href.size()),
                                    static_cast<int16_t>(i)};
      if (!spineHrefIndex->push_back(idx)) return failPass();
    }
    if (in.position() != spineBytes) return failPass();
    std::sort(spineHrefIndex->begin(), spineHrefIndex->end(),
              [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
              });
    if (!spineFile.seek(0)) return failPass();
    useSpineHrefIndex = true;
    LOG_DBG("BMC", "Using fast index for %d spine items", spineCount);
  }

  // Wrapper OOM is fine: createTocEntry falls back to checked unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(tocFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endTocPass() {
  const bool flushed = !passOut || passOut->flush();
  passOut.reset();
  // Explicit close() required: member variables persist beyond function scope
  const bool tocClosed = tocFile.close();
  const bool spineClosed = spineFile.close();
  if (!flushed || !tocClosed || !spineClosed) {
    LOG_ERR("BMC", "Failed closing TOC pass");
  }

  spineHrefIndex.reset();
  useSpineHrefIndex = false;

  passFailed |= !flushed || !tocClosed || !spineClosed;
  return !passFailed;
}

bool BookMetadataCache::endWrite() {
  if (!buildMode) {
    LOG_DBG("BMC", "endWrite called but not in build mode");
    return false;
  }

  buildMode = false;
  LOG_DBG("BMC", "Wrote %d spine, %d TOC entries", spineCount, tocCount);
  return !passFailed;
}

bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  if (passFailed) {
    LOG_ERR("BMC", "Refusing cache after failed index pass");
    return false;
  }
  for (const auto* field :
       {&metadata.title, &metadata.author, &metadata.language, &metadata.coverItemHref, &metadata.textReferenceHref}) {
    if (field->size() > MAX_CACHE_FIELD_BYTES || field->find('\0') != std::string::npos) {
      LOG_ERR("BMC", "Invalid core metadata field");
      return false;
    }
  }
  BookCachePaths paths;
  if (!paths.init(cachePath) || !restoreBookCache(paths)) return false;
  // The previous cache remains readable until the replacement closes successfully.
  if (!Storage.openFileForWrite("BMC", paths.pending, bookFile)) {
    return false;
  }
  bool committed = false;
  ScopedCleanup cleanup{[this, &committed, &paths] {
    bookFile.close();
    spineFile.close();
    tocFile.close();
    if (!committed) {
      LOG_ERR("BMC", "Discarding incomplete book.bin.new");
      Storage.remove(paths.pending);
    }
  }};

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + tmpTocBinFile, tocFile)) {
    return false;
  }

  const auto spineFileBytes = spineFile.fileSize64();
  const auto tocFileBytes = tocFile.fileSize64();
  if (spineFileBytes > UINT32_MAX || tocFileBytes > UINT32_MAX) return false;

  // Buffered streams for the whole build: every access below is sequential per
  // file, but interleaved ACROSS files, which thrashes SdFat's single shared
  // sector cache when unbuffered (one 512B SD transaction per 4-byte pod --
  // measured 31s for a 1,732-spine omnibus). Three 4KB buffers, freed on return.
  serialization::BufferedFileWriter bookOut(bookFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader spineIn(spineFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader tocIn(tocFile, BUILD_IO_BUFFER_SIZE);

  constexpr uint32_t headerASize =
      sizeof(BOOK_CACHE_VERSION) + /* LUT Offset */ sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount);
  const uint32_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5;
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount;
  const uint32_t lutOffset = headerASize + metadataSize;
  if (static_cast<uint64_t>(lutOffset) + lutSize + spineFileBytes > UINT32_MAX) return false;

  // Header A
  serialization::writePod(bookOut, BOOK_CACHE_VERSION);
  serialization::writePod(bookOut, lutOffset);
  serialization::writePod(bookOut, spineCount);
  serialization::writePod(bookOut, tocCount);
  // Metadata
  serialization::writeString(bookOut, metadata.title);
  serialization::writeString(bookOut, metadata.author);
  serialization::writeString(bookOut, metadata.language);
  serialization::writeString(bookOut, metadata.coverItemHref);
  serialization::writeString(bookOut, metadata.textReferenceHref);

  // Reuse record capacity within each scan; release it before the next phase.
  // Loop through spine entries, writing LUT positions
  if (!spineIn.seek(0)) return false;
  {
    SpineEntry entry;
    for (int i = 0; i < spineCount; i++) {
      const uint32_t pos = spineIn.position();
      if (!readSpineEntryFrom(spineIn, entry, spineFileBytes)) return false;
      serialization::writePod(bookOut, pos + lutOffset + lutSize);
    }
  }
  if (spineIn.position() != spineFileBytes) return false;
  // Total size of the spine tmp file: entries land in book.bin after the toc LUT
  // and the full spine block, so toc LUT positions are offset by it.
  const auto spineBytes = static_cast<uint32_t>(spineIn.position());

  // Loop through toc entries, writing LUT positions
  if (!tocIn.seek(0)) return false;
  uint32_t tocOutputBytes = 0;
  {
    TocEntry entry;
    for (int i = 0; i < tocCount; i++) {
      if (!readTocEntryFrom(tocIn, entry, tocFileBytes, spineCount)) return false;
      const uint64_t offset = static_cast<uint64_t>(lutOffset) + lutSize + spineBytes + tocOutputBytes;
      const uint32_t entryBytes = MIN_TOC_BYTES + entry.title.size() + entry.href.size() + entry.anchor.size();
      if (offset + entryBytes > UINT32_MAX) return false;
      serialization::writePod(bookOut, static_cast<uint32_t>(offset));
      tocOutputBytes += entryBytes;
    }
  }
  if (tocIn.position() != tocFileBytes) return false;

  // Reuse one bounded workspace across chunks; ordinary books retain one TOC
  // scan and one ZIP scan. The reserves leave room for paths and stream buffers.
  const bool isZip = !FsHelpers::hasTxtExtension(epubPath) && !FsHelpers::hasMarkdownExtension(epubPath);
  const bool useBatchSizes = isZip && spineCount >= LARGE_SPINE_THRESHOLD;
  int chunk = spineCount;
  if (useBatchSizes) {
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t budget = freeHeap > BOOK_BIN_HEAP_RESERVE ? freeHeap - BOOK_BIN_HEAP_RESERVE : 0;
    chunk = static_cast<int>(std::min<uint32_t>(spineCount, budget / BOOK_BIN_ITEM_BYTES));
  }
  if (chunk < std::min<int>(spineCount, BOOK_BIN_MIN_CHUNK)) {
    LOG_ERR("BMC", "Insufficient heap for spine sizing workspace");
    return false;
  }
  auto spineToTocIndex = chunk > 0 ? makeUniqueNoThrow<int16_t[]>(chunk) : nullptr;
  auto spineSizes = useBatchSizes ? makeUniqueNoThrow<uint32_t[]>(chunk) : nullptr;
  auto targets = useBatchSizes ? makeUniqueNoThrow<ZipFile::SizeTarget[]>(chunk) : nullptr;
  if ((chunk > 0 && !spineToTocIndex) || (useBatchSizes && (!spineSizes || !targets))) {
    LOG_ERR("BMC", "OOM: spine sizing workspace");
    return false;
  }

  ZipFile zip(epubPath);
  // TXT/Markdown books are a single spine item sized by the source file.
  size_t rawSize = 0;
  if (isZip) {
    if (!zip.open()) {
      LOG_ERR("BMC", "Could not open EPUB zip for size calculations");
      return false;
    }
  } else {
    HalFile rawFile;
    if (Storage.openFileForRead("BMC", epubPath, rawFile)) {
      rawSize = rawFile.size();
    }
  }

  uint32_t cumSize = 0;
  if (!spineIn.seek(0)) return false;
  int lastSpineTocIndex = -1;
  for (int first = 0; first < spineCount; first += chunk) {
    const int count = std::min(chunk, spineCount - first);
    std::fill_n(spineToTocIndex.get(), count, int16_t{-1});
    if (!tocIn.seek(0)) return false;
    {
      TocEntry entry;
      for (int j = 0; j < tocCount; j++) {
        if (!readTocEntryFrom(tocIn, entry, tocFileBytes, spineCount)) return false;
        const int local = entry.spineIndex - first;
        if (local >= 0 && local < count && spineToTocIndex[local] == -1) {
          spineToTocIndex[local] = static_cast<int16_t>(j);
        }
      }
    }

    if (useBatchSizes) {
      std::fill_n(spineSizes.get(), count, uint32_t{0});
      const size_t chunkStart = spineIn.position();
      {
        SpineEntry entry;
        for (int i = 0; i < count; i++) {
          if (!readSpineEntryFrom(spineIn, entry, spineFileBytes)) return false;
          const std::string path = FsHelpers::normalisePath(entry.href);
          targets[i] = {ZipFile::fnvHash64(path.c_str(), path.size()), static_cast<uint16_t>(path.size()),
                        static_cast<uint16_t>(i)};
        }
      }
      std::sort(targets.get(), targets.get() + count, [](const ZipFile::SizeTarget& a, const ZipFile::SizeTarget& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
      });
      const int matched = zip.fillUncompressedSizes({targets.get(), static_cast<size_t>(count)},
                                                    {spineSizes.get(), static_cast<size_t>(count)});
      if (matched < 0) {
        LOG_ERR("BMC", "Failed reading EPUB central directory");
        return false;
      }
      LOG_DBG("BMC", "Batch lookup matched %d/%d spine items", matched, count);
      if (!spineIn.seek(chunkStart)) return false;
    }

    {
      SpineEntry spineEntry;
      for (int i = 0; i < count; i++) {
        if (!readSpineEntryFrom(spineIn, spineEntry, spineFileBytes)) return false;
        spineEntry.tocIndex = spineToTocIndex[i];
        if (spineEntry.tocIndex == -1) spineEntry.tocIndex = lastSpineTocIndex;
        lastSpineTocIndex = spineEntry.tocIndex;
        size_t itemSize = isZip ? (useBatchSizes ? spineSizes[i] : 0) : rawSize;
        if (isZip && itemSize == 0) {
          const std::string path = FsHelpers::normalisePath(spineEntry.href);
          if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
            LOG_ERR("BMC", "Could not get size for spine item: %s", path.c_str());
          }
        }
        if (itemSize > UINT32_MAX - cumSize) {
          LOG_ERR("BMC", "Cumulative spine size exceeds cache range");
          return false;
        }
        cumSize += itemSize;
        spineEntry.cumulativeSize = cumSize;
        writeSpineEntryTo(bookOut, spineEntry);
      }
    }
  }
  spineSizes.reset();
  if (isZip) zip.close();

  // Loop through toc entries from toc file writing to book.bin
  if (!tocIn.seek(0)) return false;
  {
    TocEntry tocEntry;
    for (int i = 0; i < tocCount; i++) {
      if (!readTocEntryFrom(tocIn, tocEntry, tocFileBytes, spineCount)) return false;
      writeTocEntryTo(bookOut, tocEntry);
    }
  }
  if (bookOut.position() != static_cast<uint64_t>(lutOffset) + lutSize + spineBytes + tocOutputBytes) return false;

  const bool written = bookOut.flush();
  // Close the member handle before either rename; SdFat close also reports sync failure.
  const bool closed = bookFile.close();
  if (!written || !closed) {
    LOG_ERR("BMC", "Failed writing staged book.bin");
    return false;
  }

  const bool hadPrevious = Storage.exists(paths.primary);
  if (hadPrevious && !Storage.rename(paths.primary, paths.backup)) {
    LOG_ERR("BMC", "Could not retain previous book.bin");
    return false;
  }
  if (!Storage.rename(paths.pending, paths.primary)) {
    LOG_ERR("BMC", "Could not install book.bin");
    restoreBookCache(paths);
    return false;
  }
  if (hadPrevious && !Storage.remove(paths.backup)) {
    LOG_ERR("BMC", "Could not commit book.bin; backup retained");
    return false;
  }

  committed = true;
  LOG_DBG("BMC", "Successfully built book.bin");
  return true;
}

bool BookMetadataCache::cleanupTmpFiles() const {
  const auto spineBinFile = cachePath + tmpSpineBinFile;
  if (Storage.exists(spineBinFile.c_str())) {
    Storage.remove(spineBinFile.c_str());
  }
  const auto tocBinFile = cachePath + tmpTocBinFile;
  if (Storage.exists(tocBinFile.c_str())) {
    Storage.remove(tocBinFile.c_str());
  }
  return true;
}

bool BookMetadataCache::writeSpineEntry(HalFile& file, const SpineEntry& entry) const {
  return serialization::writeStringChecked(file, entry.href) &&
         serialization::writePodChecked(file, entry.cumulativeSize) &&
         serialization::writePodChecked(file, entry.tocIndex);
}

bool BookMetadataCache::writeTocEntry(HalFile& file, const TocEntry& entry) const {
  return serialization::writeStringChecked(file, entry.title) && serialization::writeStringChecked(file, entry.href) &&
         serialization::writeStringChecked(file, entry.anchor) && serialization::writePodChecked(file, entry.level) &&
         serialization::writePodChecked(file, entry.spineIndex);
}

// Note: for the LUT to be accurate, this **MUST** be called for all spine items before `addTocEntry` is ever called
// this is because in this function we're marking positions of the items
void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (passFailed) return;
  if (!buildMode || !spineFile) {
    LOG_DBG("BMC", "createSpineEntry called but not in build mode");
    return;
  }

  if (spineCount >= static_cast<uint32_t>(INT16_MAX) + 1) {
    LOG_ERR("BMC", "Spine count exceeds signed cache index range");
    passFailed = true;
    return;
  }
  const SpineEntry entry(href, 0, -1);
  if (passOut) {
    writeSpineEntryTo(*passOut, entry);
  } else if (!writeSpineEntry(spineFile, entry)) {
    LOG_ERR("BMC", "Failed writing spine staging entry");
    passFailed = true;
    return;
  }
  spineCount++;
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (passFailed) return;
  if (!buildMode || !tocFile || !spineFile) {
    LOG_DBG("BMC", "createTocEntry called but not in build mode");
    return;
  }

  if (tocCount >= static_cast<uint32_t>(INT16_MAX) + 1) {
    LOG_ERR("BMC", "TOC count exceeds signed cache index range");
    passFailed = true;
    return;
  }
  int16_t spineIndex = -1;

  if (useSpineHrefIndex) {
    uint64_t targetHash = fnvHash64(href);
    uint16_t targetLen = static_cast<uint16_t>(href.size());

    auto it =
        std::lower_bound(spineHrefIndex->begin(), spineHrefIndex->end(), SpineHrefIndexEntry{targetHash, targetLen, 0},
                         [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                           return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
                         });

    while (it != spineHrefIndex->end() && it->hrefHash == targetHash && it->hrefLen == targetLen) {
      spineIndex = it->spineIndex;
      break;
    }

    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  } else {
    const auto spineBytes = spineFile.fileSize64();
    if (spineBytes > UINT32_MAX) {
      LOG_ERR("BMC", "Cannot seek spine staging file");
      passFailed = true;
      return;
    }
    // TOC entries almost always follow spine order, and several often share one
    // file. Search from the previous match to the end, then wrap to the start.
    SpineEntry spineEntry;
    const auto scan = [&](const int first, const int last, const uint32_t offset) {
      if (!spineFile.seek(offset)) return false;
      for (int i = first; i < last && spineIndex < 0; i++) {
        const auto entryOffset = static_cast<uint32_t>(spineFile.position());
        if (!readSpineEntryFrom(spineFile, spineEntry, spineBytes)) return false;
        if (spineEntry.href == href) {
          spineIndex = static_cast<int16_t>(i);
          tocScanIndex = i;
          tocScanOffset = entryOffset;
        }
      }
      return true;
    };
    const int resume = tocScanIndex;
    if (!scan(resume, spineCount, tocScanOffset) || (spineIndex < 0 && resume > 0 && !scan(0, resume, 0))) {
      LOG_ERR("BMC", "Cannot scan spine staging file");
      passFailed = true;
      return;
    }
    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  }

  // Compose the title to NFC at index time so the cache stores precomposed glyphs;
  // device fonts have no combining-mark positioning, so NFD titles render broken.
  const TocEntry entry(utf8ComposeNfc(title), href, anchor, level, spineIndex);
  if (passOut) {
    writeTocEntryTo(*passOut, entry);
  } else if (!writeTocEntry(tocFile, entry)) {
    LOG_ERR("BMC", "Failed writing TOC staging entry");
    passFailed = true;
    return;
  }
  tocCount++;
}

/* ============= READING / LOADING FUNCTIONS ================ */

void BookMetadataCache::invalidateReadCache() {
  loaded = false;
  bookFile.close();
  cumulativeSizes.reset();
  coreMetadata = {};
  lutOffset = 0;
  spineCount = tocCount = 0;
}

bool BookMetadataCache::load() {
  invalidateReadCache();
  ScopedCleanup cleanup{[this] {
    if (!loaded) {
      LOG_ERR("BMC", "Invalid or unreadable book cache");
      invalidateReadCache();
    }
  }};
  BookCachePaths paths;
  if (!paths.init(cachePath)) return false;
  // A surviving backup is authoritative until publication has committed.
  const char* selected = Storage.exists(paths.backup) ? paths.backup : paths.primary;
  if (!Storage.openFileForRead("BMC", selected, bookFile)) return false;

  const auto fileSize = bookFile.size();
  uint8_t version = 0;
  if (fileSize > UINT32_MAX || !serialization::readPodChecked(bookFile, version) || version != BOOK_CACHE_VERSION ||
      !serialization::readPodChecked(bookFile, lutOffset) || !serialization::readPodChecked(bookFile, spineCount) ||
      !serialization::readPodChecked(bookFile, tocCount))
    return false;

  const uint32_t lutSize = (static_cast<uint32_t>(spineCount) + tocCount) * sizeof(uint32_t);
  if (lutOffset < bookFile.position() || lutOffset > fileSize || lutSize > fileSize - lutOffset) return false;
  const uint32_t dataStart = lutOffset + lutSize;
  const uint32_t minDataBytes =
      static_cast<uint32_t>(spineCount) * MIN_SPINE_BYTES + static_cast<uint32_t>(tocCount) * MIN_TOC_BYTES;
  if (minDataBytes > fileSize - dataStart) return false;

  if (!readCacheString(bookFile, coreMetadata.title, lutOffset) ||
      !readCacheString(bookFile, coreMetadata.author, lutOffset) ||
      !readCacheString(bookFile, coreMetadata.language, lutOffset) ||
      !readCacheString(bookFile, coreMetadata.coverItemHref, lutOffset) ||
      !readCacheString(bookFile, coreMetadata.textReferenceHref, lutOffset) || bookFile.position() != lutOffset)
    return false;

  // Reuse the four-byte-per-spine cache for the LUT while validating the stream.
  // Allocation is once per load, fallible, and released if any read fails.
  if (spineCount != 0) {
    cumulativeSizes = makeUniqueNoThrow<uint32_t[]>(spineCount);
    if (!cumulativeSizes) {
      LOG_ERR("BMC", "OOM: %u chapter sizes", static_cast<unsigned>(spineCount));
      return false;
    }
    if (!serialization::readBytesChecked(bookFile, cumulativeSizes.get(), spineCount * sizeof(uint32_t))) return false;
  }
  uint32_t tocStart = static_cast<uint32_t>(fileSize);
  if (tocCount && !serialization::readPodChecked(bookFile, tocStart)) return false;
  if (tocStart < dataStart || tocStart > fileSize ||
      static_cast<uint32_t>(tocCount) * MIN_TOC_BYTES > fileSize - tocStart || !bookFile.seek(dataStart))
    return false;

  // One transient buffer batches SD reads; OOM uses BufferedFileReader's
  // unbuffered fallback. Hrefs are skipped without per-chapter string allocation.
  serialization::BufferedFileReader in(bookFile, BUILD_IO_BUFFER_SIZE);
  uint32_t previousSize = 0;
  for (uint16_t i = 0; i < spineCount; ++i) {
    uint32_t length = 0;
    uint32_t size = 0;
    int16_t tocIndex = -1;
    const auto pos = in.position();
    const uint32_t end = i + 1 < spineCount ? cumulativeSizes[i + 1] : tocStart;
    if (cumulativeSizes[i] != pos || end < pos || end > tocStart || end - pos < MIN_SPINE_BYTES ||
        !readBufferedCachePod(in, length) || length == 0 || length > MAX_CACHE_FIELD_BYTES ||
        length != end - pos - MIN_SPINE_BYTES || !in.seek(in.position() + length) || !readBufferedCachePod(in, size) ||
        !readBufferedCachePod(in, tocIndex) || in.position() != end || size < previousSize || tocIndex < -1 ||
        tocIndex >= static_cast<int>(tocCount))
      return false;
    cumulativeSizes[i] = size;
    previousSize = size;
  }
  if (in.position() != tocStart) return false;

  loaded = true;
  LOG_DBG("BMC", "Loaded cache data: %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

uint32_t BookMetadataCache::getCumulativeSize(const int index) const {
  if (!loaded || !cumulativeSizes || index < 0 || index >= spineCount) return 0;
  return cumulativeSizes[index];
}

bool BookMetadataCache::seekCacheEntry(const uint32_t lutIndex, uint32_t& end) {
  const uint32_t count = static_cast<uint32_t>(spineCount) + tocCount;
  const auto fileSize = bookFile.size();
  const uint32_t lutSize = count * sizeof(uint32_t);
  if (!loaded || lutIndex >= count || fileSize > UINT32_MAX || lutOffset > fileSize || lutSize > fileSize - lutOffset)
    return false;
  uint32_t offsets[2] = {0, static_cast<uint32_t>(fileSize)};
  const size_t bytes = (lutIndex + 1 < count ? 2 : 1) * sizeof(uint32_t);
  if (!bookFile.seek(lutOffset + lutIndex * sizeof(uint32_t)) ||
      !serialization::readBytesChecked(bookFile, offsets, bytes))
    return false;
  end = offsets[1];
  const uint32_t minimum = lutIndex < spineCount ? MIN_SPINE_BYTES : MIN_TOC_BYTES;
  return offsets[0] >= lutOffset + lutSize && offsets[0] <= end && end <= fileSize && end - offsets[0] >= minimum &&
         bookFile.seek(offsets[0]);
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  if (!loaded || index < 0 || index >= spineCount) {
    LOG_ERR("BMC", "Spine index %d unavailable", index);
    return {};
  }
  SpineEntry entry;
  uint32_t end = 0;
  if (seekCacheEntry(static_cast<uint32_t>(index), end) &&
      readCacheString(bookFile, entry.href, end - SPINE_TAIL_BYTES) && !entry.href.empty() &&
      serialization::readPodChecked(bookFile, entry.cumulativeSize) &&
      serialization::readPodChecked(bookFile, entry.tocIndex) && bookFile.position() == end &&
      entry.cumulativeSize == cumulativeSizes[index] && entry.tocIndex >= -1 &&
      entry.tocIndex < static_cast<int>(tocCount))
    return entry;
  LOG_ERR("BMC", "Unreadable spine entry %d", index);
  return {};
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  if (!loaded || index < 0 || index >= tocCount) {
    LOG_ERR("BMC", "TOC index %d unavailable", index);
    return {};
  }
  TocEntry entry;
  uint32_t end = 0;
  if (seekCacheEntry(static_cast<uint32_t>(spineCount) + index, end) &&
      readCacheString(bookFile, entry.title, end - TOC_TAIL_BYTES, true) &&
      readCacheString(bookFile, entry.href, end - TOC_TAIL_BYTES) &&
      readCacheString(bookFile, entry.anchor, end - TOC_TAIL_BYTES) &&
      serialization::readPodChecked(bookFile, entry.level) &&
      serialization::readPodChecked(bookFile, entry.spineIndex) && bookFile.position() == end &&
      entry.spineIndex >= -1 && entry.spineIndex < static_cast<int>(spineCount))
    return entry;
  LOG_ERR("BMC", "Unreadable TOC entry %d", index);
  return {};
}
