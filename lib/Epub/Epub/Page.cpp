#include "Page.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <cstdint>
#include <cstring>
#include <limits>

namespace {

bool writeFixedString(HalFile& file, const char* text, const size_t fieldSize) {
  static constexpr uint8_t ZERO_PADDING[FOOTNOTE_HREF_LEN] = {};
  const size_t length = strnlen(text, fieldSize);
  if (length == fieldSize || fieldSize > sizeof(ZERO_PADDING)) return false;
  return serialization::writeBytesChecked(file, text, length + 1) &&
         serialization::writeBytesChecked(file, ZERO_PADDING, fieldSize - length - 1);
}

template <typename Predicate>
void renderFilteredPageElements(const std::vector<std::unique_ptr<PageElement>>& elements, GfxRenderer& renderer,
                                const int fontId, const int xOffset, const int yOffset, Predicate&& predicate) {
  for (const auto& element : elements) {
    if (predicate(*element)) {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

}  // namespace

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(HalFile& file) {
  if (!block || !serialization::writePodChecked(file, xPos) || !serialization::writePodChecked(file, yPos)) {
    LOG_ERR("PGE", "Failed to write line metadata");
    return false;
  }
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(HalFile& file) {
  int16_t position[2];
  if (!serialization::readBytesChecked(file, position, sizeof(position))) {
    LOG_ERR("PGE", "Failed to read line metadata");
    return nullptr;
  }

  auto tb = TextBlock::deserialize(file);
  if (!tb) {
    LOG_ERR("PGE", "Deserialization failed: null TextBlock");
    return nullptr;
  }

  auto line = makeUniqueNoThrow<PageLine>(std::move(tb), position[0], position[1]);
  if (!line) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageLine");
    return nullptr;
  }
  return line;
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  // Images don't use fontId or text rendering
  imageBlock->render(renderer, xPos + xOffset, yPos + yOffset);
}

void PageImage::renderPlaceholder(GfxRenderer& renderer, const int xOffset, const int yOffset) const {
  imageBlock->renderPlaceholder(renderer, xPos + xOffset, yPos + yOffset);
}

bool PageImage::serialize(HalFile& file) {
  if (!imageBlock || !serialization::writePodChecked(file, xPos) || !serialization::writePodChecked(file, yPos)) {
    LOG_ERR("PGE", "Failed to write image metadata");
    return false;
  }
  return imageBlock->serialize(file);
}

std::unique_ptr<PageImage> PageImage::deserialize(HalFile& file) {
  int16_t position[2];
  if (!serialization::readBytesChecked(file, position, sizeof(position))) {
    LOG_ERR("PGE", "Failed to read image metadata");
    return nullptr;
  }

  auto ib = ImageBlock::deserialize(file);
  if (!ib) {
    LOG_ERR("PGE", "Deserialization failed: null ImageBlock");
    return nullptr;
  }
  auto image = makeUniqueNoThrow<PageImage>(std::move(ib), position[0], position[1]);
  if (!image) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageImage");
    return nullptr;
  }
  return image;
}

void PageHorizontalRule::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  (void)fontId;
  if (width == 0 || thickness == 0) {
    return;
  }

  renderer.drawLine(xPos + xOffset, yPos + yOffset, xPos + xOffset + width - 1, yPos + yOffset, thickness, true);
}

bool PageHorizontalRule::serialize(HalFile& file) {
  if (width == 0 || thickness == 0 || !serialization::writePodChecked(file, xPos) ||
      !serialization::writePodChecked(file, yPos) || !serialization::writePodChecked(file, width) ||
      !serialization::writePodChecked(file, thickness)) {
    LOG_ERR("PGE", "Failed to write horizontal rule metadata");
    return false;
  }
  return true;
}

std::unique_ptr<PageHorizontalRule> PageHorizontalRule::deserialize(HalFile& file) {
  uint8_t metadata[2 * sizeof(int16_t) + sizeof(uint16_t) + sizeof(uint8_t)];
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint16_t width = 0;
  if (!serialization::readBytesChecked(file, metadata, sizeof(metadata))) {
    LOG_ERR("PGE", "Failed to read horizontal rule metadata");
    return nullptr;
  }
  memcpy(&xPos, metadata, sizeof(xPos));
  memcpy(&yPos, metadata + sizeof(int16_t), sizeof(yPos));
  memcpy(&width, metadata + 2 * sizeof(int16_t), sizeof(width));
  const uint8_t thickness = metadata[sizeof(metadata) - 1];

  if (width == 0 || thickness == 0) {
    LOG_ERR("PGE", "Deserialization failed: invalid horizontal rule metadata (width=%u thickness=%u)", width,
            thickness);
    return nullptr;
  }

  auto rule = makeUniqueNoThrow<PageHorizontalRule>(width, thickness, xPos, yPos);
  if (!rule) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageHorizontalRule");
    return nullptr;
  }
  return rule;
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset, [](const PageElement&) { return true; });
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset,
                             [](const PageElement& element) { return element.getTag() == TAG_PageImage; });
}

