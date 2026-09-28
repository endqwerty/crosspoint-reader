#include "SectionPageReader.h"

#include <HalStorage.h>
#include <Logging.h>

#include <limits>

#include "Page.h"

namespace {
bool readOffset(HalFile& file, const uint32_t position, uint32_t& value) {
  return file.seek(position) && file.read(reinterpret_cast<uint8_t*>(&value), sizeof(value)) == sizeof(value);
}
}  // namespace

std::unique_ptr<Page> SectionPageReader::load(const std::string& filePath, const int page) {
  if (page < 0) {
    LOG_ERR("SCT", "Invalid page index: %d", page);
    return nullptr;
  }
  HalFile file;
  if (!Storage.openFileForRead("SCT", filePath, file)) return nullptr;

  const auto fileSize = file.size();
  uint32_t lutOffset = 0;
  if (fileSize < HEADER_SIZE || !readOffset(file, HEADER_SIZE - sizeof(uint32_t) * 5, lutOffset)) {
    LOG_ERR("SCT", "Failed to read section header");
    return nullptr;
  }
  const uint64_t pageEntry = static_cast<uint64_t>(lutOffset) + sizeof(uint32_t) * static_cast<uint64_t>(page);
  uint32_t pagePos = 0;
  if (lutOffset < HEADER_SIZE || pageEntry > std::numeric_limits<uint32_t>::max() - sizeof(uint32_t) ||
      pageEntry + sizeof(uint32_t) > fileSize || !readOffset(file, static_cast<uint32_t>(pageEntry), pagePos) ||
      pagePos < HEADER_SIZE || pagePos >= lutOffset) {
    LOG_ERR("SCT", "Invalid section page offset: %d", page);
    return nullptr;
  }

  // The visible offset shares this handle so saving progress needs no second open.
  uint32_t visibleLutOffset = 0;
  uint32_t visibleTextOffset = 0;
  if (readOffset(file, HEADER_SIZE - sizeof(uint32_t), visibleLutOffset)) {
    const uint64_t visibleEntry =
        static_cast<uint64_t>(visibleLutOffset) + sizeof(uint32_t) * static_cast<uint64_t>(page);
    if (visibleLutOffset >= HEADER_SIZE && visibleEntry <= std::numeric_limits<uint32_t>::max() - sizeof(uint32_t) &&
        visibleEntry + sizeof(uint32_t) <= fileSize) {
      if (!readOffset(file, static_cast<uint32_t>(visibleEntry), visibleTextOffset)) visibleTextOffset = 0;
    }
  }
  if (!file.seek(pagePos)) {
    LOG_ERR("SCT", "Failed to seek section page: %d", page);
    return nullptr;
  }
  auto decoded = Page::deserialize(file);
  if (decoded) decoded->visibleTextOffset = visibleTextOffset;
  return decoded;
}
