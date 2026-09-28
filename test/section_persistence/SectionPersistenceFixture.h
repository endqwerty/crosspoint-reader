#pragma once

#include <Epub/Page.h>
#include <Epub/SectionPageReader.h>
#include <Logging.h>
#include <Serialization.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Only dependency state is supplied here. Persistence/read algorithms and the
// temporary-path method are compiled from their complete production methods.
class Section {
 public:
  struct PageLutEntry {
    uint32_t fileOffset;
    uint16_t paragraphIndex;
    uint16_t listItemIndex;
    uint32_t visibleTextOffset;
  };
  struct ParserState {
    std::vector<std::pair<std::string, uint16_t>> anchors;
    bool failed = false;
    void markFailed() { failed = true; }
    bool hasError() const { return failed; }
    const auto& getAnchors() const { return anchors; }
  };
  struct BuildContext {
    std::unique_ptr<ParserState> parser = std::make_unique<ParserState>();
    std::vector<PageLutEntry> lut;
    bool ioFailed = false;
  };
  std::string filePath = "section";
  HalFile file;
  std::unique_ptr<BuildContext> build_ = std::make_unique<BuildContext>();
  uint16_t builtPageCount_ = 0;
  uint16_t pageCount = 0;

  uint32_t onPageComplete(std::unique_ptr<Page> page);
  void appendPage(std::unique_ptr<Page> page, uint16_t paragraphIndex, uint16_t listItemIndex,
                  uint32_t visibleTextOffset);
  bool commitBuildFile(uint8_t version, uint32_t bytesConsumed, uint32_t totalBytes);
  std::unique_ptr<Page> loadPageDuringBuild(int page);
  std::optional<uint16_t> getCachedPageCount() const;
  std::optional<uint16_t> getPageForAnchor(const std::string& anchor) const;
  std::optional<uint16_t> getPageForParagraphIndex(uint16_t index) const;
  std::optional<uint16_t> getParagraphIndexForPage(uint16_t page) const;
  std::optional<uint16_t> getPageForListItemIndex(uint16_t index) const;
  std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t offset, bool preferFirstAtOffset = false) const;
  std::optional<uint32_t> getVisibleTextOffsetForPage(uint16_t page) const;
#include "SectionTmpPath.h"
};

namespace section_test {
uint8_t completeVersion();
uint8_t incompleteVersion();
uint8_t partialVersion();
}  // namespace section_test
