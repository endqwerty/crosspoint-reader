#pragma once

#include <Epub/PagePrefetchPolicy.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ReaderProgressState.h"

#define LOG_ERR(...) \
  do {               \
  } while (0)
#define LOG_DBG(...) \
  do {               \
  } while (0)
#define tr(value) #value
inline constexpr int UI_12_FONT_ID = 0;
namespace EpdFontFamily {
inline constexpr int BOLD = 1;
}
inline unsigned long millis() { return 1000; }
struct ReaderRenderSpec {};
struct IncrementalSettings {
  uint8_t screenMargin = 0;
  int getReaderFontId() const { return 1; }
  ReaderRenderSpec readerRenderSpec(int, int) const { return {}; }
};
inline IncrementalSettings SETTINGS;
struct IncrementalHeap {
  size_t freeHeap = 128 * 1024;
  size_t largest = 64 * 1024;
  mutable int freeReads = 0, largestReads = 0;
  size_t getFreeHeap() const {
    ++freeReads;
    return freeHeap;
  }
  size_t getMaxAllocHeap() const {
    ++largestReads;
    return largest;
  }
};
inline IncrementalHeap ESP;
struct RenderLock {
  static inline bool busy = false;
  static inline bool failTryAcquire = false;
  enum class Mode { Blocking, Try };
  static inline unsigned tryAttempts = 0, peekCalls = 0;
  explicit RenderLock(Mode mode = Mode::Blocking) {
    if (mode == Mode::Try) ++tryAttempts;
  }
  bool ownsLock() const { return !busy && !failTryAcquire; }
  static bool peek() {
    ++peekCalls;
    return busy;
  }
};
struct GfxRenderer {
  struct FontCache {
    int releases = 0;
    int scans = 0, prewarms = 0;
    struct Scope {
      FontCache* owner;
      void endScanAndPrewarm() { ++owner->prewarms; }
    };
    Scope createPrewarmScope() {
      ++scans;
      return {this};
    }
    void releaseSdFontCaches() { ++releases; }
  } fonts;
  std::vector<int> ttfFonts;
  const std::vector<int>& getTtfFonts() const { return ttfFonts; }
  FontCache* getFontCacheManager() { return &fonts; }
  struct FrameBufferLoan {
    explicit FrameBufferLoan(GfxRenderer&) {}
  };
  uint32_t loans = 0;
  uint32_t frameBufferLoanCount() const { return loans; }
  bool committed = true;
  int errors = 0;
  int emptyChapters = 0;
  int displays = 0;
  void clearScreen() {}
  bool frameAvailable = true;
  int scannedPages = 0;
  bool hasFrameBuffer() const { return frameAvailable; }
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  void getOrientedViewableTRBL(int* t, int* r, int* b, int* l) const { *t = *r = *b = *l = 0; }
  void drawCenteredText(int, int, const char* text, bool, int) {
    if (std::string(text) == "STR_EMPTY_CHAPTER")
      ++emptyChapters;
    else
      ++errors;
  }
  void displayBuffer() { ++displays; }
  bool displayCommitted() const { return committed; }
};
struct UITheme {
  struct Metrics {
    int statusBarVerticalMargin = 0;
  };
  int indexingPopups = 0;
  int buildErrors = 0;
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
  uint8_t getStatusBarHeight() const { return 10; }
  uint8_t getProgressBarHeight() const { return 2; }
  Metrics getMetrics() const { return {}; }
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) { ++backHints; }
  int backHints = 0;
  void drawPopup(GfxRenderer&, const char* message) {
    if (std::string(message) == "STR_INDEXING") ++indexingPopups;
    if (std::string(message) == "STR_INDEX_FAILED") ++buildErrors;
  }
};
#define GUI UITheme::getInstance()
struct IncrementalStorage {
  enum class Cache { None, Partial, Complete };
  Cache cache = Cache::None;
  int totalPages = 100;
  int cachedPages = 0;
  int buildsStarted = 0;
  int fontReleasesAtStart = -1;
  int stateReads = 0;
  int chunks = 0;
  int pagesBuilt = 0;
  int largestChunk = 0;
  int pageReads = 0;
  size_t idleReadHeapCost = 0;
  int retainAttempts = 0, retainedPages = 0, prefetchReleaseChecks = 0;
  bool startFails = false;
  bool chunkFails = false;
  bool allocationFails = false;
  bool readFails = false;
};
inline IncrementalStorage* activeStorage = nullptr;
struct Epub {
  struct Spine {
    std::string href = "chapter.xhtml";
  };
  int getSpineItemsCount() const { return 3; }
  Spine getSpineItem(int) const { return {}; }
  size_t getCumulativeSpineItemSize(int spine) const { return (spine + 1) * 200000; }
};
struct Page {
  void render(GfxRenderer& renderer, int, int, int) { ++renderer.scannedPages; }
  uint32_t visibleTextOffset = 0;
  std::vector<int> footnotes;
  std::vector<int> links;
};
struct Section {
  IncrementalStorage& storage = *activeStorage;
  int currentPage = 0;
  int pageCount = 0;
  int built = 0;
  bool building = false;
  bool partial = false;
  bool complete = false;
  GfxRenderer& renderer;
  Section(const std::shared_ptr<Epub>&, int, GfxRenderer& renderer) : renderer(renderer) {}
  bool loadSectionFile(const ReaderRenderSpec&) {
    partial = storage.cache == IncrementalStorage::Cache::Partial;
    complete = storage.cache == IncrementalStorage::Cache::Complete;
    pageCount = complete ? storage.totalPages : storage.cachedPages;
    return partial || complete;
  }
  bool isPartial() const { return partial; }
  bool isBuilding() const {
    ++storage.stateReads;
    return building;
  }
  bool isBuildComplete() const { return complete; }
  bool hasHtmlCache() const { return true; }
  int estimatedTotalPages() const { return storage.totalPages; }
  template <typename Callback = std::nullptr_t>
  bool startBuild(const ReaderRenderSpec&, Callback = nullptr) {
    storage.fontReleasesAtStart = renderer.fonts.releases;
    ++storage.buildsStarted;
    if (storage.startFails) return false;
    building = true;
    complete = false;
    built = 0;
    return true;
  }
  bool loanOnBuild = false;
  bool buildSomeMore(int budget) {
    if (loanOnBuild) ++renderer.loans;
    ++storage.chunks;
    storage.largestChunk = std::max(storage.largestChunk, budget);
    if (storage.chunkFails) {
      building = false;
      return false;
    }
    const int pages = std::min(budget, storage.totalPages - built);
    built += pages;
    storage.pagesBuilt += pages;
    pageCount = std::max(pageCount, built);
    if (built == storage.totalPages) {
      complete = true;
      building = partial = false;
      pageCount = built;
    }
    return true;
  }
  std::optional<uint16_t> getPageForAnchor(const std::string&) const { return 4; }
  std::optional<uint16_t> findAnchor(const std::string&) const { return 4; }
  std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t offset) const { return offset / 100; }
  bool buildReachedVisibleTextOffset(uint32_t offset) const { return built > static_cast<int>(offset / 100); }
  void abandonBuild() { building = false; }
  void clearCache() {}
  void releasePrefetchedPageIfLowMemory(size_t, size_t) { ++storage.prefetchReleaseChecks; }
  bool retainPrefetchedPage(int, std::unique_ptr<Page>, size_t freeHeap, size_t largestBlock) {
    ++storage.retainAttempts;
    if (building || !PagePrefetchPolicy::hasHeadroom(freeHeap, largestBlock)) return false;
    ++storage.retainedPages;
    return true;
  }
  std::unique_ptr<Page> loadPage(int page) {
    ++storage.pageReads;
    ESP.freeHeap -= std::min(ESP.freeHeap, storage.idleReadHeapCost);
    if (storage.readFails) return nullptr;
    auto result = std::make_unique<Page>();
    result->visibleTextOffset = page * 100;
    return result;
  }
};
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (activeStorage->allocationFails) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct EpubReaderActivity {
  IncrementalStorage storage;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section;
  GfxRenderer renderer;
  ReaderProgressState progressState;
  int currentSpineIndex = 1;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  bool pendingLastPageJump = false;
  std::optional<uint32_t> pendingOffsetJump;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  int cachedSpineIndex = 1;
  int cachedChapterTotalPageCount = 0;
  std::string pendingAnchor;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.5f;
  bool pendingBuildError = false;
  bool pendingSyncSaveError = false;
  int pendingManualTurn = 0;
  bool automaticPageTurnActive = false;
  unsigned long lastPageTurnTime = 0;
  bool renderedPageNeedsGrayscale = false;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool partialRebuildStartFailed = false;
  int buildViewportWidth = 0, buildViewportHeight = 0;
  bool buildHeapPaused = false;
  bool buildPopupPending = false;
#include "ReaderIncrementalConstants.h"
  int pagesUntilFullRefresh = 7;
  std::vector<int> currentPageFootnotes, currentPageLinks;
  int currentPageLinkMarginLeft = 0, currentPageLinkMarginTop = 0;
  unsigned long lastRenderCompleteMs = 0;
  int idlePrewarmSpine = -1, idlePrewarmPage = -1;
  int updates = 0, renderedPage = -1, saves = 0, savedPage = -1;
  EpubReaderActivity() {
    activeStorage = &storage;
    ESP = {};
    GUI = {};
    RenderLock::busy = RenderLock::failTryAcquire = false;
    RenderLock::tryAttempts = RenderLock::peekCalls = 0;
  }
  struct Input {
    struct Labels {
      const char *btn1, *btn2, *btn3, *btn4;
    };
    Labels mapLabels(const char* a, const char* b, const char* c, const char* d) { return {a, b, c, d}; }
  } mappedInput;
  bool pageRendered = false;
  void markPageRendered() { pageRendered = true; }
  void requestUpdate() { ++updates; }
  void discardOverlayPage() {}
  void settleOverlayRefresh() {}
  void updateBookmarkFlag() {}
  void renderStatusBar() {}
  void renderContents(std::unique_ptr<Page>, int, int, int, int) { renderedPage = section->currentPage; }
  bool saveProgress(int, int page, int) {
    ++saves;
    savedPage = page;
    return true;
  }
  enum class Overlay { None, Test };
  Overlay overlay = Overlay::None;
  bool pageBufferStale = false;
  void runIdlePrewarm();
  bool backgroundBuildWanted() const;
  bool buildTickHeapGate();
  bool resolvePendingSectionJump();
  void advanceSectionBuild();
  void showBuildPopup(GfxRenderer&, int&);
  bool skipLoopDelay();
  void renderBook();
  bool applyDeferredReposition();
  void clearDeferredReposition();
  void clearPendingNavigation();
  bool pageTurn(bool);
  bool skipPages(int);
  bool isAtEndOfBook() const;
  void onReturnFromEndOfBook();
};
