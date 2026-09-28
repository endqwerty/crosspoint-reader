#include "LibraryIndexFile.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "LibraryText.h"

namespace library {

LibraryIndexFile::~LibraryIndexFile() { close(); }

bool LibraryIndexFile::open(const char* path) { return openImpl(path, false); }

bool LibraryIndexFile::openForReconciliation(const char* path) { return openImpl(path, true); }

bool LibraryIndexFile::openImpl(const char* path, const bool acceptStaleFold) {
  close();
  readFailed = false;
  if (!Storage.openFileForRead("LIBIDX", path, file)) {
    readFailed = true;
    return false;
  }

  if (file.read(&head, sizeof(head)) != static_cast<int>(sizeof(head))) {
    readFailed = true;
    lastValidity = ClixValidity::SizeMismatch;
    file.close();
    return false;
  }
  nextReadOffset = sizeof(head);

  if (acceptStaleFold && head.formatVersion == 2) {
    // v2 shares the 128-byte records and name blobs; its header predates series.
    head.selfSize = head.nameStart;
    head.nameStart = head.seriesStart;
    head.nameLen = head.seriesRefStart;
    head.seriesStart = head.seriesRefStart = 0;
    head.seriesCount = head.knownSeriesCount = 0;
  }
  lastValidity = acceptStaleFold ? validateHeaderStructure(head, file.fileSize64(), true)
                                 : validateHeader(head, file.fileSize64());
  if (lastValidity != ClixValidity::Ok) {
    LOG_INF("LIBIDX", "index rejected: %s", clixValidityName(lastValidity));
    file.close();
    return false;
  }
  opened = true;
  return true;
}

void LibraryIndexFile::close() {
  if (file.isOpen()) file.close();
  opened = false;
  nextReadOffset = UINT32_MAX;
  orderCacheCount = 0;
}

bool LibraryIndexFile::readAt(const uint32_t offset, void* dst, const size_t len) {
  if (!opened) return false;
  // Every offset handed to this function comes from the header, and the header
  // was validated against the real file size, so a short read means the card
  // changed under us rather than a bad computation.
  if ((nextReadOffset != offset && !file.seekSet(offset)) || file.read(dst, len) != static_cast<int>(len)) {
    nextReadOffset = UINT32_MAX;
    orderCacheCount = 0;
    readFailed = true;
    return false;
  }
  nextReadOffset = offset + len;
  return true;
}

uint16_t LibraryIndexFile::readOrdinal(const uint32_t orderStart, const uint16_t row) {
  const uint16_t base = row / ORDER_CACHE_ENTRIES * ORDER_CACHE_ENTRIES;
  const uint32_t offset = orderStart + base * sizeof(uint16_t);
  const uint16_t slot = row - base;
  if (orderCacheCount == 0 || orderCacheOffset != offset) {
    orderCacheCount = 0;
    const auto count = static_cast<uint8_t>(std::min<uint16_t>(ORDER_CACHE_ENTRIES, head.bookCount - base));
    if (!readAt(offset, orderCache, count * sizeof(uint16_t))) return 0xFFFF;
    orderCacheOffset = offset;
    orderCacheCount = count;
  }
  const uint16_t ordinal = orderCache[slot];
  return ordinal < head.bookCount ? ordinal : 0xFFFF;
}

uint16_t LibraryIndexFile::ordinalForRow(const SortOrder order, const uint16_t row) {
  constexpr uint16_t NONE = 0xFFFF;
  if (!opened || row >= head.bookCount) return NONE;

  switch (order) {
    case SortOrder::TitleAsc:
      // The record section IS in title order, so this costs no storage and no
      // read at all.
      return row;
    case SortOrder::TitleDesc:
      return static_cast<uint16_t>(head.bookCount - 1 - row);
    case SortOrder::AuthorAsc:
    case SortOrder::AuthorDesc: {
      const uint16_t k = order == SortOrder::AuthorAsc ? row : static_cast<uint16_t>(head.bookCount - 1 - row);
      return readOrdinal(authorOrderOffset(head, 0), k);
    }
    case SortOrder::AddedAsc:
    case SortOrder::AddedDesc: {
      // arrivalOrder runs oldest first, so both directions share one on-disk
      // permutation.
      const uint16_t k = order == SortOrder::AddedAsc ? row : static_cast<uint16_t>(head.bookCount - 1 - row);
      return readOrdinal(arrivalOrderOffset(head, 0), k);
    }
    case SortOrder::SeriesAsc:
    case SortOrder::SeriesDesc: {
      const uint16_t k = order == SortOrder::SeriesAsc ? row : static_cast<uint16_t>(head.bookCount - 1 - row);
      return readOrdinal(seriesOrderOffset(head, 0), k);
    }
  }
  return NONE;
}

bool LibraryIndexFile::readSeriesRef(const uint16_t ordinal, ClixSeriesRef& out) {
  out.seriesId = CLIX_SERIES_NONE;
  out.seriesIndex = SERIES_INDEX_NONE;
  if (!opened || ordinal >= head.bookCount) return false;
  if (!readAt(seriesRefOffset(head, ordinal), &out, sizeof(out))) return false;
  // Same reasoning as the record clamp: this came off a card the user can write
  // to. An id past the table would seek outside the series section and render
  // whatever bytes it found as a heading.
  if ((out.seriesId == CLIX_SERIES_NONE && out.seriesIndex != SERIES_INDEX_NONE) ||
      (out.seriesId != CLIX_SERIES_NONE && out.seriesId >= head.seriesCount)) {
    readFailed = true;
    return false;
  }
  return true;
}

bool LibraryIndexFile::readSeries(const uint16_t seriesId, std::string& name, uint16_t& bookCount, uint32_t* identity) {
  name.clear();
  bookCount = 0;
  if (!opened || head.formatVersion != CLIX_FORMAT_VERSION || seriesId >= head.seriesCount) return false;

  ClixSeriesEntry entry{};
  if (!readAt(seriesEntryOffset(head, seriesId), &entry, sizeof(entry))) return false;
  if (entry.nameLen == 0 || entry.nameLen > CLIX_SERIES_NAME_BYTES || entry.bookCount == 0 ||
      entry.bookCount > head.knownSeriesCount) {
    readFailed = true;
    return false;
  }
  name.assign(entry.name, entry.nameLen);
  bookCount = entry.bookCount;
  if (identity) *identity = entry.identity;
  return true;
}

bool LibraryIndexFile::recentRowsFor(const BookIdentity* books, const size_t count, uint16_t* outRows) {
  constexpr uint16_t NONE = 0xFFFF;
  if (count > MAX_IDENTITY_LOOKUPS || (count != 0 && (!books || !outRows))) {
    LOG_ERR("LIBIDX", "invalid identity lookup arguments");
    return false;
  }
  for (size_t i = 0; i < count; i++) outRows[i] = NONE;
  if (!opened || count == 0 || head.bookCount == 0) return opened;

  constexpr size_t CHUNK_RECORDS = 32;  // 4096 bytes, the aligned-tile size
  auto chunk = makeUniqueNoThrow<uint8_t[]>(CHUNK_RECORDS * sizeof(ClixRecord));
  if (!chunk) {
    LOG_ERR("LIBIDX", "OOM: %u-byte lookup chunk", static_cast<unsigned>(CHUNK_RECORDS * sizeof(ClixRecord)));
    return false;
  }

  // Pass 1: record section, matching sizes in the chunk and confirming the few
  // size hits against the stored path hash.
  uint16_t ordinals[MAX_IDENTITY_LOOKUPS];
  for (size_t i = 0; i < count; i++) ordinals[i] = NONE;
  size_t unresolved = count;
  for (uint16_t base = 0; base < head.bookCount && unresolved > 0; base += CHUNK_RECORDS) {
    const uint16_t batch = std::min<uint16_t>(CHUNK_RECORDS, head.bookCount - base);
    if (!readAt(recordOffset(head, base), chunk.get(), batch * sizeof(ClixRecord))) return false;
    for (uint16_t r = 0; r < batch && unresolved > 0; r++) {
      // memcpy, not a cast: the chunk buffer has no alignment guarantee for the
      // record's 32-bit fields.
      ClixRecord record;
      memcpy(&record, chunk.get() + r * sizeof(ClixRecord), sizeof(ClixRecord));
      uint64_t hash = 0;
      bool hashRead = false;
      for (size_t i = 0; i < count; i++) {
        if (ordinals[i] != NONE) continue;
        // Size 0 means the caller could not stat the file (the index handle
        // may be the only reader the card allows); the hash alone decides.
        if (books[i].fileSize != 0 && books[i].fileSize != record.fileSize) continue;
        if (!hashRead) {
          if (!readPathHash(record, hash)) return false;
          hashRead = true;
        }
        if (books[i].pathHash == hash) {
          ordinals[i] = base + r;
          unresolved--;
        }
      }
    }
  }

  // Pass 2: arrival permutation, translating matched ordinals to ascending
  // rows.
  for (uint16_t base = 0; base < head.bookCount && unresolved < count; base += CHUNK_RECORDS * 2) {
    const uint16_t batch = std::min<uint16_t>(CHUNK_RECORDS * 2, head.bookCount - base);
    if (!readAt(arrivalOrderOffset(head, base), chunk.get(), batch * sizeof(uint16_t))) return false;
    for (uint16_t k = 0; k < batch; k++) {
      uint16_t ordinal;
      memcpy(&ordinal, chunk.get() + k * sizeof(uint16_t), sizeof(uint16_t));
      if (ordinal >= head.bookCount) {
        LOG_ERR("LIBIDX", "invalid arrival ordinal");
        readFailed = true;
        return false;
      }
      for (size_t i = 0; i < count; i++) {
        if (ordinals[i] != NONE && ordinals[i] == ordinal) {
          if (outRows[i] != NONE) {
            LOG_ERR("LIBIDX", "duplicate arrival identity");
            readFailed = true;
            return false;
          }
          outRows[i] = base + k;
        }
      }
    }
  }
  return true;
}

bool LibraryIndexFile::rowsForPaths(const SortOrder order, const PathIdentity* paths, const size_t count,
                                    uint16_t* outRows) {
  constexpr uint16_t NONE = 0xFFFF;
  if (count > MAX_PATH_LOOKUPS || (count && (!paths || !outRows))) {
    LOG_ERR("LIBIDX", "Invalid path lookup arguments");
    return false;
  }
  for (size_t i = 0; i < count; ++i) outRows[i] = NONE;
  if (!opened || count == 0 || head.bookCount == 0) return opened;
  uint32_t permutation = 0;
  bool descending = false;
  switch (order) {
    case SortOrder::TitleDesc:
      descending = true;
      [[fallthrough]];
    case SortOrder::TitleAsc:
      break;
    case SortOrder::AuthorDesc:
      descending = true;
      [[fallthrough]];
    case SortOrder::AuthorAsc:
      permutation = authorOrderOffset(head, 0);
      break;
    case SortOrder::AddedDesc:
      descending = true;
      [[fallthrough]];
    case SortOrder::AddedAsc:
      permutation = arrivalOrderOffset(head, 0);
      break;
    case SortOrder::SeriesDesc:
      descending = true;
      [[fallthrough]];
    case SortOrder::SeriesAsc:
      permutation = seriesOrderOffset(head, 0);
      break;
    default:
      LOG_ERR("LIBIDX", "Invalid path lookup order");
      return false;
  }
  uint8_t nameLengths[MAX_PATH_LOOKUPS]{};
  for (size_t i = 0; i < count; ++i) {
    const auto path = paths[i].path;
    const size_t length = path.size() - (path.find_last_of('/') + 1);
    if (path.empty() || path.front() != '/' || path.find('\0') != std::string_view::npos || length == 0 ||
        length > 255) {
      LOG_ERR("LIBIDX", "Invalid lookup path");
      return false;
    }
    nameLengths[i] = static_cast<uint8_t>(length);
  }
  // A single transient chunk serves both record and permutation scans. It is
  // fallible and exceeds the stack budget; no library-sized table is retained.
  constexpr size_t CHUNK_BYTES = 4096;
  auto chunk = makeUniqueNoThrow<uint8_t[]>(CHUNK_BYTES);
  if (!chunk) {
    LOG_ERR("LIBIDX", "OOM: path lookup chunk");
    return false;
  }
  uint16_t ordinals[MAX_PATH_LOOKUPS] = {NONE, NONE};
  size_t unresolved = count;
  std::string candidate;
  constexpr uint16_t RECORDS = CHUNK_BYTES / sizeof(ClixRecord);
  for (int pass = 0; pass < 2 && unresolved; ++pass) {
    for (uint16_t base = 0; base < head.bookCount && unresolved; base += RECORDS) {
      const uint16_t batch = std::min<uint16_t>(RECORDS, head.bookCount - base);
      if (!readAt(recordOffset(head, base), chunk.get(), batch * sizeof(ClixRecord))) return false;
      for (uint16_t r = 0; r < batch && unresolved; ++r) {
        ClixRecord record;
        memcpy(&record, chunk.get() + r * sizeof(record), sizeof(record));
        uint64_t hash = 0;
        bool hashRead = false, pathRead = false;
        for (size_t i = 0; i < count; ++i) {
          const bool sizeMatches = paths[i].fileSize == 0 || record.fileSize == paths[i].fileSize;
          if (ordinals[i] != NONE || record.nameLen != nameLengths[i] || sizeMatches != (pass == 0)) continue;
          if (!hashRead) {
            if (!readPathHash(record, hash)) return false;
            hashRead = true;
          }
          if (hash != paths[i].pathHash) continue;
          if (!pathRead) {
            if (!readPath(record, candidate)) return false;
            pathRead = true;
          }
          if (candidate != paths[i].path) continue;
          ordinals[i] = base + r;
          --unresolved;
        }
      }
    }
  }
  if (unresolved == count) return true;
  uint16_t rows[MAX_PATH_LOOKUPS] = {NONE, NONE};
  if (permutation == 0) {
    std::copy_n(ordinals, count, rows);
  } else {
    constexpr uint16_t PERMUTATIONS = CHUNK_BYTES / sizeof(uint16_t);
    for (uint16_t base = 0; base < head.bookCount; base += PERMUTATIONS) {
      const uint16_t batch = std::min<uint16_t>(PERMUTATIONS, head.bookCount - base);
      if (!readAt(permutation + base * sizeof(uint16_t), chunk.get(), batch * sizeof(uint16_t))) return false;
      for (uint16_t k = 0; k < batch; ++k) {
        uint16_t ordinal;
        memcpy(&ordinal, chunk.get() + k * sizeof(ordinal), sizeof(ordinal));
        if (ordinal >= head.bookCount) {
          LOG_ERR("LIBIDX", "Invalid lookup ordinal");
          return false;
        }
        for (size_t i = 0; i < count; ++i) {
          if (ordinals[i] == NONE || ordinals[i] != ordinal) continue;
          if (rows[i] != NONE) {
            LOG_ERR("LIBIDX", "Duplicate lookup ordinal");
            return false;
          }
          rows[i] = base + k;
        }
      }
    }
  }
  for (size_t i = 0; i < count; ++i) {
    if (ordinals[i] != NONE && rows[i] == NONE) {
      LOG_ERR("LIBIDX", "Missing lookup ordinal");
      return false;
    }
  }
  for (size_t i = 0; i < count; ++i) {
    outRows[i] = rows[i] == NONE || !descending ? rows[i] : static_cast<uint16_t>(head.bookCount - 1 - rows[i]);
  }
  return true;
}

bool LibraryIndexFile::readRecord(const uint16_t ordinal, ClixRecord& out) {
  if (!opened || ordinal >= head.bookCount) return false;
  if (!readAt(recordOffset(head, ordinal), &out, sizeof(out))) return false;

  // Clamp here, at the single point every record enters the program. These
  // lengths come off an SD card that the user can write to and that can rot: a
  // foldLen of 255 against a 96-byte field sends a string_view 159 bytes past the
  // end of the record, and callers build views from them without looking. Fixing
  // it at each call site would mean fixing it again at the next one.
  out.foldLen = static_cast<uint8_t>(std::min<size_t>(out.foldLen, CLIX_FOLD_BYTES));
  out.authorKeyLen = static_cast<uint8_t>(std::min<size_t>(out.authorKeyLen, CLIX_AUTHOR_KEY_BYTES));
  if (out.metadataStatus > CLIX_METADATA_FAILED) return false;
  // nameOff is u32 and every reader adds a length to it before comparing against
  // the section size. A forged value near the top of the range wraps that sum and
  // passes the bounds check it was supposed to fail, so it is rejected here
  // instead — the one place that can, before any arithmetic touches it.
  if (out.nameOff > head.nameLen) {
    out.nameLen = 0;
    out.nameOff = 0;
  }
  return true;
}

bool LibraryIndexFile::readName(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened || record.nameLen == 0) return false;
  if (record.nameOff > head.nameLen || sizeof(uint64_t) > head.nameLen - record.nameOff ||
      record.nameLen > head.nameLen - record.nameOff - sizeof(uint64_t))
    return false;
  out.resize(record.nameLen);
  return readAt(head.nameStart + record.nameOff + sizeof(uint64_t), out.data(), record.nameLen);
}

