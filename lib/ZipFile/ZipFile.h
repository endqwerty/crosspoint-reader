#pragma once
#include <HalStorage.h>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

class ZipFile {
 public:
  struct FileStatSlim {
    uint16_t method;             // Compression method
    uint32_t compressedSize;     // Compressed size
    uint32_t uncompressedSize;   // Uncompressed size
    uint32_t localHeaderOffset;  // Offset of local file header
  };

  struct ZipDetails {
    uint32_t centralDirOffset;
    uint32_t centralDirEnd;
    uint16_t totalEntries;
    bool isSet;
  };

  // Target for batch uncompressed size lookup (sorted by hash, then len)
  struct SizeTarget {
    uint64_t hash;   // FNV-1a 64-bit hash of normalized path
    uint16_t len;    // Length of path for collision reduction
    uint16_t index;  // Caller's index (e.g. spine index)
  };

  // FNV-1a 64-bit hash computed from char buffer (no std::string allocation)
  static uint64_t fnvHash64(const char* s, size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
      hash ^= static_cast<uint8_t>(s[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

 private:
  const std::string& filePath;
  HalFile file;
  ZipDetails zipDetails = {0, 0, 0, false};
  std::unordered_map<std::string, FileStatSlim> fileStatSlimCache;

  // Cursor for sequential central-dir scanning optimization
  uint32_t lastCentralDirPos = 0;
  uint16_t lastCentralDirIndex = 0;
  bool lastCentralDirPosValid = false;

  static constexpr size_t DIRECTORY_NAME_CAPACITY = 255;
  struct DirectoryEntry {
    FileStatSlim stat;
    uint32_t crc32;
    uint16_t nameLength;
  };
  bool seekDirectory(uint32_t position);
  // Names are length-delimited; names beyond capacity are skipped without truncation.
  bool readDirectoryEntry(DirectoryEntry& entry, char (&name)[DIRECTORY_NAME_CAPACITY]);

  bool loadFileStatSlim(const char* filename, FileStatSlim* fileStat);
  long getDataOffset(const FileStatSlim& fileStat);
  bool loadZipDetails();

 public:
  explicit ZipFile(const std::string& filePath) : filePath(filePath) {}
  ~ZipFile() = default;
  // Zip file can be opened and closed by hand in order to allow for quick calculation of inflated file size
  // It is NOT recommended to pre-open it for any kind of inflation due to memory constraints
  bool isOpen() const { return !!file; }
  bool open();
  bool close();
  bool loadAllFileStatSlims();
  bool getInflatedFileSize(const char* filename, size_t* size);
  // Batch lookup: scan ZIP central dir once and fill sizes for matching targets.
  // targets must be sorted by (hash, len). sizes[target.index] receives uncompressedSize.
  // Returns the number of targets matched, or -1 on I/O/format failure.
  // On failure, sizes may contain partial results and must not be used.
  int fillUncompressedSizes(std::span<const SizeTarget> targets, std::span<uint32_t> sizes);
  // Due to the memory required to run each of these, it is recommended to not preopen the zip file for multiple
  // These functions will open and close the zip as needed
  uint8_t* readFileToMemory(const char* filename, size_t* size = nullptr, bool trailingNullByte = false);
  // allowEarlyStop: a short write from `out` is treated as the sink asking to
  // stop (returns true) instead of a write failure — used by header probes
  // that only need the first bytes of an entry.
  bool readFileToStream(const char* filename, Print& out, size_t chunkSize, bool allowEarlyStop = false);

  template <typename F>
  bool enumerateFilePaths(F&& callback) {
    if (!fileStatSlimCache.empty()) {
      for (const auto& entry : fileStatSlimCache) {
        callback(std::string_view{entry.first});
      }
      return true;
    }

    return enumerateFileEntries([&callback](std::string_view path, uint32_t, uint32_t) { callback(path); });
  }

  // Callback receives (path, crc32, compressedSize) for each central-directory
  // entry. Always scans the central directory: the slim-stat cache does not
  // hold CRCs.
  template <typename F>
  bool enumerateFileEntries(F&& callback) {
    const bool wasOpen = isOpen();
    if (!wasOpen && !open()) {
      return false;
    }

    const auto finish = [this, wasOpen](const bool ok) {
      if (!wasOpen) close();
      return ok;
    };
    if (!loadZipDetails() || !seekDirectory(zipDetails.centralDirOffset)) return finish(false);
    char itemName[DIRECTORY_NAME_CAPACITY];
    for (uint32_t i = 0; i < zipDetails.totalEntries; ++i) {
      DirectoryEntry entry;
      if (!readDirectoryEntry(entry, itemName)) return finish(false);
      if (entry.nameLength <= sizeof(itemName)) {
        callback(std::string_view{itemName, entry.nameLength}, entry.crc32, entry.stat.compressedSize);
      }
    }
    return finish(true);
  }
};
