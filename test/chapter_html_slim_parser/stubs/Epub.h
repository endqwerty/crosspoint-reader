#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Epub {
 public:
  std::vector<uint8_t> imageHeader;

  template <typename Output>
  bool readItemContentsToStream(const std::string&, Output& output, size_t, bool allowEarlyStop = false) const {
    if (imageHeader.empty()) return false;
    const auto written = output.write(imageHeader.data(), imageHeader.size());
    return written == imageHeader.size() || allowEarlyStop;
  }
};