bool LibraryIndexFile::readPathHash(const ClixRecord& record, uint64_t& out) {
  out = 0;
  if (!opened) return false;
  if (record.nameOff > head.nameLen || sizeof(out) > head.nameLen - record.nameOff) {
    readFailed = true;
    return false;
  }
  return readAt(head.nameStart + record.nameOff, &out, sizeof(out));
}

bool LibraryIndexFile::readBlobField(const ClixRecord& record, const uint8_t field, std::string& out) {
  out.clear();
  if (!opened || record.nameLen == 0) return false;
  if (record.nameOff > head.nameLen || sizeof(uint64_t) > head.nameLen - record.nameOff ||
      record.nameLen > head.nameLen - record.nameOff - sizeof(uint64_t))
    return false;

  uint32_t at = record.nameOff + sizeof(uint64_t) + record.nameLen;
  for (uint8_t i = 0; i <= field; i++) {
    if (at >= head.nameLen) return false;
    uint8_t len = 0;
    if (!readAt(head.nameStart + at, &len, sizeof(len))) return false;
    ++at;
    if (len > head.nameLen - at) return false;
    if (i == field) {
      out.resize(len);
      return len == 0 || readAt(head.nameStart + at, out.data(), len);
    }
    at += len;
  }
  return false;
}

