#include "ZipFile.h"

#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

struct ZipInflateCtx {
  HalFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

namespace {
constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;
constexpr size_t DIRECTORY_HEADER_BYTES = 46;
uint16_t readLe16(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8); }
uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// RAII zip: opens the zip if not already open, closes on destruction only if
// it performed the open.  Removes the wasOpen/close boilerplate from every method.
class ScopedOpenClose final {
 public:
  [[nodiscard]] explicit ScopedOpenClose(ZipFile& zf) : zf(zf), needsClose(!zf.isOpen()) {
    if (needsClose) ok = zf.open();
  }
  ~ScopedOpenClose() {
    if (needsClose && ok) zf.close();
  }
  ScopedOpenClose(const ScopedOpenClose&) = delete;
  ScopedOpenClose& operator=(const ScopedOpenClose&) = delete;
  ScopedOpenClose(ScopedOpenClose&&) = delete;
  ScopedOpenClose& operator=(ScopedOpenClose&&) = delete;
  explicit operator bool() const { return ok || !needsClose; }

 private:
  ZipFile& zf;
  bool needsClose = false;
  bool ok = true;  // true when zip was already open (no open() call needed)
};

size_t zipFillCallback(void* vctx, const uint8_t** data) {
  auto* ctx = static_cast<ZipInflateCtx*>(vctx);
  if (ctx->fileRemaining == 0) return 0;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const int result = ctx->file->read(ctx->readBuf, toRead);
  // HalFile::read() returns a negative int on error. Treat it as end-of-stream
  // rather than letting the negative-to-size_t conversion underflow fileRemaining
  // and report a huge bytesRead, which would have the inflate library read past
  // the end of readBuf.
  if (result < 0) {
    LOG_ERR("ZIP", "Failed to read compressed data: %d", result);
    return 0;
  }
  const size_t bytesRead = static_cast<size_t>(result);
  ctx->fileRemaining -= bytesRead;

  *data = ctx->readBuf;
  return bytesRead;
}
}  // namespace

bool ZipFile::seekDirectory(const uint32_t position) {
  if (position < zipDetails.centralDirOffset || position > zipDetails.centralDirEnd || !file.seek(position)) {
    LOG_ERR("ZIP", "Failed to seek central directory");
    return false;
  }
  return true;
}

bool ZipFile::readDirectoryEntry(DirectoryEntry& entry, char (&name)[DIRECTORY_NAME_CAPACITY]) {
  const size_t start = file.position();
  uint8_t header[DIRECTORY_HEADER_BYTES];
  if (start < zipDetails.centralDirOffset || start > zipDetails.centralDirEnd ||
      zipDetails.centralDirEnd - start < sizeof(header) || file.read(header, sizeof(header)) != sizeof(header) ||
      readLe32(header) != 0x02014b50) {
    LOG_ERR("ZIP", "Invalid or unreadable central-directory entry");
    return false;
  }
  entry.stat.method = readLe16(header + 10);
  entry.crc32 = readLe32(header + 16);
  entry.stat.compressedSize = readLe32(header + 20);
  entry.stat.uncompressedSize = readLe32(header + 24);
  entry.nameLength = readLe16(header + 28);
  entry.stat.localHeaderOffset = readLe32(header + 42);
  const uint64_t next =
      static_cast<uint64_t>(start) + sizeof(header) + entry.nameLength + readLe16(header + 30) + readLe16(header + 32);
  if (next > zipDetails.centralDirEnd) {
    LOG_ERR("ZIP", "Central-directory entry exceeds declared bounds");
    return false;
  }
  if (entry.nameLength <= sizeof(name) && file.read(name, entry.nameLength) != entry.nameLength) {
    LOG_ERR("ZIP", "Failed to read central-directory filename");
    return false;
  }
  return file.position() == next || seekDirectory(static_cast<uint32_t>(next));
}

