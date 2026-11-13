#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Records what the hash would cover; "toString" is the byte count.
class MD5Builder {
 public:
  void begin() { added = 0; }
  void add(const uint8_t*, const size_t length) { added += length; }
  void add(const char* text) { added += std::char_traits<char>::length(text); }
  void calculate() {}
  std::string toString() const { return std::to_string(added); }

 private:
  size_t added = 0;
};