bool LibraryIndexFile::readAuthor(const ClixRecord& record, std::string& out) { return readBlobField(record, 0, out); }

// The book's own title, after the name and the author. Absent (length 0) for a
// book that never told us one, in which case the caller shows the filename.
bool LibraryIndexFile::readTitle(const ClixRecord& record, std::string& out) {
  return readBlobField(record, 1, out) && !out.empty();
}

bool LibraryIndexFile::readMetadata(const ClixRecord& record, std::string& title, std::string& author,
                                    const uint8_t authorField) {
  title.clear();
  author.clear();
  if (!opened || record.nameLen == 0 || record.nameOff > head.nameLen ||
      sizeof(uint64_t) > head.nameLen - record.nameOff ||
      record.nameLen > head.nameLen - record.nameOff - sizeof(uint64_t))
    return false;

  uint8_t buffer[64];
  uint32_t bufferStart = 0;
  size_t buffered = 0;
  const auto copyBytes = [&](uint32_t at, void* dst, size_t len) {
    auto* out = static_cast<uint8_t*>(dst);
    while (len > 0) {
      if (at < bufferStart || at - bufferStart >= buffered) {
        if (len >= sizeof(buffer)) return readAt(head.nameStart + at, out, len);
        bufferStart = at;
        buffered = std::min<size_t>(sizeof(buffer), head.nameLen - at);
        if (buffered == 0 || !readAt(head.nameStart + at, buffer, buffered)) return false;
      }
      const size_t count = std::min<size_t>(len, buffered - (at - bufferStart));
      memcpy(out, buffer + (at - bufferStart), count);
      out += count;
      at += count;
      len -= count;
    }
    return true;
  };

  uint32_t at = record.nameOff + sizeof(uint64_t) + record.nameLen;
  const uint8_t lastField = std::max<uint8_t>(1, authorField);
  for (uint8_t field = 0; field <= lastField; ++field) {
    uint8_t len = 0;
    if (at >= head.nameLen || !copyBytes(at, &len, sizeof(len))) break;
    ++at;
    if (len > head.nameLen - at) break;
    std::string* value = field == 1 ? &title : field == authorField ? &author : nullptr;
    if (value) {
      value->resize(len);
      if (!copyBytes(at, value->data(), len)) break;
    }
    at += len;
    if (field == lastField) return true;
  }
  title.clear();
  author.clear();
  return false;
}