bool ZipFile::loadAllFileStatSlims() {
  const ScopedOpenClose zip{*this};
  if (!zip || !loadZipDetails() || !seekDirectory(zipDetails.centralDirOffset)) return false;
  char itemName[DIRECTORY_NAME_CAPACITY];
  fileStatSlimCache.clear();
  fileStatSlimCache.reserve(zipDetails.totalEntries);
  for (uint32_t i = 0; i < zipDetails.totalEntries; ++i) {
    DirectoryEntry entry;
    if (!readDirectoryEntry(entry, itemName)) {
      fileStatSlimCache.clear();
      return false;
    }
    if (entry.nameLength <= sizeof(itemName)) {
      fileStatSlimCache.emplace(std::string(itemName, entry.nameLength), entry.stat);
    }
  }
  lastCentralDirPos = zipDetails.centralDirOffset;
  lastCentralDirIndex = 0;
  lastCentralDirPosValid = true;
  return true;
}

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  if (!fileStatSlimCache.empty()) {
    const auto it = fileStatSlimCache.find(filename);
    if (it == fileStatSlimCache.end()) return false;
    *fileStat = it->second;
    return true;
  }
  const ScopedOpenClose zip{*this};
  if (!zip || !loadZipDetails()) return false;
  uint32_t ordinal = lastCentralDirPosValid ? lastCentralDirIndex : 0;
  const uint32_t start =
      lastCentralDirPosValid && ordinal < zipDetails.totalEntries ? lastCentralDirPos : zipDetails.centralDirOffset;
  if (ordinal >= zipDetails.totalEntries) ordinal = 0;
  if (!seekDirectory(start)) return false;
  const std::string_view requested(filename);
  char itemName[DIRECTORY_NAME_CAPACITY];
  for (uint32_t visited = 0; visited < zipDetails.totalEntries; ++visited) {
    if (ordinal == zipDetails.totalEntries) {
      if (!seekDirectory(zipDetails.centralDirOffset)) return false;
      ordinal = 0;
    }
    DirectoryEntry entry;
    if (!readDirectoryEntry(entry, itemName)) return false;
    ++ordinal;
    if (entry.nameLength <= sizeof(itemName) && std::string_view(itemName, entry.nameLength) == requested) {
      *fileStat = entry.stat;
      lastCentralDirPos = file.position();
      lastCentralDirIndex = static_cast<uint16_t>(ordinal);
      lastCentralDirPosValid = true;
      return true;
    }
  }
  return false;
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const ScopedOpenClose zip{*this};
  if (!zip) return -1;

  constexpr auto localHeaderSize = 30;

  uint8_t pLocalHeader[localHeaderSize];
  const uint64_t fileOffset = fileStat.localHeaderOffset;

  if (!file.seek(fileOffset)) {
    LOG_ERR("ZIP", "Failed to seek local header");
    return -1;
  }
  const size_t read = file.read(pLocalHeader, localHeaderSize);

  if (read != localHeaderSize) {
    LOG_ERR("ZIP", "Something went wrong reading the local header");
    return -1;
  }

  if (readLe32(pLocalHeader) != 0x04034b50) {
    LOG_ERR("ZIP", "Not a valid zip file header");
    return -1;
  }

  const uint16_t filenameLength = pLocalHeader[26] + (pLocalHeader[27] << 8);
  const uint16_t extraOffset = pLocalHeader[28] + (pLocalHeader[29] << 8);
  return fileOffset + localHeaderSize + filenameLength + extraOffset;
}

