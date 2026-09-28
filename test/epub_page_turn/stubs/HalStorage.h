#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace epub_page_test {
struct IoCounts {
  size_t opens = 0;
  size_t seeks = 0;
  size_t reads = 0;
  size_t bytes = 0;
  size_t writes = 0;
  size_t writtenBytes = 0;
  size_t closes = 0;
  size_t removes = 0;
  size_t renames = 0;
  size_t injectedFailures = 0;
};
inline IoCounts io;
struct IoFaults {
  // One-based call numbers; zero leaves that operation unaffected.
  size_t failReadCall = 0;
  size_t shortReadCall = 0;
  size_t failWriteCall = 0;
  size_t failSeekCall = 0;
  size_t failCloseCall = 0;
  size_t failRemoveCall = 0;
  size_t failRenameCall = 0;
  size_t failRenameCall2 = 0;
  size_t writeBudget = std::numeric_limits<size_t>::max();
  bool failOpen = false;
  bool failRemove = false;
  bool failRename = false;
};
inline IoFaults faults;
inline std::array<std::vector<uint8_t>, 4> files;
inline std::array<bool, 4> fileExists{true, true, false};
inline int fileIndex(const char* path) {
  if (std::strcmp(path, "section") == 0) return 0;
  if (std::strcmp(path, "page1") == 0) return 1;
  if (std::strcmp(path, "section.part") == 0) return 2;
  if (std::strcmp(path, "section.bak") == 0) return 3;
  return -1;
}
}  // namespace epub_page_test

class HalFile {
 public:
  void open(std::vector<uint8_t>& bytes) {
    bytes_ = &bytes;
    position_ = 0;
  }
  bool seek(size_t position) {
    ++epub_page_test::io.seeks;
    if (epub_page_test::io.seeks == epub_page_test::faults.failSeekCall) {
      ++epub_page_test::io.injectedFailures;
      return false;
    }
    if (!bytes_ || position > bytes_->size()) return false;
    position_ = position;
    return true;
  }
  int read(void* output, size_t count) {
    ++epub_page_test::io.reads;
    if (epub_page_test::io.reads == epub_page_test::faults.failReadCall) {
      ++epub_page_test::io.injectedFailures;
      return -1;
    }
    if (!bytes_) return 0;
    if (count && epub_page_test::io.reads == epub_page_test::faults.shortReadCall) {
      ++epub_page_test::io.injectedFailures;
      --count;
    }
    count = std::min(count, bytes_->size() - position_);
    if (count) std::memcpy(output, bytes_->data() + position_, count);
    position_ += count;
    epub_page_test::io.bytes += count;
    return static_cast<int>(count);
  }
  size_t write(const void* input, size_t count) {
    ++epub_page_test::io.writes;
    if (epub_page_test::io.writes == epub_page_test::faults.failWriteCall) {
      ++epub_page_test::io.injectedFailures;
      return 0;
    }
    if (!bytes_) return 0;
    const bool incomplete = count > epub_page_test::faults.writeBudget;
    if (incomplete) ++epub_page_test::io.injectedFailures;
    count = std::min(count, epub_page_test::faults.writeBudget);
    if (position_ + count > bytes_->size()) bytes_->resize(position_ + count);
    if (count) std::memcpy(bytes_->data() + position_, input, count);
    position_ += count;
    epub_page_test::faults.writeBudget -= count;
    epub_page_test::io.writtenBytes += count;
    // SdFat reports zero on error even if an earlier part advanced the cursor.
    return incomplete ? 0 : count;
  }
  size_t size() const { return bytes_ ? bytes_->size() : 0; }
  size_t position() const { return position_; }
  explicit operator bool() const { return bytes_ != nullptr; }
  bool close() {
    ++epub_page_test::io.closes;
    const bool opened = bytes_ != nullptr;
    bytes_ = nullptr;
    if (epub_page_test::io.closes == epub_page_test::faults.failCloseCall) {
      ++epub_page_test::io.injectedFailures;
      return false;
    }
    return opened;
  }

 private:
  std::vector<uint8_t>* bytes_ = nullptr;
  size_t position_ = 0;
};

class HalStorage {
 public:
  bool openFileForRead(const char* module, const std::string& path, HalFile& file) {
    return openFileForRead(module, path.c_str(), file);
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    ++epub_page_test::io.opens;
    const int index = epub_page_test::fileIndex(path);
    if (epub_page_test::faults.failOpen) {
      ++epub_page_test::io.injectedFailures;
      return false;
    }
    if (index < 0 || !epub_page_test::fileExists[index]) return false;
    file.open(epub_page_test::files[index]);
    return true;
  }
  bool exists(const char* path) const {
    const int index = epub_page_test::fileIndex(path);
    return index >= 0 && epub_page_test::fileExists[index];
  }
  bool remove(const char* path) {
    ++epub_page_test::io.removes;
    const int index = epub_page_test::fileIndex(path);
    if (epub_page_test::faults.failRemove || epub_page_test::io.removes == epub_page_test::faults.failRemoveCall) {
      ++epub_page_test::io.injectedFailures;
      return false;
    }
    if (index < 0 || !epub_page_test::fileExists[index]) return false;
    epub_page_test::fileExists[index] = false;
    epub_page_test::files[index].clear();
    return true;
  }
  bool rename(const char* from, const char* to) {
    ++epub_page_test::io.renames;
    const int source = epub_page_test::fileIndex(from);
    const int destination = epub_page_test::fileIndex(to);
    if (epub_page_test::faults.failRename || epub_page_test::io.renames == epub_page_test::faults.failRenameCall ||
        epub_page_test::io.renames == epub_page_test::faults.failRenameCall2) {
      ++epub_page_test::io.injectedFailures;
      return false;
    }
    if (source < 0 || destination < 0 || !epub_page_test::fileExists[source] || epub_page_test::fileExists[destination])
      return false;
    epub_page_test::files[destination] = std::move(epub_page_test::files[source]);
    epub_page_test::fileExists[source] = false;
    epub_page_test::fileExists[destination] = true;
    return true;
  }
};
inline HalStorage Storage;