bool LibraryIndexFile::readTitleAndAuthor(const ClixRecord& record, std::string& title, std::string& author) {
  return readMetadata(record, title, author, 0);
}

bool LibraryIndexFile::readTitleAndSourceAuthor(const ClixRecord& record, std::string& title, std::string& author) {
  return readMetadata(record, title, author, 2);
}

bool LibraryIndexFile::readSourceAuthor(const ClixRecord& record, std::string& out) {
  return readBlobField(record, 2, out);
}

bool LibraryIndexFile::readPath(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened || record.folderId >= head.folderCount) return false;

  // Batch nearby folder lengths without retaining a folder table. Read-ahead
  // stays within the current aligned sector and the folder section.
  uint8_t buffer[64];
  uint32_t bufferStart = 0;
  size_t buffered = 0;
  uint32_t offset = head.folderStart;
  const uint32_t folderEnd = head.folderStart + head.folderLen;
  for (uint16_t i = 0; i <= record.folderId; i++) {
    if (offset >= folderEnd) return false;
    if (offset < bufferStart || offset - bufferStart >= buffered) {
      bufferStart = offset;
      buffered = std::min<size_t>(sizeof(buffer), std::min(folderEnd - offset, CLIX_ALIGN - offset % CLIX_ALIGN));
      if (!readAt(offset, buffer, buffered)) return false;
    }
    const uint8_t pathLen = buffer[offset - bufferStart];
    if (pathLen == 0) return false;
    if (pathLen > folderEnd - offset - 1u) return false;
    if (i == record.folderId) {
      std::string dir(pathLen, '\0');
      if (!readAt(offset + 1, dir.data(), pathLen)) return false;
      std::string name;
      if (!readName(record, name)) return false;
      out = joinLibraryPath(dir, name);
      return true;
    }
    offset += 1u + pathLen;
    if (offset >= folderEnd) return false;
  }
  return false;
}

}  // namespace library
