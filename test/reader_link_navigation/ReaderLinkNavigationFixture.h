#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ReaderNavigationHistory.h"

#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)

struct RenderLock {
  inline static bool held = false;
  RenderLock() {
    EXPECT_FALSE(held);
    held = true;
  }
  ~RenderLock() { held = false; }
};

struct HalFile {
  std::vector<uint8_t> data;
  std::vector<uint8_t>* sink = nullptr;
  int read(uint8_t* destination, size_t count) const {
    const auto bytes = std::min(count, data.size());
    if (bytes > 0) std::memcpy(destination, data.data(), bytes);
    return static_cast<int>(bytes);
  }
  size_t write(const uint8_t* source, size_t count) {
    if (!sink) return 0;
    sink->assign(source, source + count);
    return count;
  }
};
struct StorageFixture {
  bool available = false;
  std::vector<uint8_t> data;
  bool writable = true;
  std::string writtenPath;
  std::vector<uint8_t> written;
  std::string removedPath;
  bool exists(const char*) const { return available; }
  bool openFileForRead(const char*, const std::string&, HalFile& file) {
    file.data = data;
    return available;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    if (!writable) return false;
    writtenPath = path;
    file.sink = &written;
    return true;
  }
  bool remove(const char* path) {
    removedPath = path;
    available = false;
    return true;
  }
};
inline StorageFixture Storage;
inline int clampPercent(int percent) { return std::clamp(percent, 0, 100); }

struct FootnoteResult {
  std::string href;
};
struct ProgressChangeResult {
  int spineIndex = 0;
  int page = 0;
  int totalPages = 0;
  bool hasSavedProgress = false;
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 0;
  std::string xpath;
  float percentage = 0;
};
struct ChapterResult {
  int spineIndex = 0;
  std::string anchor;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<FootnoteResult, ProgressChangeResult, ChapterResult> data;
};
struct CrossPointPosition {
  int spineIndex;
  int pageNumber;
};
struct SavedProgressPosition {
  std::string xpath;
  float percentage;
};
struct ProgressMapper {
  template <typename Book, typename Renderer>
  static CrossPointPosition toCrossPoint(const Book&, const SavedProgressPosition&, const Renderer&, int, int) {
    return {7, 9};
  }
};
struct FootnoteEntry {
  std::string href;
};
struct Page {
  size_t count = 2;
};
struct ReaderRenderer {
  void getOrientedViewableTRBL(int* top, int* right, int* bottom, int* left) const {
    EXPECT_TRUE(RenderLock::held);
    *top = 2;
    *right = 3;
    *bottom = 4;
    *left = 5;
  }
};
inline struct {
  int screenMargin = 7;
  char dictionaryName[16] = "dictionary";
} SETTINGS;
inline unsigned long millis() { return 1234; }
struct EpubReaderFootnoteSelectActivity {
  std::unique_ptr<Page> page;
  size_t count;
  int left;
  int top;
  EpubReaderFootnoteSelectActivity(ReaderRenderer&, int, std::unique_ptr<Page> p, int left, int top)
      : page(std::move(p)), count(page->count), left(left), top(top) {}
};
struct DictionaryWordSelectActivity : EpubReaderFootnoteSelectActivity {
  using EpubReaderFootnoteSelectActivity::EpubReaderFootnoteSelectActivity;
};
inline bool failPickerAllocation = false;
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (failPickerAllocation) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}

