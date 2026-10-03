#pragma once

// Read side of the CLX1 index.
//
// Holds one open file handle and a few hundred bytes; nothing that scales with
// the library stays resident. A page of rows is a handful of seeks, which is
// what the fixed 128-byte record stride buys — record k is always at
// recordStart + 128k, so no offset table has to be loaded to find it.

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "LibraryFormat.h"

namespace library {

enum class SortOrder : uint8_t {
  // File modification time, oldest first in Asc; firstSeen breaks ties.
  RecentAsc,
  RecentDesc,
  TitleAsc,
  TitleDesc,
  AuthorAsc,
  AuthorDesc,
  SeriesAsc,
  SeriesDesc,
  AddedAsc = RecentAsc,
  AddedDesc = RecentDesc,
};

// Complete-path identity; a zero size means the caller could not stat the file.
struct BookIdentity {
  uint64_t pathHash;
  uint32_t fileSize;
};

struct PathIdentity {
  std::string_view path;
  uint64_t pathHash;
  uint32_t fileSize = 0;  // Hint only: changed-size books are searched in a second pass.
};

class LibraryIndexFile {
 public:
  LibraryIndexFile() = default;
  ~LibraryIndexFile();
  LibraryIndexFile(const LibraryIndexFile&) = delete;
  LibraryIndexFile& operator=(const LibraryIndexFile&) = delete;

  // Open and validate. On failure `validity()` says why, so a rebuild loop
  // caused by a format bug is visible in the log rather than looking like a slow
  // first boot.
  bool open(const char* path);
  // Accept an otherwise valid stale fold so a rebuild can preserve arrival
  // history without exposing stale sort/search keys to the browser.
  bool openForReconciliation(const char* path);
  void close();
  bool isOpen() const { return opened; }
  bool ioFailed() const { return readFailed; }

  ClixValidity validity() const { return lastValidity; }
  const ClixHeader& header() const { return head; }
  uint16_t bookCount() const { return opened ? head.bookCount : 0; }
  bool limitsReached() const { return opened && (head.flags & CLIX_FLAG_LIMITS_REACHED) != 0; }
  bool ranksDegraded() const { return opened && (head.flags & CLIX_FLAG_RANKS_DEGRADED) != 0; }
  bool dedupDegraded() const { return opened && (head.flags & CLIX_FLAG_DEDUP_DEGRADED) != 0; }
  uint16_t seriesCount() const { return opened ? head.seriesCount : 0; }
  // Books belonging to a series, which is also where the standalone block starts
  // in series order.
  uint16_t knownSeriesCount() const { return opened ? head.knownSeriesCount : 0; }
  // Whether a series order is worth offering at all. An index built before the
  // reader turned book metadata on carries no series, and a tab that leads to
  // nothing but ungrouped books is worse than no tab.
  bool hasSeries() const { return opened && head.seriesCount > 0 && head.knownSeriesCount > 0; }

  // Record ordinal of the row at display position `row` in `order`. Returns
  // 0xFFFF when out of range, which callers treat as "no such row" rather than
  // indexing anyway.
  uint16_t ordinalForRow(SortOrder order, uint16_t row);

  // Resolve ascending arrival rows in two bounded passes. Missing books return
  // 0xFFFF; callers must discard all output rows if the operation fails.
  static constexpr size_t MAX_IDENTITY_LOOKUPS = 16;
  bool recentRowsFor(const BookIdentity* books, size_t count, uint16_t* outRows);

  // Refresh anchors: exact paths, confirmed after the persisted hash matches.
  // Missing paths return 0xFFFF. Failure leaves every output at 0xFFFF.
  static constexpr size_t MAX_PATH_LOOKUPS = 2;
  bool rowsForPaths(SortOrder order, const PathIdentity* paths, size_t count, uint16_t* outRows);

  bool readRecord(uint16_t ordinal, ClixRecord& out);
  // Persisted complete-path fingerprint used by rebuild reconciliation.
  bool readPathHash(const ClixRecord& record, uint64_t& out);

  // Display basename, exactly as it sits on the card. This is the only string
  // the UI draws, and it is never shortened on disk.
  bool readName(const ClixRecord& record, std::string& out);
  // The author the build settled on, stored right after the name. Reading it
  // rather than re-deriving it from the name is what makes the metadata pass and
  // the spelling harmonisation visible: neither survives a filename that no
  // longer carries "Title - Author".
  // Empty is a valid Unknown Author value; false means the field could not be read.
  bool readAuthor(const ClixRecord& record, std::string& out);
  bool readTitle(const ClixRecord& record, std::string& out);
  // Read the display fields in one bounded pass. Empty fields are valid;
  // malformed or unreadable blobs fail and clear both outputs.
  bool readTitleAndAuthor(const ClixRecord& record, std::string& title, std::string& author);
  bool readTitleAndSourceAuthor(const ClixRecord& record, std::string& title, std::string& author);
  // The display fields plus the chosen spelling's author sort, for author headings.
  bool readTitleAuthorAndSort(const ClixRecord& record, std::string& title, std::string& author,
                              std::string& authorSort);
  // Cleaned author spelling before the library-wide spelling vote. Empty is a
  // valid value, so success is independent of `out.empty()`.
  bool readSourceAuthor(const ClixRecord& record, std::string& out);
  // Everything a rebuild carries across for an unchanged book (format 5 only).
  bool readReuseFields(const ClixRecord& record, std::string& title, std::string& sourceAuthor,
                       std::string& sourceAuthorSort, std::string& uuid);
  // Author sort of the chosen spelling ("Tolkien, J. R. R."); empty is valid.
  bool readAuthorSort(const ClixRecord& record, std::string& out);
  // The book's 16-byte UUID; false when the book names none.
  bool readUuid(const ClixRecord& record, std::string& out);

  // Absolute path of the book, rebuilt from its folder record.
  bool readPath(const ClixRecord& record, std::string& out);

  // The book's series and position. Fills `out` with CLIX_SERIES_NONE when the
  // book belongs to none, so callers can read it unconditionally.
  bool readSeriesRef(uint16_t ordinal, ClixSeriesRef& out);
  // Name and on-card book count of one series. `seriesId` comes from a
  // ClixSeriesRef; anything out of range fails rather than reading a neighbour.
  bool readSeries(uint16_t seriesId, std::string& name, uint16_t& bookCount, uint32_t* identity = nullptr);

 private:
  bool openImpl(const char* path, bool acceptStaleFold);
  bool readAt(uint32_t offset, void* dst, size_t len);
  bool validName(const ClixRecord& record) const;
  uint16_t readOrdinal(uint32_t orderStart, uint16_t row);
  bool readBlobField(const ClixRecord& record, uint8_t field, std::string& out);
  // Blob fields 0..count-1 into the non-null outputs, in one buffered pass.
  bool readFields(const ClixRecord& record, std::string* const* outs, uint8_t count);

  HalFile file;
  ClixHeader head{};
  uint32_t nextReadOffset = UINT32_MAX;
  static constexpr uint16_t ORDER_CACHE_ENTRIES = 32;
  uint16_t orderCache[ORDER_CACHE_ENTRIES]{};
  uint32_t orderCacheOffset = UINT32_MAX;
  uint8_t orderCacheCount = 0;
  bool opened = false;
  bool readFailed = false;
  ClixValidity lastValidity = ClixValidity::BadMagic;
};

}  // namespace library
