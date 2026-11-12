#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace cache_test {
inline std::map<std::string, std::vector<uint8_t>> files;
inline int reads = 0, seeks = 0, opens = 0, handles = 0, writes = 0;
inline int failRead = -1, failSeek = -1, failWrite = -1, shortRead = -1;
inline int injectedReads = 0, injectedWrites = 0;
inline int closes = 0, renames = 0, removes = 0;
inline int failClose = -1, failRename = -1, failRenameAgain = -1, failRemove = -1;
inline uint64_t reportedSize = 0;
inline bool failOpen = false;
inline void resetFaults() {
  reads = seeks = opens = writes = 0;
  closes = renames = removes = 0;
  failClose = failRename = failRenameAgain = failRemove = -1;
  failRead = failSeek = failWrite = shortRead = -1;
  injectedReads = injectedWrites = 0;
  failOpen = false;
  reportedSize = 0;
}
}  // namespace cache_test
class HalFile {
  std::vector<uint8_t>* bytes = nullptr;
  size_t offset = 0;

 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  ~HalFile() { close(); }
  bool open(const std::string& path, bool write) {
    close();
    ++cache_test::opens;
    if (cache_test::failOpen) return false;
    if (!write && !cache_test::files.contains(path)) return false;
    bytes = &cache_test::files[path];
    offset = 0;
    if (write) bytes->clear();
    ++cache_test::handles;
    return true;
  }
  bool close() {
    if (!bytes) return true;
    const bool ok = cache_test::closes++ != cache_test::failClose;
    bytes = nullptr;
    offset = 0;
    --cache_test::handles;
    return ok;
  }
  explicit operator bool() const { return bytes; }
  bool isOpen() const { return bytes; }
  size_t position() const { return offset; }
  int available() const { return static_cast<int>(size() - std::min(size(), offset)); }
  size_t size() const { return bytes ? bytes->size() : 0; }
  uint64_t fileSize64() const { return cache_test::reportedSize ? cache_test::reportedSize : size(); }
  bool seek(size_t at) {
    if (cache_test::seeks++ == cache_test::failSeek || !bytes) return false;
    offset = at;
    return true;
  }
  int read(void* to, size_t n) {
    const int call = cache_test::reads++;
    if (call == cache_test::failRead) {
      ++cache_test::injectedReads;
      return -1;
    }
    if (!bytes) return -1;
    if (offset >= bytes->size()) return 0;
    n = std::min(n, bytes->size() - offset);
    if (call == cache_test::shortRead && n) {
      --n;
      ++cache_test::injectedReads;
    }
    std::memcpy(to, bytes->data() + offset, n);
    offset += n;
    return static_cast<int>(n);
  }
  size_t write(const void* from, size_t n) {
    if (cache_test::writes++ == cache_test::failWrite) {
      ++cache_test::injectedWrites;
      return 0;
    }
    if (!bytes) return 0;
    if (offset + n > bytes->size()) bytes->resize(offset + n);
    if (n) std::memcpy(bytes->data() + offset, from, n);
    offset += n;
    return n;
  }
};
struct StorageFake {
  bool openFileForRead(const char*, const std::string& p, HalFile& f) { return f.open(p, false); }
  bool openFileForWrite(const char*, const std::string& p, HalFile& f) { return f.open(p, true); }
  bool exists(const char* p) { return cache_test::files.contains(p); }
  bool remove(const char* p) {
    if (cache_test::removes++ == cache_test::failRemove) return false;
    return cache_test::files.erase(p) != 0;
  }
  bool rename(const char* from, const char* to) {
    const int call = cache_test::renames++;
    if (call == cache_test::failRename || call == cache_test::failRenameAgain || !cache_test::files.contains(from) ||
        cache_test::files.contains(to))
      return false;
    auto node = cache_test::files.extract(from);
    node.key() = to;
    cache_test::files.insert(std::move(node));
    return true;
  }
};
inline StorageFake Storage;
