#pragma once
#include <cstddef>
#include <cstdint>
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* data, size_t count) {
    size_t written = 0;
    while (written < count && write(data[written])) written++;
    return written;
  }
};
