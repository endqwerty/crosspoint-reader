#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace parser_test {

inline thread_local size_t openFileCount = 0;

class ScopedReadFailure {
 public:
  explicit ScopedReadFailure(size_t successfulReads = 0, int errorResult = -1)
      : remaining_(successfulReads), errorResult_(errorResult), previous_(active_) {
    active_ = this;
  }
  ~ScopedReadFailure() { active_ = previous_; }
  ScopedReadFailure(const ScopedReadFailure&) = delete;
  ScopedReadFailure& operator=(const ScopedReadFailure&) = delete;
  size_t failures() const { return failures_; }
  static int errorResult() { return active_->errorResult_; }

  static bool shouldFail() {
    if (!active_ || active_->failures_ != 0) return false;
    if (active_->remaining_ != 0) {
      --active_->remaining_;
      return false;
    }
    ++active_->failures_;
    return true;
  }

 private:
  size_t remaining_;
  int errorResult_;
  size_t failures_ = 0;
  ScopedReadFailure* previous_;
  static inline thread_local ScopedReadFailure* active_ = nullptr;
};

class ScopedWriteFailure {
 public:
  ScopedWriteFailure() : previous_(active_) { active_ = this; }
  ~ScopedWriteFailure() { active_ = previous_; }
  ScopedWriteFailure(const ScopedWriteFailure&) = delete;
  ScopedWriteFailure& operator=(const ScopedWriteFailure&) = delete;
  size_t failures() const { return failures_; }
  static bool shouldFail() {
    if (!active_ || active_->failures_) return false;
    ++active_->failures_;
    return true;
  }

 private:
  size_t failures_ = 0;
  ScopedWriteFailure* previous_;
  static inline thread_local ScopedWriteFailure* active_ = nullptr;
};

}  // namespace parser_test

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  bool open(const char* path, const char* mode) {
    close();
    file_ = std::fopen(path, mode);
    if (file_) ++parser_test::openFileCount;
    return file_ != nullptr;
  }
  int available() const { return file_ ? static_cast<int>(size() - position()) : 0; }
  int read(void* buffer, size_t count) {
    if (!file_) return -1;
    if (parser_test::ScopedReadFailure::shouldFail()) return parser_test::ScopedReadFailure::errorResult();
    const auto bytes = std::fread(buffer, 1, count, file_);
    return std::ferror(file_) ? -1 : static_cast<int>(bytes);
  }
  size_t write(const void* buffer, size_t count) {
    if (parser_test::ScopedWriteFailure::shouldFail()) return 0;
    return file_ ? std::fwrite(buffer, 1, count, file_) : 0;
  }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  bool flush() { return file_ && std::fflush(file_) == 0; }
  bool seek(size_t offset) { return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_SET) == 0; }
  bool seekCur(size_t offset) { return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_CUR) == 0; }
  bool close() {
    if (!file_) return false;
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    --parser_test::openFileCount;
    return ok;
  }
  bool isOpen() const { return file_ != nullptr; }
  explicit operator bool() const { return isOpen(); }
  size_t position() const { return file_ ? static_cast<size_t>(std::ftell(file_)) : 0; }
  size_t size() const {
    if (!file_) return 0;
    const long offset = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, offset, SEEK_SET);
    return end > 0 ? static_cast<size_t>(end) : 0;
  }

 private:
  std::FILE* file_ = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "rb"); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "wb"); }
  bool exists(const char* path) const {
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
  }
  bool remove(const std::string& path) { return std::remove(path.c_str()) == 0; }
  bool rename(const char* from, const char* to) { return std::rename(from, to) == 0; }
};

#define Storage HalStorage::getInstance()

inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
