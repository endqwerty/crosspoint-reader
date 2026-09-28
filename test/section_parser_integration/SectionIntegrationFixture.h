#pragma once

#include <Epub/Page.h>
#include <Epub/PrefetchedPageCache.h>
#include <Epub/ReaderRenderSpec.h>
#include <Epub/SectionPageReader.h>
#include <Epub/css/CssParser.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

// Test dependency state for complete production Section methods. The parser,
// layout, page encoding/decoding and build lifecycle are production code.
class Section {
 public:
  struct PageLutEntry {
    uint32_t fileOffset;
    uint16_t paragraphIndex;
    uint16_t listItemIndex;
    uint32_t visibleTextOffset;
  };
  struct BuildContext {
    std::vector<PageLutEntry> lut;
    std::string parsePath;
    std::string contentBase;
    std::string imageBasePath;
    std::string htmlPath;
    std::string tmpHtmlPath;
    bool reusedHtml = true;
    bool ioFailed = false;
    CssParser* cssParser = nullptr;
    uint32_t bytesConsumed = 0;
    uint32_t totalBytes = 0;
    std::unique_ptr<ChapterHtmlSlimParser> parser;
  };
  std::string filePath;
  HalFile file;
  PrefetchedPageCache prefetchedPage;
  std::unique_ptr<BuildContext> build_;
  bool buildComplete_ = false;
  uint16_t builtPageCount_ = 0;
  uint16_t pageCount = 0;
  bool partial_ = false;
  uint16_t partialPageCount_ = 0;
  uint32_t partialBytesConsumed_ = 0;
  uint32_t partialTotalBytes_ = 0;

  ~Section();
  bool writeSectionFileHeader(const ReaderRenderSpec& spec);
  bool loadSectionFile(const ReaderRenderSpec& spec);
  bool clearCache();
  uint32_t onPageComplete(std::unique_ptr<Page> page);
  void appendPage(std::unique_ptr<Page> page, uint16_t paragraphIndex, uint16_t listItemIndex,
                  uint32_t visibleTextOffset);
  bool buildSomeMore(int maxPages);
  bool commitBuildFile(uint8_t version, uint32_t bytesConsumed, uint32_t totalBytes);
  bool finalizeBuild();
  void suspendBuild();
  void abandonBuild();
#include "SectionTmpPath.h"
};
