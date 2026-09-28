#pragma once
#include <Print.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace cache_test {
inline std::map<std::string, std::vector<uint8_t>> files;
inline int reads = 0, seeks = 0, opens = 0, handles = 0, writes = 0;
inline int failRead = -1, failSeek = -1, shortRead = -1;
inline bool failOpen = false;
inline int ioOperations = 0, persistentFailureAt = -1;
inline bool ioFailed() {
  if (++ioOperations > 256) throw std::runtime_error("I/O operation budget exceeded: no progress after card failure");
  return persistentFailureAt >= 0 && ioOperations >= persistentFailureAt;
}
inline void resetFaults() {
  reads = seeks = opens = writes = 0;
  ioOperations = 0;
  persistentFailureAt = -1;
  failRead = failSeek = shortRead = -1;
  failOpen = false;
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
    if (!bytes) return false;
    bytes = nullptr;
    offset = 0;
    --cache_test::handles;
    return true;
  }
  explicit operator bool() const { return bytes; }
  bool isOpen() const { return bytes; }
  size_t position() const { return offset; }
  size_t size() const { return bytes ? bytes->size() : 0; }
  int available() const { return static_cast<int>(size() - std::min(size(), offset)); }
  bool seekCur(size_t n) { return seek(offset + n); }
  bool seek(size_t at) {
    if (cache_test::ioFailed()) return false;
    if (cache_test::seeks++ == cache_test::failSeek || !bytes) return false;
    offset = at;
    return true;
  }
  int read(void* to, size_t n) {
    if (cache_test::ioFailed()) return -1;
    const int call = cache_test::reads++;
    if (call == cache_test::failRead || !bytes) return -1;
    if (call == cache_test::shortRead) n /= 2;
    if (offset >= bytes->size()) return 0;
    n = std::min(n, bytes->size() - offset);
    std::memcpy(to, bytes->data() + offset, n);
    offset += n;
    return static_cast<int>(n);
  }
  size_t write(const void* from, size_t n) {
    ++cache_test::writes;
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
  bool remove(const char* p) { return cache_test::files.erase(p) != 0; }
};
inline StorageFake Storage;
