#pragma once

#include <cstdint>

namespace ReferencePages {

constexpr uint32_t BYTES_PER_PAGE = 2048;

constexpr uint32_t count(const uint32_t bookSize) {
  if (bookSize == 0) return 0;
  const uint32_t rounded = bookSize / BYTES_PER_PAGE + (bookSize % BYTES_PER_PAGE >= BYTES_PER_PAGE / 2);
  return rounded > 0 ? rounded : 1;
}

constexpr uint32_t pageFor(const double progress01, const uint32_t pageCount) {
  if (pageCount == 0) return 0;
  if (!(progress01 > 0)) return 1;
  if (progress01 >= 1) return pageCount;
  uint32_t page = static_cast<uint32_t>(progress01 * pageCount) + 1;
  // Compare boundaries directly to avoid division/multiplication roundoff.
  if (page < pageCount && progress01 >= static_cast<double>(page) / pageCount) ++page;
  if (page > 1 && progress01 < static_cast<double>(page - 1) / pageCount) --page;
  return page > pageCount ? pageCount : page;
}

// Page holding the last text of a screen page that ends at progress01. A page
// boundary belongs to the earlier page, so a screen page reached through
// startOffset() never shows an earlier page than the one requested.
constexpr uint32_t pageForEnd(const double progress01, const uint32_t pageCount) {
  if (pageCount == 0) return 0;
  if (!(progress01 > 0)) return 1;
  if (progress01 >= 1) return pageCount;
  const double position = progress01 * pageCount;
  uint32_t page = static_cast<uint32_t>(position);
  if (static_cast<double>(page) < position) ++page;
  return page < 1 ? 1 : (page > pageCount ? pageCount : page);
}

constexpr uint32_t startOffset(const uint32_t page, const uint32_t bookSize, const uint32_t pageCount) {
  if (bookSize == 0 || pageCount == 0) return 0;
  const uint32_t clampedPage = page < 1 ? 1 : (page > pageCount ? pageCount : page);
  // Round up to the first byte inside the requested page's interval.
  return static_cast<uint32_t>((static_cast<uint64_t>(clampedPage - 1) * bookSize + pageCount - 1) / pageCount);
}

}  // namespace ReferencePages
