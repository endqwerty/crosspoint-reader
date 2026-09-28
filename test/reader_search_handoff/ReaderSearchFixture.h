#pragma once
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <variant>

#include "ReaderNavigationHistory.h"

#define LOG_ERR(...) ((void)0)
struct RenderLock {
  inline static bool held = false;
  RenderLock() {
    EXPECT_FALSE(held);
    held = true;
  }
  ~RenderLock() { held = false; }
};
struct ImageBlock {
  inline static unsigned releases = 0;
  static void releaseRenderCache() {
    EXPECT_TRUE(RenderLock::held);
    releases++;
  }
};
struct FontCacheManager {
  unsigned releases = 0;
  void releaseSdFontCaches() {
    EXPECT_TRUE(RenderLock::held);
    releases++;
  }
};
struct GfxRenderer {
  FontCacheManager fonts;
  bool hasFonts = true;
  FontCacheManager* getFontCacheManager() { return hasFonts ? &fonts : nullptr; }
};
struct Book {
  int getSpineItemsCount() const { return 20; }
};
struct EpubSearchActivity {
  EpubSearchActivity(GfxRenderer&, int, const std::shared_ptr<Book>&) {}
};
struct EpubReaderMenuActivity {
  enum class MenuAction { FIND_IN_BOOK };
};
struct ProgressChangeResult {
  int spineIndex = 0;
  uint32_t visibleTextOffset = 0;
  bool hasVisibleTextOffset = false;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<std::monostate, ProgressChangeResult> data;
};
inline bool failSearchAllocation = false;
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (failSearchAllocation) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}

class EpubReaderActivity {
 public:
  struct Section {
    inline static unsigned releases = 0;
    int currentPage = 7;
    int pageCount = 42;
    ~Section() {
      EXPECT_TRUE(RenderLock::held);
      releases++;
    }
  };
  std::shared_ptr<Book> epub = std::make_shared<Book>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  GfxRenderer renderer;
  int mappedInput = 0;
  int currentSpineIndex = 3;
  int nextPageNumber = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> currentPageVisibleOffset = 1234;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> pendingOffsetJump;
  std::optional<uint16_t> pendingPageJump;
  std::string pendingAnchor;
  bool pendingPercentJump = false;
  bool pendingLastPageJump = false;
  bool pendingBuildError = false;
  bool buildHeapPaused = false;
  int pendingManualTurn = 0;
  ReaderNavigationHistory navigationHistory;
  unsigned overlays = 0, menus = 0, updates = 0;
  std::unique_ptr<EpubSearchActivity> search;
  std::function<void(const ActivityResult&)> resultHandler;
  ~EpubReaderActivity() {
    RenderLock lock;
    section.reset();
  }
  void discardOverlayPage() {
    EXPECT_TRUE(RenderLock::held);
    overlays++;
  }
  void openReaderMenu() { menus++; }
  void requestUpdate() { updates++; }
  void clearDeferredReposition() {
    cachedChapterTotalPageCount = 0;
    cachedVisibleTextOffset.reset();
  }
  void startActivityForResult(std::unique_ptr<EpubSearchActivity>&& child,
                              std::function<void(const ActivityResult&)> callback) {
    EXPECT_TRUE(RenderLock::held);
    EXPECT_FALSE(section);
    EXPECT_EQ(overlays, 1u);
    EXPECT_EQ(ImageBlock::releases, 1u);
    EXPECT_TRUE(epub);
    search = std::move(child);
    resultHandler = std::move(callback);
  }
  void returnFromSearch(const ActivityResult& result) {
    search.reset();
    auto callback = std::move(resultHandler);
    callback(result);
  }
  void prepareForBookSearch();
  void clearPendingNavigation();
  void launchBookSearch();
  void activateFind();
};