class EpubReaderActivity {
 public:
  using Jump = ReaderNavigationHistory::Jump;
  struct Book {
    int target = 10;
    int resolves = 0;
    int textReference = 3;
    size_t bookSize = 10000;
    int spineCount = 10;
    ChapterResult tocItem{4, "chapter"};
    std::vector<ChapterResult> tocItems;  // per-index entries; tocItem beyond them
    ChapterResult getTocItem(int index) const {
      return index < static_cast<int>(tocItems.size()) ? tocItems[index] : tocItem;
    }
    size_t getBookSize() const { return bookSize; }
    int getSpineItemsCount() const { return spineCount; }
    size_t getCumulativeSpineItemSize(int spine) const { return (spine + 1) * 1000; }
    std::string getCachePath() const { return "/cache"; }
    int getSpineIndexForTextReference() const { return textReference; }
    int resolveHrefToSpineIndex(const std::string&) {
      ++resolves;
      return target;
    }
  };
  struct Section {
    int currentPage = 4;
    int pageCount = 20;
    bool loadSucceeds = true;
    int loads = 0;
    std::unique_ptr<Page> loadPage(int page) {
      EXPECT_TRUE(RenderLock::held);
      EXPECT_EQ(page, currentPage);
      ++loads;
      return loadSucceeds ? std::make_unique<Page>() : nullptr;
    }
    int estimatedTotalPages() const { return 20; }
    std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t offset) const { return offset / 100; }
    ~Section() { EXPECT_TRUE(RenderLock::held); }
  };

  std::unique_ptr<Book> epub = std::make_unique<Book>();
  std::unique_ptr<Section> section;
  int currentSpineIndex = 2;
  int nextPageNumber = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  std::optional<uint32_t> pendingOffsetJump;
  std::optional<uint16_t> pendingPageJump;
  bool pendingPercentJump = false;
  bool pendingLastPageJump = false;
  bool pendingBuildError = false;
  bool buildHeapPaused = false;
  int pendingManualTurn = 0;
  float pendingSpineProgress = 0;
  std::string pendingAnchor;
  ReaderNavigationHistory navigationHistory;
  enum class Overlay { None, Contents };
  Overlay overlay = Overlay::Contents;
  int panelIndex = 0;
  std::vector<FootnoteEntry> currentPageFootnotes;
  ReaderRenderer renderer;
  int mappedInput = 0;
  int updates = 0;
  int menus = 0;
  int clears = 0;
  int saves = 0;
  int savedSpine = 0;
  int savedPage = 0;
  int savedCount = -1;
  std::optional<uint32_t> savedOffset;
  std::unique_ptr<EpubReaderFootnoteSelectActivity> picker;
  std::function<void(const ActivityResult&)> pickerResult;

  ~EpubReaderActivity() {
    RenderLock lock;
    section.reset();
  }
  void showPage(int spine, int page) {
    currentSpineIndex = spine;
    RenderLock lock;
    section = std::make_unique<Section>();
    section->currentPage = page;
  }
  void requestUpdate() { ++updates; }
  void openReaderMenu() { ++menus; }
  void loadCachedBookmarks() {}
  void discardOverlayPage() {}
  void clearDeferredReposition() {
    EXPECT_TRUE(RenderLock::held);
    cachedChapterTotalPageCount = 0;
    cachedVisibleTextOffset.reset();
    ++clears;
  }
  void startActivityForResult(std::unique_ptr<EpubReaderFootnoteSelectActivity>&& activity,
                              std::function<void(const ActivityResult&)> handler) {
    EXPECT_FALSE(RenderLock::held);
    picker = std::move(activity);
    pickerResult = std::move(handler);
  }
  bool saveProgress(int spine, int page, int count, std::optional<uint32_t> offset = std::nullopt) {
    ++saves;
    savedSpine = spine;
    savedPage = page;
    savedCount = count;
    savedOffset = offset;
    return true;
  }
  bool showDictionaryMessage = false;
  unsigned long dictionaryMessageTime = 0;
  void openDictionaryWordSelect();
  void openFootnoteSelect(bool returnToMenuOnCancel);
  void navigateToHref(const std::string& href, Jump jump);
  void saveLinkStack() const;
  void loadLinkStack();
  void restoreSavedPosition();
  void clearPendingNavigation();
  void jumpToPercent(int percent);
  void jumpToByteOffset(size_t targetSize);
  void loadSavedProgress();
  void returnFromProgress(const ActivityResult& result);
  void returnFromChapter(const ActivityResult& result);
  void selectToc();
};

struct EpubReaderChapterSelectionActivity {
  std::unique_ptr<EpubReaderActivity::Book> epub = std::make_unique<EpubReaderActivity::Book>();
  struct {
    void clearTapFlash() {}
  } app;
  struct {
    int selected = 0;
  } nav;
  int finishes = 0;
  std::optional<ActivityResult> result;
  int listCount() const { return epub ? 2 : 0; }
  void setResult(ActivityResult&& value) { result = std::move(value); }
  void setResult(ChapterResult&& value) { result = ActivityResult{false, std::move(value)}; }
  void finish() { ++finishes; }
  void activateIndex(int index);
};
