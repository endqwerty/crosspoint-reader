#include "LibraryFollowOns.h"

#include <HalStorage.h>

#include <cstring>

#include "LibraryBookState.h"

namespace library {
namespace {

// Rows examined after the current book; finished books consume this budget so a
// long finished run cannot turn the end screen into a shelf walk.
constexpr uint16_t MAX_SCAN_ROWS = 16;

// A finished book, or one whose file has gone, is not a suggestion. A state that
// cannot be read is treated as unread rather than hiding the book.
bool suggestible(const std::string& path) {
  BookState state;
  if (readBookState(bookStateKey(path), state) && state.reading == ReadingState::Finished) return false;
  return Storage.exists(path.c_str());
}

bool fillFollowOn(LibraryIndexFile& index, const ClixRecord& record, FollowOn& out) {
  if (!index.readPath(record, out.path) || !suggestible(out.path)) return false;
  if (!index.readTitle(record, out.title) || out.title.empty()) {
    const size_t slash = out.path.find_last_of('/');
    out.title = out.path.substr(slash + 1);
  }
  return true;
}

struct Anchor {
  uint16_t row;
  uint16_t ordinal;
};

// Position of `path` in `order`, and its record ordinal.
bool locate(LibraryIndexFile& index, const SortOrder order, const std::string_view path, Anchor& out) {
  const PathIdentity identity{path, bookStateKey(path)};
  uint16_t row = 0xFFFF;
  if (!index.rowsForPaths(order, &identity, 1, &row) || row == 0xFFFF) return false;
  const uint16_t ordinal = index.ordinalForRow(order, row);
  if (ordinal == 0xFFFF) return false;
  out = {row, ordinal};
  return true;
}

// Walks the rows after `from` while `sameGroup` holds, collecting suggestible books.
template <typename SameGroup>
size_t collect(LibraryIndexFile& index, const SortOrder order, const uint16_t from, FollowOn* out,
               const size_t maxCount, SameGroup sameGroup) {
  size_t found = 0;
  uint16_t scanned = 0;
  for (uint32_t row = static_cast<uint32_t>(from) + 1;
       row < index.bookCount() && scanned < MAX_SCAN_ROWS && found < maxCount; ++row, ++scanned) {
    const uint16_t ordinal = index.ordinalForRow(order, static_cast<uint16_t>(row));
    ClixRecord record;
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record) || !sameGroup(ordinal, record)) break;
    if (fillFollowOn(index, record, out[found])) ++found;
  }
  return index.ioFailed() ? 0 : found;
}

}  // namespace

size_t findFollowOns(LibraryIndexFile& index, const std::string_view bookPath, FollowOn* out, const size_t maxCount) {
  if (!index.isOpen() || !out || maxCount == 0 || index.bookCount() == 0) return 0;

  Anchor bySeries;
  if (!locate(index, SortOrder::SeriesAsc, bookPath, bySeries)) return 0;
  ClixSeriesRef ref{};
  if (!index.readSeriesRef(bySeries.ordinal, ref)) return 0;

  if (ref.seriesId != CLIX_SERIES_NONE) {
    const size_t found = collect(index, SortOrder::SeriesAsc, bySeries.row, out, maxCount,
                                 [&](const uint16_t nextOrdinal, const ClixRecord&) {
                                   ClixSeriesRef next{};
                                   return index.readSeriesRef(nextOrdinal, next) && next.seriesId == ref.seriesId;
                                 });
    if (found) return found;
  }

  // Author identities are twelve-byte keys; a book with none (initials only)
  // has no reliable neighbours.
  ClixRecord current;
  if (!index.readRecord(bySeries.ordinal, current) || current.authorKeyLen == 0) return 0;
  Anchor byAuthor;
  if (!locate(index, SortOrder::AuthorAsc, bookPath, byAuthor)) return 0;
  return collect(index, SortOrder::AuthorAsc, byAuthor.row, out, maxCount, [&](const uint16_t, const ClixRecord& next) {
    return next.authorKeyLen == current.authorKeyLen &&
           memcmp(next.authorKey, current.authorKey, current.authorKeyLen) == 0;
  });
}

}  // namespace library
