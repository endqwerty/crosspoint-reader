#pragma once

#include <cstdint>
#include <memory>
#include <string>

class Page;

// Reads committed/partial section pages independently of the live chapter builder.
class SectionPageReader {
 public:
  static constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) +
                                          sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) +
                                          sizeof(bool) + sizeof(bool) + sizeof(uint8_t) + sizeof(bool) +
                                          sizeof(uint32_t) * 5 + sizeof(int8_t) + sizeof(uint8_t) + sizeof(uint8_t);

  static std::unique_ptr<Page> load(const std::string& filePath, int page);
};
