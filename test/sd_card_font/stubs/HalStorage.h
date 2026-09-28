#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

inline std::vector<uint8_t> sdFontTestFile;
struct SdFontTestIo {
  size_t opens = 0;
  size_t seeks = 0;
  size_t reads = 0;
  size_t bytes = 0;
};
inline SdFontTestIo sdFontTestIo;
inline size_t& sdFontTestReads = sdFontTestIo.reads;
// One-based operation number; zero disables fault injection.
inline size_t sdFontTestFailOpen = 0;
inline size_t sdFontTestFailSeek = 0;
inline size_t sdFontTestShortRead = 0;

struct SdFontTestEsp {
  size_t freeHeap = 200 * 1024;
  size_t largestBlock = 200 * 1024;
  size_t getFreeHeap() const { return freeHeap; }
  size_t getMaxAllocHeap() const { return largestBlock; }
};
inline SdFontTestEsp ESP;
inline uint32_t millis() { return 0; }

class HalFile {
 public:
  bool seekSet(size_t position) {
    ++sdFontTestIo.seeks;
    if (sdFontTestIo.seeks == sdFontTestFailSeek || !opened_ || position > sdFontTestFile.size()) return false;
    position_ = position;
    return true;
  }
  int read(void* output, size_t count) {
    ++sdFontTestIo.reads;
    if (!opened_ || position_ > sdFontTestFile.size()) return 0;
    count = std::min(count, sdFontTestFile.size() - position_);
    if (sdFontTestIo.reads == sdFontTestShortRead && count > 0) --count;
    std::memcpy(output, sdFontTestFile.data() + position_, count);
    position_ += count;
    sdFontTestIo.bytes += count;
    return static_cast<int>(count);
  }
  void close() { opened_ = false; }
  size_t position() const { return opened_ ? position_ : 0; }
  size_t size() const { return opened_ ? sdFontTestFile.size() : 0; }
  void open() {
    opened_ = true;
    position_ = 0;
  }

 private:
  size_t position_ = 0;
  bool opened_ = false;
};

class HalStorage {
 public:
  bool openFileForRead(const char*, const char*, HalFile& file) {
    ++sdFontTestIo.opens;
    if (sdFontTestIo.opens == sdFontTestFailOpen) return false;
    file.open();
    return true;
  }
};
inline HalStorage Storage;
