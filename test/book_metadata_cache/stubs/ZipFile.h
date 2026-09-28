#pragma once
#include <cstdint>
#include <span>
#include <string>
class ZipFile {
 public:
  struct SizeTarget {
    uint64_t hash;
    uint16_t len;
    uint16_t index;
  };
  explicit ZipFile(const std::string&) {}
  bool open() { return true; }
  void close() {}
  bool getInflatedFileSize(const char*, size_t* size) {
    *size = 100;
    return true;
  }
  int fillUncompressedSizes(std::span<const SizeTarget> t, std::span<uint32_t> sizes) {
    for (const auto& item : t) sizes[item.index] = 100;
    return static_cast<int>(t.size());
  }
  static uint64_t fnvHash64(const char*, size_t) { return 0; }
};
