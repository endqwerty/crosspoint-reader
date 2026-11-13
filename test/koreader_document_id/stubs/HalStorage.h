#pragma once

#include <cstddef>
#include <string>
#include <vector>

// A file whose reads return scripted results, one per read() call.
class HalFile {
 public:
  size_t fileSize() { return size; }
  bool seekSet(size_t) { return true; }
  int read(void*, const size_t count) {
    if (next >= results.size()) return static_cast<int>(count);
    return results[next++];
  }

  size_t size = 0;
  std::vector<int> results;
  size_t next = 0;
};

class HalStorage {
 public:
  bool openFileForRead(const char*, const std::string&, HalFile& file) {
    file = scripted;
    return true;
  }

  HalFile scripted;
};

inline HalStorage Storage;
