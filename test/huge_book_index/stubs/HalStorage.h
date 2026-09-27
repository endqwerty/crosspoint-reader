#pragma once

// In-memory SD card. Its buffers are test storage, not device heap, so they are
// allocated outside the heap cap.
#include <HeapCap.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct StorageFaults {
  size_t tocWriteOpens = 0, tocCloses = 0;
  size_t failTocOpenAt = 0, failTocCloseAt = 0;
};
inline StorageFaults storageFaults;

struct StorageMetrics {
  size_t reads = 0, readBytes = 0, seeks = 0, writes = 0;
};
inline StorageMetrics storageMetrics;

struct TestFile {
  std::vector<uint8_t> bytes;
};

class HalFile {
 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  ~HalFile() { close(); }

  explicit operator bool() const { return static_cast<bool>(data); }
  size_t position() const { return pos; }
  size_t size() const { return data ? data->bytes.size() : 0; }
  uint64_t fileSize64() const { return size(); }
  int available() const { return static_cast<int>(size() - std::min(size(), pos)); }
  bool seek(const size_t p) {
    if (!data || p > size()) return false;
    ++storageMetrics.seeks;
    pos = p;
    return true;
  }
  int read(void* dst, const size_t n) {
    if (!data) return -1;
    const size_t got = pos < size() ? std::min(n, size() - pos) : 0;
    if (got) memcpy(dst, data->bytes.data() + pos, got);
    ++storageMetrics.reads;
    storageMetrics.readBytes += got;
    pos += got;
    return static_cast<int>(got);
  }
  size_t write(const void* src, const size_t n) {
    if (!data) return 0;
    ++storageMetrics.writes;
    heapcap::Untracked guard;
    if (pos + n > size()) data->bytes.resize(pos + n);
    memcpy(data->bytes.data() + pos, src, n);
    pos += n;
    return n;
  }
  bool close() {
    heapcap::Untracked guard;
    const bool failed = data && tocOutput && ++storageFaults.tocCloses == storageFaults.failTocCloseAt;
    data.reset();
    pos = 0;
    tocOutput = false;
    return !failed;
  }

 private:
  friend struct TestStorage;
  std::shared_ptr<TestFile> data;
  size_t pos = 0;
  bool tocOutput = false;
};

struct TestStorage {
  std::map<std::string, std::shared_ptr<TestFile>> files;

  bool openFileForRead(const char* module, const char* path, HalFile& out) {
    heapcap::Untracked guard;
    return openFileForRead(module, std::string(path), out);
  }
  bool openFileForWrite(const char* module, const char* path, HalFile& out) {
    heapcap::Untracked guard;
    return openFileForWrite(module, std::string(path), out);
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& out) {
    heapcap::Untracked guard;
    out.close();
    const auto it = files.find(path);
    if (it == files.end()) return false;
    out.data = it->second;
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    heapcap::Untracked guard;
    out.close();
    const bool toc = path.ends_with("/toc.bin.tmp");
    if (toc && ++storageFaults.tocWriteOpens == storageFaults.failTocOpenAt) return false;
    out.tocOutput = toc;
    out.data = files[path] = std::make_shared<TestFile>();
    return true;
  }
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool rename(const char* from, const char* to) {
    heapcap::Untracked guard;
    if (!files.contains(from) || files.contains(to)) return false;
    auto node = files.extract(from);
    node.key() = to;
    files.insert(std::move(node));
    return true;
  }
  bool removeDir(const char* path) {
    heapcap::Untracked guard;
    const std::string prefix = std::string(path) + "/";
    for (auto it = files.begin(); it != files.end();) {
      if (it->first.starts_with(prefix))
        it = files.erase(it);
      else
        ++it;
    }
    return true;
  }
  bool remove(const char* path) {
    heapcap::Untracked guard;
    return files.erase(path) != 0;
  }
};

inline TestStorage Storage;
