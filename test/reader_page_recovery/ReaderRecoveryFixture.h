#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ChapterPosition.h"
#include "ReaderNavigationHistory.h"
#include "ReaderProgressState.h"

#define LOG_ERR(...) \
  do {               \
  } while (0)
#define LOG_DBG(...) \
  do {               \
  } while (0)
#define tr(value) "load error"
inline unsigned long millis() { return 1000; }
inline constexpr int UI_12_FONT_ID = 0;
namespace EpdFontFamily {
inline constexpr int BOLD = 1;
}

struct RecoveryStorage {
  bool failRead = false;
  int pageCount = 100;
  int lastRead = -1;
  int abandonCalls = 0;
  int clearCalls = 0;
  int destructions = 0;
  bool building = false;
};
struct RecoveryPage {
  uint32_t visibleTextOffset = 0;
  std::vector<int> footnotes;
  std::vector<int> links;
};
struct RecoverySection {
  RecoveryStorage& storage;
  int currentPage = 0;
  int pageCount;
  explicit RecoverySection(RecoveryStorage& storage) : storage(storage), pageCount(storage.pageCount) {}
  ~RecoverySection() { ++storage.destructions; }
  int estimatedTotalPages() const { return pageCount; }
  bool isBuilding() const { return storage.building; }
  bool isPartial() const { return storage.building; }
  void abandonBuild() { ++storage.abandonCalls; }
  void clearCache() { ++storage.clearCalls; }
  std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t offset) const {
    return static_cast<uint16_t>(offset / 100);
  }
  std::optional<uint16_t> findAnchor(const std::string&) const { return 42; }
  std::unique_ptr<RecoveryPage> loadPage(int page) {
    storage.lastRead = page;
    if (storage.failRead) return nullptr;
    auto result = std::make_unique<RecoveryPage>();
    result->visibleTextOffset = page * 100;
    return result;
  }
};
struct RecoveryRenderer {
  int errors = 0;
  int displays = 0;
  bool committed = true;
  bool displayCommitted() const { return committed; }
  void clearScreen() {}
  void drawCenteredText(int, int, const char*, bool, int) { ++errors; }
  void displayBuffer() { ++displays; }
};
struct EpubReaderActivity {
  RecoveryStorage storage;
  std::unique_ptr<RecoverySection> section = std::make_unique<RecoverySection>(storage);
  bool epub = true;
  RecoveryRenderer renderer;
  ReaderProgressState progressState;
  ReaderNavigationHistory navigationHistory;
  int currentSpineIndex = 2;
  int nextPageNumber = 5;
  int cachedSpineIndex = 2;
  int cachedChapterTotalPageCount = 100;
  std::optional<uint16_t> pendingPageJump;
  std::string pendingAnchor;
  std::optional<uint32_t> pendingOffsetJump;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  bool pendingPercentJump = false;
  bool pendingLastPageJump = false;
  bool pendingBuildError = false;
  bool buildHeapPaused = false;
  int pendingManualTurn = 0;
  float pendingSpineProgress = 0;
  bool automaticPageTurnActive = true;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  std::vector<int> currentPageFootnotes;
  std::vector<int> currentPageLinks;
  int currentPageLinkMarginLeft = 0;
  int currentPageLinkMarginTop = 0;
  int orientedMarginTop = 0, orientedMarginRight = 0, orientedMarginBottom = 0, orientedMarginLeft = 0;
  unsigned long lastRenderCompleteMs = 0;
  unsigned long lastPageTurnTime = 0;
  int idlePrewarmSpine = -1, idlePrewarmPage = -1;
  int updates = 0;
  int renderedPage = -1;
  int saves = 0;
  int savedPage = -1, savedSpine = -1;
  bool saveSucceeds = true;
  bool pageRendered = false;
  void markPageRendered() { pageRendered = true; }
  void requestUpdate() { ++updates; }
  void showPendingSyncSaveError() {}
  void renderContents(std::unique_ptr<RecoveryPage>, int, int, int, int) { renderedPage = section->currentPage; }
  bool saveProgress(int spine, int page, int, std::optional<uint32_t> = std::nullopt) {
    ++saves;
    savedSpine = spine;
    savedPage = page;
    return saveSucceeds;
  }
  void renderAttempt();
  void resumeSection();
  bool applyDeferredReposition();
  void clearDeferredReposition();
  void clearPendingNavigation();
  bool hasPendingSectionJump() const { return pendingPercentJump || pendingLastPageJump; }
  bool resolvePendingSectionJump();
  ChapterPosition chapterPosition() const;
};
