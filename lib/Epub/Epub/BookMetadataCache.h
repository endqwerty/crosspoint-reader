#pragma once

#include <BufferedFile.h>
#include <ChunkedVector.h>
#include <HalStorage.h>

#include <algorithm>
#include <memory>
#include <string>

class BookMetadataCache {
 public:
  struct BookMetadata {
    std::string title;
    std::string author;
    std::string language;
    std::string coverItemHref;
    std::string textReferenceHref;
  };

  struct SpineEntry {
    std::string href;
    uint32_t cumulativeSize;
    int16_t tocIndex;

    SpineEntry() : cumulativeSize(0), tocIndex(-1) {}
    SpineEntry(std::string href, const uint32_t cumulativeSize, const int16_t tocIndex)
        : href(std::move(href)), cumulativeSize(cumulativeSize), tocIndex(tocIndex) {}
  };

  struct TocEntry {
    std::string title;
    std::string href;
    std::string anchor;
    uint8_t level;
    int16_t spineIndex;

    TocEntry() : level(0), spineIndex(-1) {}
    TocEntry(std::string title, std::string href, std::string anchor, const uint8_t level, const int16_t spineIndex)
        : title(std::move(title)),
          href(std::move(href)),
          anchor(std::move(anchor)),
          level(level),
          spineIndex(spineIndex) {}
  };

 private:
  std::string cachePath;
  uint32_t lutOffset;
  uint16_t spineCount;
  uint16_t tocCount;
  bool loaded;
  bool buildMode;
  bool passFailed = false;

  HalFile bookFile;
  // Temp file handles during build
  HalFile spineFile;
  HalFile tocFile;
  // Buffers the per-entry tmp-file writes during the OPF/TOC passes: those
  // writes interleave with zip-inflate SD reads, and unbuffered they thrash
  // SdFat's shared sector cache (one 512B transaction per 4-byte pod). One
  // wrapper serves whichever pass is active (spine, then toc).
  std::unique_ptr<serialization::BufferedFileWriter> passOut;

  // Cumulative spine sizes, cached in RAM at load() so progress/percent lookups are
  // O(1) instead of 2 seeks + a heap-allocating SpineEntry read per access (4 bytes
  // per spine item; <1KB for typical books).
  std::unique_ptr<uint32_t[]> cumulativeSizes;

  // Index for fast href→spineIndex lookup (used only for large EPUBs)
  struct SpineHrefIndexEntry {
    uint64_t hrefHash;  // FNV-1a 64-bit hash
    uint16_t hrefLen;   // length for collision reduction
    int16_t spineIndex;
  };
  using SpineHrefIndex = ChunkedVector<SpineHrefIndexEntry, 32, 256, 256>;
  std::unique_ptr<SpineHrefIndex> spineHrefIndex;
  bool useSpineHrefIndex = false;

  static constexpr uint16_t LARGE_SPINE_THRESHOLD = 400;

  // Linear TOC-to-spine search below the threshold: the spine entry matched last,
  // so the next search resumes there instead of rereading the staged spine from its start.
  int tocScanIndex = 0;
  uint32_t tocScanOffset = 0;

  // FNV-1a 64-bit hash function
  static uint64_t fnvHash64(const std::string& s) {
    uint64_t hash = 14695981039346656037ull;
    for (char c : s) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 1099511628211ull;
    }
    return hash;
  }

  bool writeSpineEntry(HalFile& file, const SpineEntry& entry) const;
  bool writeTocEntry(HalFile& file, const TocEntry& entry) const;
  void invalidateReadCache();
  bool seekCacheEntry(uint32_t lutIndex, uint32_t& end);

 public:
  BookMetadata coreMetadata;

  explicit BookMetadataCache(std::string cachePath)
      : cachePath(std::move(cachePath)), lutOffset(0), spineCount(0), tocCount(0), loaded(false), buildMode(false) {}
  ~BookMetadataCache() = default;

  // Building phase (stream to disk immediately)
  bool beginWrite();
  bool beginContentOpfPass();
  void createSpineEntry(const std::string& href);
  bool endContentOpfPass();
  bool beginTocPass();
  void createTocEntry(const std::string& title, const std::string& href, const std::string& anchor, uint8_t level);
  bool endTocPass();
  bool endWrite();
  bool cleanupTmpFiles() const;

  // Post-processing to update mappings and sizes
  bool buildBookBin(const std::string& epubPath, const BookMetadata& metadata);

  // Reading phase (read mode)
  bool load();
  // Entry faults return empty without discarding validated counts/sizes: readers
  // use those to distinguish navigation failures from the end of the book.
  SpineEntry getSpineEntry(int index);
  TocEntry getTocEntry(int index);
  // Cumulative byte size up to and including the given spine item (0 if out of range
  // or not loaded). Backed by the in-RAM cumulativeSizes cache populated in load().
  uint32_t getCumulativeSize(int index) const;
  int getSpineCount() const { return spineCount; }
  int getTocCount() const { return tocCount; }
  bool isLoaded() const { return loaded; }
};
