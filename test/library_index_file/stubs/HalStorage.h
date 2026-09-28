#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

class HalFile {
  friend class HalStorage;

 public:
  bool close() {
    if (!hasImpl) {
      invalidCloses++;
      return false;
    }
    open = false;
    return true;
  }

  bool isOpen() const { return hasImpl && open; }
  int read(void* dst, size_t len) {
    ++readCalls;
    if (captureReads) readRanges.emplace_back(position_, len);
    if (std::exchange(failRead, false)) return -1;
    if (failReadAfter >= 0 && failReadAfter-- == 0) return -1;
    if (!isOpen() || !data) return -1;
    len = std::min(len, data->size() - std::min(position_, data->size()));
    len = std::min(len, std::exchange(nextReadLimit, SIZE_MAX));
    if (shortReadAfter >= 0 && shortReadAfter-- == 0 && len) --len;
    if (len == 0) return 0;
    std::memcpy(dst, data->data() + position_, len);
    position_ += len;
    bytesRead += len;
    return static_cast<int>(len);
  }
  size_t position() const { return isOpen() ? position_ : 0; }
  size_t size() const { return isOpen() && data ? data->size() : 0; }
  uint64_t fileSize64() { return data ? data->size() : 0; }
  bool seekSet(size_t offset) {
    ++seekCalls;
    if (std::exchange(failSeek, false)) return false;
    if (failSeekAfter >= 0 && failSeekAfter-- == 0) return false;
    if (!isOpen() || !data || offset > data->size()) return false;
    position_ = offset;
    return true;
  }

  static void resetInvalidCloseCount() { invalidCloses = 0; }
  static int invalidCloseCount() { return invalidCloses; }
  static inline bool failRead = false;
  static inline bool failSeek = false;
  static inline size_t nextReadLimit = SIZE_MAX;
  static inline int failReadAfter = -1;
  static inline int shortReadAfter = -1;
  static inline int failSeekAfter = -1;
  static inline bool captureReads = false;
  static inline std::vector<std::pair<size_t, size_t>> readRanges;
  static inline size_t readCalls = 0;
  static inline size_t seekCalls = 0;
  static inline size_t bytesRead = 0;
  static void resetIoCounters() {
    readCalls = seekCalls = bytesRead = 0;
    readRanges.clear();
  }
  static void resetFaults() {
    failRead = failSeek = false;
    nextReadLimit = SIZE_MAX;
    failReadAfter = shortReadAfter = failSeekAfter = -1;
    captureReads = false;
    resetIoCounters();
  }

 private:
  bool hasImpl = false;
  bool open = false;
  const std::vector<uint8_t>* data = nullptr;
  size_t position_ = 0;
  static inline int invalidCloses = 0;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file.hasImpl = true;
    file.open = path == filePath;
    file.data = file.open ? &fileData : nullptr;
    file.position_ = 0;
    return file.open;
  }

  void setFile(const std::string& path, std::vector<uint8_t> data) {
    HalFile::resetFaults();
    filePath = path;
    fileData = std::move(data);
  }

  void clearFile() {
    HalFile::resetFaults();
    filePath.clear();
    fileData.clear();
  }

  void truncateFile(size_t size) { fileData.resize(std::min(size, fileData.size())); }

 private:
  std::string filePath;
  std::vector<uint8_t> fileData;
};

#define Storage HalStorage::getInstance()