bool ZipFile::loadZipDetails() {
  if (zipDetails.isSet) {
    return true;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  const size_t fileSize = file.size();
  if (fileSize < 22) {
    LOG_ERR("ZIP", "File too small to be a valid zip");
    return false;  // Minimum EOCD size is 22 bytes
  }

  // We scan the last 1KB (or the whole file if smaller) for the EOCD signature
  // 0x06054b50 is stored as 0x50, 0x4b, 0x05, 0x06 in little-endian
  const int scanRange = fileSize > 1024 ? 1024 : fileSize;
  const auto buffer = makeUniqueNoThrow<uint8_t[]>(scanRange);
  if (!buffer) {
    LOG_ERR("ZIP", "Failed to allocate memory for EOCD scan buffer");
    return false;
  }

  if (!file.seek(fileSize - scanRange) || file.read(buffer.get(), scanRange) != scanRange) {
    LOG_ERR("ZIP", "Failed to read EOCD scan buffer");
    return false;
  }

  // Scan backwards for the signature
  int foundOffset = -1;
  for (int i = scanRange - 22; i >= 0; i--) {
    constexpr uint32_t signature = 0x06054b50;
    uint32_t candidate;
    memcpy(&candidate, buffer.get() + i, sizeof(candidate));
    if (candidate == signature && i + 22 + readLe16(buffer.get() + i + 20) == scanRange) {
      foundOffset = i;
      break;
    }
  }

  if (foundOffset == -1) {
    LOG_ERR("ZIP", "EOCD signature not found in zip file");
    return false;
  }

  const uint8_t* end = buffer.get() + foundOffset;
  const uint64_t endPosition = fileSize - scanRange + foundOffset;
  const uint32_t offset = readLe32(end + 16);
  const uint32_t length = readLe32(end + 12);
  const uint16_t entries = readLe16(end + 10);
  if (readLe16(end + 4) != 0 || readLe16(end + 6) != 0 || readLe16(end + 8) != entries ||
      static_cast<uint64_t>(offset) + length > endPosition || static_cast<uint64_t>(offset) + length > UINT32_MAX ||
      static_cast<uint64_t>(entries) * DIRECTORY_HEADER_BYTES > length ||
      endPosition + 22 + readLe16(end + 20) != fileSize) {
    LOG_ERR("ZIP", "Invalid or unsupported central-directory trailer");
    return false;
  }
  zipDetails.centralDirOffset = offset;
  zipDetails.centralDirEnd = offset + length;
  zipDetails.totalEntries = entries;
  zipDetails.isSet = true;
  return true;
}

bool ZipFile::open() {
  if (!Storage.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    // Explicit close() required: member variable persists beyond function scope
    file.close();
  }
  lastCentralDirPos = 0;
  lastCentralDirIndex = 0;
  lastCentralDirPosValid = false;
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(std::span<const SizeTarget> targets, std::span<uint32_t> sizes) {
  if (targets.empty()) return 0;
  const ScopedOpenClose zip{*this};
  if (!zip || !loadZipDetails() || !seekDirectory(zipDetails.centralDirOffset)) return -1;
  int matched = 0;
  char itemName[DIRECTORY_NAME_CAPACITY];
  for (uint32_t i = 0; i < zipDetails.totalEntries; ++i) {
    DirectoryEntry entry;
    if (!readDirectoryEntry(entry, itemName)) return -1;
    if (entry.nameLength > sizeof(itemName)) continue;
    const uint64_t hash = fnvHash64(itemName, entry.nameLength);
    const SizeTarget key{hash, entry.nameLength, 0};
    auto it = std::lower_bound(targets.begin(), targets.end(), key, [](const SizeTarget& a, const SizeTarget& b) {
      return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
    });
    while (it != targets.end() && it->hash == hash && it->len == entry.nameLength) {
      if (it->index < sizes.size()) {
        sizes[it->index] = entry.stat.uncompressedSize;
        ++matched;
      }
      ++it;
    }
    if (matched >= static_cast<int>(targets.size())) break;
  }
  return matched;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  const ScopedOpenClose zip{*this};
  if (!zip) return nullptr;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return nullptr;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return nullptr;

  if (!file.seek(fileOffset)) {
    LOG_ERR("ZIP", "Failed to seek file payload");
    return nullptr;
  }

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  if (inflatedDataSize > SIZE_MAX - static_cast<size_t>(trailingNullByte)) {
    LOG_ERR("ZIP", "Output buffer size overflows address range");
    return nullptr;
  }
  const size_t dataSize = static_cast<size_t>(inflatedDataSize) + static_cast<size_t>(trailingNullByte);
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    LOG_ERR("ZIP", "Failed to allocate memory for output buffer (%zu bytes)", dataSize);
    return nullptr;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const size_t dataRead = file.read(data, inflatedDataSize);

    if (dataRead != inflatedDataSize) {
      LOG_ERR("ZIP", "Failed to read data");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(1024));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      free(data);
      return nullptr;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = 1024;

    // One-shot mode: `data` holds the entire output, so back-references
    // resolve inside it and no 32KB window is allocated.
    InflateStream inflate;
    if (!inflate.init(false)) {
      LOG_ERR("ZIP", "Failed to init inflate stream for %s", filename);
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    inflate.setFill(zipFillCallback, &ctx);

    if (!inflate.read(data, inflatedDataSize)) {
      LOG_ERR("ZIP", "Failed to inflate file");
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    free(fileReadBuffer);

    // Continue out of block with data set
  } else {
    LOG_ERR("ZIP", "Unsupported compression method");
    free(data);
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize, const bool allowEarlyStop) {
  if (chunkSize == 0) {
    LOG_ERR("ZIP", "Zero-sized stream buffer");
    return false;
  }
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  if (!file.seek(fileOffset)) {
    LOG_ERR("ZIP", "Failed to seek file payload");
    return false;
  }
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for buffer");
      return false;
    }

    size_t remaining = inflatedDataSize;
    while (remaining > 0) {
      const int result = file.read(buffer, remaining < chunkSize ? remaining : chunkSize);
      if (result <= 0) {
        LOG_ERR("ZIP", "Could not read more bytes");
        free(buffer);
        return false;
      }

      const auto dataRead = static_cast<size_t>(result);
      if (out.write(buffer, dataRead) != dataRead) {
        free(buffer);
        if (allowEarlyStop) return true;  // sink has what it needs
        LOG_ERR("ZIP", "Failed to write all output bytes to stream");
        return false;
      }
      remaining -= dataRead;
    }

    free(buffer);
    return true;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      return false;
    }

    auto* outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!outputBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for output buffer");
      free(fileReadBuffer);
      return false;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = chunkSize;

    InflateStream inflate;
    if (!inflate.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate stream for %s", filename);
      free(outputBuffer);
      free(fileReadBuffer);
      return false;
    }
    inflate.setFill(zipFillCallback, &ctx);

    bool success = false;
    size_t totalProduced = 0;

    while (true) {
      size_t produced;
      const InflateStream::Status status = inflate.readAtMost(outputBuffer, chunkSize, &produced);

      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }

      if (produced > 0) {
        if (out.write(outputBuffer, produced) != produced) {
          if (allowEarlyStop) {
            success = true;  // sink has what it needs
          } else {
            LOG_ERR("ZIP", "Failed to write all output bytes to stream");
          }
          break;
        }
      }

      if (status == InflateStream::Status::Done) {
        if (totalProduced != static_cast<size_t>(inflatedDataSize)) {
          LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(inflatedDataSize),
                  totalProduced);
          break;
        }
        LOG_DBG("ZIP", "Decompressed %d bytes into %d bytes", deflatedDataSize, inflatedDataSize);
        success = true;
        break;
      }

      if (status == InflateStream::Status::Error) {
        LOG_ERR("ZIP", "Decompression failed");
        break;
      }
      // InflateStream::Status::Ok: output buffer full, continue
    }

    free(outputBuffer);
    free(fileReadBuffer);
    return success;  // inflate destructor frees the decompressor state + window
  }

  LOG_ERR("ZIP", "Unsupported compression method");
  return false;
}