void Page::renderWithImagePlaceholders(GfxRenderer& renderer, const int fontId, const int xOffset,
                                       const int yOffset) const {
  for (const auto& element : elements) {
    if (element->getTag() == TAG_PageImage) {
      static_cast<const PageImage&>(*element).renderPlaceholder(renderer, xOffset, yOffset);
    } else {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

bool Page::serialize(HalFile& file) const {
  if (elements.size() > std::numeric_limits<uint16_t>::max() ||
      !serialization::writePodChecked(file, static_cast<uint16_t>(elements.size()))) {
    LOG_ERR("PGE", "Failed to write page element count");
    return false;
  }
  for (const auto& element : elements) {
    if (!element || !serialization::writePodChecked(file, static_cast<uint8_t>(element->getTag()))) {
      LOG_ERR("PGE", "Failed to write page element tag");
      return false;
    }
    if (!element->serialize(file)) return false;
  }

  const auto fnCount = static_cast<uint16_t>(std::min<size_t>(footnotes.size(), MAX_FOOTNOTES_PER_PAGE));
  if (!serialization::writePodChecked(file, fnCount)) {
    LOG_ERR("PGE", "Failed to write footnote count");
    return false;
  }
  for (uint16_t i = 0; i < fnCount; i++) {
    const auto& fn = footnotes[i];
    if (!writeFixedString(file, fn.number, sizeof(fn.number)) || !writeFixedString(file, fn.href, sizeof(fn.href))) {
      LOG_ERR("PGE", "Failed to write footnote");
      return false;
    }
  }

  const auto linkCount = static_cast<uint16_t>(std::min<size_t>(links.size(), MAX_LINKS_PER_PAGE));
  if (!serialization::writePodChecked(file, linkCount)) {
    LOG_ERR("PGE", "Failed to write link count");
    return false;
  }
  for (uint16_t i = 0; i < linkCount; i++) {
    const auto& link = links[i];
    if (link.href[0] == '\0' || link.width <= 0 || link.height <= 0 ||
        !writeFixedString(file, link.href, sizeof(link.href)) || !serialization::writePodChecked(file, link.x) ||
        !serialization::writePodChecked(file, link.y) || !serialization::writePodChecked(file, link.width) ||
        !serialization::writePodChecked(file, link.height)) {
      LOG_ERR("PGE", "Failed to write link %u", i);
      return false;
    }
  }
  return true;
}

size_t Page::cacheBudgetBytes() const {
  constexpr size_t ALLOCATION_ALLOWANCE = 64;
  size_t total = sizeof(Page) + ALLOCATION_ALLOWANCE;
  const auto chargeAllocation = [&total](const size_t count, const size_t elementSize, const size_t allocations = 1) {
    if (count == 0) return true;
    const size_t allowance = allocations * ALLOCATION_ALLOWANCE;
    if (total > SIZE_MAX - allowance || count > (SIZE_MAX - total - allowance) / elementSize) return false;
    total += count * elementSize + allowance;
    return true;
  };
  if (!chargeAllocation(elements.capacity(), sizeof(elements[0])) ||
      !chargeAllocation(footnotes.capacity(), sizeof(footnotes[0])) ||
      !chargeAllocation(links.capacity(), sizeof(links[0])))
    return SIZE_MAX;

  for (const auto& element : elements) {
    if (!element) return SIZE_MAX;
    switch (element->getTag()) {
      case TAG_PageLine: {
        if (!chargeAllocation(1, sizeof(PageLine), 2)) return SIZE_MAX;
        const auto& block = static_cast<const PageLine&>(*element).getBlock();
        if (!block) return SIZE_MAX;
        // Each line owns its block; include the full block storage.
        const size_t blockBytes = block->cacheBudgetBytes();
        if (blockBytes > SIZE_MAX - total) return SIZE_MAX;
        total += blockBytes;
        break;
      }
      case TAG_PageHorizontalRule:
        if (!chargeAllocation(1, sizeof(PageHorizontalRule), 2)) return SIZE_MAX;
        break;
      default:
        return SIZE_MAX;
    }
  }
  return total;
}

std::unique_ptr<Page> Page::deserialize(HalFile& file) {
  auto page = makeUniqueNoThrow<Page>();
  if (!page) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate Page");
    return nullptr;
  }

  uint16_t count = 0;
  if (!serialization::readPodChecked(file, count)) {
    LOG_ERR("PGE", "Failed to read page element count");
    return nullptr;
  }

  // Reserve up front so a page load costs one allocation for the element vector
  // instead of a grow-copy-free cycle every doubling. `count` is untrusted (it
  // comes straight off the SD cache), so clamp it: a real page holds a few dozen
  // elements, while a corrupt header could ask for 65535 * sizeof(unique_ptr) and
  // abort() on the failed allocation (vector's operator new is throwing, and this
  // firmware builds with -fno-exceptions). Under-reserving is harmless -- the
  // push_back path below still grows normally.
  static constexpr uint16_t RESERVE_CAP = 256;
  page->elements.reserve(std::min(count, RESERVE_CAP));

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag = 0;
    if (!serialization::readPodChecked(file, tag)) {
      LOG_ERR("PGE", "Failed to read page element tag");
      return nullptr;
    }

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      if (!pl) {
        return nullptr;
      }
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      if (!pi) {
        return nullptr;
      }
      page->elements.push_back(std::move(pi));
    } else if (tag == TAG_PageHorizontalRule) {
      auto rule = PageHorizontalRule::deserialize(file);
      if (!rule) {
        return nullptr;
      }
      page->elements.push_back(std::move(rule));
    } else {
      LOG_ERR("PGE", "Deserialization failed: Unknown tag %u", tag);
      return nullptr;
    }
  }

  // Deserialize footnotes
  uint16_t fnCount = 0;
  if (!serialization::readPodChecked(file, fnCount) || fnCount > MAX_FOOTNOTES_PER_PAGE) {
    LOG_ERR("PGE", "Invalid footnote count %u", fnCount);
    return nullptr;
  }
  page->footnotes.resize(fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    auto& entry = page->footnotes[i];
    if (!serialization::readBytesChecked(file, entry.number, sizeof(entry.number)) ||
        !serialization::readBytesChecked(file, entry.href, sizeof(entry.href)) ||
        !memchr(entry.number, '\0', sizeof(entry.number)) || !memchr(entry.href, '\0', sizeof(entry.href))) {
      LOG_ERR("PGE", "Failed to read footnote %u", i);
      return nullptr;
    }
  }

  uint16_t linkCount = 0;
  if (!serialization::readPodChecked(file, linkCount) || linkCount > MAX_LINKS_PER_PAGE) {
    LOG_ERR("PGE", "Invalid link count %u", linkCount);
    return nullptr;
  }
  page->links.resize(linkCount);
  for (uint16_t i = 0; i < linkCount; i++) {
    auto& link = page->links[i];
    int16_t geometry[4];
    if (!serialization::readBytesChecked(file, link.href, sizeof(link.href)) ||
        !memchr(link.href, '\0', sizeof(link.href)) ||
        !serialization::readBytesChecked(file, geometry, sizeof(geometry))) {
      LOG_ERR("PGE", "Failed to read link %u", i);
      return nullptr;
    }
    link.x = geometry[0];
    link.y = geometry[1];
    link.width = geometry[2];
    link.height = geometry[3];
    if (link.href[0] == '\0' || link.width <= 0 || link.height <= 0) {
      LOG_ERR("PGE", "Invalid link geometry %u", i);
      return nullptr;
    }
  }

  return page;
}
