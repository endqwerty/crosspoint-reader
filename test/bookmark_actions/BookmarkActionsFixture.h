#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "BookmarkEntry.h"
#include "util/BookmarkFile.h"

#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)

inline unsigned long nowMs = 1000;
inline unsigned long millis() { return nowMs; }

struct RenderLock {
  inline static bool held = false;
  RenderLock() {
    EXPECT_FALSE(held);
    held = true;
  }
  ~RenderLock() { held = false; }
};

namespace BookmarkSaveSpy {
inline bool succeeds = true;
inline int calls = 0;
inline bool hadPrepend = false;
inline bool hadExclusion = false;
inline std::vector<BookmarkEntry> liveAtSave;
inline std::vector<BookmarkEntry> proposed;
inline std::function<void()> observe;
inline std::vector<std::string> events;
inline void reset() {
  succeeds = true;
  calls = 0;
  hadPrepend = false;
  hadExclusion = false;
  liveAtSave.clear();
  proposed.clear();
  observe = {};
  events.clear();
}
}  // namespace BookmarkSaveSpy

namespace BookmarkLoadSpy {
inline bool succeeds = true;
inline bool exists = true;
inline int calls = 0;
inline std::vector<BookmarkEntry> entries;
inline void reset() {
  succeeds = true;
  exists = true;
  calls = 0;
  entries.clear();
}
}  // namespace BookmarkLoadSpy

inline bool BookmarkFile::load(const std::string&, std::vector<BookmarkEntry>& bookmarks, bool* exists) {
  ++BookmarkLoadSpy::calls;
  if (exists) *exists = BookmarkLoadSpy::exists;
  bookmarks.clear();
  if (BookmarkLoadSpy::succeeds) bookmarks = BookmarkLoadSpy::entries;
  return BookmarkLoadSpy::succeeds;
}

inline bool BookmarkFile::save(const std::string&, const std::vector<BookmarkEntry>& bookmarks,
                               const SaveOptions& options) {
  EXPECT_TRUE(RenderLock::held);
  using namespace BookmarkSaveSpy;
  ++calls;
  events.emplace_back("save");
  liveAtSave = bookmarks;
  proposed.clear();
  hadPrepend = options.prepend != nullptr;
  hadExclusion = options.exclude != nullptr;
  if (options.prepend) proposed.push_back(*options.prepend);
  for (const auto& bookmark : bookmarks) {
    if (!options.exclude || !options.exclude(bookmark, options.context)) proposed.push_back(bookmark);
  }
  if (observe) observe();
  return succeeds;
}

struct RendererFixture {
  bool committed = true;
  bool displayCommitted() const { return committed; }
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getLineHeight(int) const { return 16; }
};

enum class StrId {
  STR_RENAME,
  STR_BOOKMARK_SAVE_FAILED,
  STR_BOOKMARK_ADDED,
  STR_BOOKMARK_REMOVED,
  STR_NO_BOOKMARKS,
  STR_BOOKMARK_LOAD_FAILED,
  STR_HOLD_OPEN_FOR_ACTIONS
};
class I18n {
 public:
  static I18n& getInstance() {
    static I18n instance;
    return instance;
  }
  const char* get(StrId id) const {
    if (id == StrId::STR_BOOKMARK_SAVE_FAILED) return "translated save error";
    if (id == StrId::STR_NO_BOOKMARKS) return "translated empty";
    if (id == StrId::STR_BOOKMARK_LOAD_FAILED) return "translated load error";
    if (id == StrId::STR_RENAME) return "translated rename";
    return id == StrId::STR_BOOKMARK_ADDED ? "translated added" : "translated removed";
  }
};
#include "I18nTranslationMacro.h"
struct Rect {
  int x, y, width, height;
};
struct GuiFixture {
  int popups = 0;
  std::string lastPopup;
  void drawHelpText(RendererFixture&, Rect, const char*) {}
  void drawPopup(RendererFixture&, const char* message) {
    EXPECT_TRUE(RenderLock::held);
    ++popups;
    lastPopup = message;
  }
};
inline GuiFixture GUI;

struct Epub {
  const std::string& getPath() const {
    static const std::string path = "/book.epub";
    return path;
  }
  int getSpineItemsCount() const { return 10; }
  float calculateProgress(int spine, float fraction) const { return (spine + fraction) / 10.0f; }
};
struct ReaderSection {
  int currentPage = 4;
  int pageCount = 20;
  int textReads = 0;
  int offsetReads = 0;
  int estimatedTotalPages() const { return pageCount; }
  std::string getTextFromSectionFile() {
    ++textReads;
    return "page summary";
  }
  std::optional<uint32_t> getVisibleTextOffsetForPage(uint16_t page) {
    ++offsetReads;
    return page * 100;
  }
};
struct CrossPointPosition {
  int spine;
  int page;
};
struct SavedProgressPosition {
  std::string xpath;
  float percentage;
};
struct ProgressMapper {
  static SavedProgressPosition toSavedProgress(const std::shared_ptr<Epub>& epub, CrossPointPosition position) {
    return {"/body/p[4]", epub->calculateProgress(position.spine, position.page / 19.0f)};
  }
};
struct BookmarkUtil {
  static std::string sanitizeBookmarkSummary(const std::string& summary) { return summary; }
};

struct EpubReaderActivity {
  RendererFixture renderer;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<ReaderSection> section = std::make_unique<ReaderSection>();
  std::vector<BookmarkEntry> cachedBookmarks;
  std::optional<uint32_t> currentPageVisibleOffset = 400;
  int currentSpineIndex = 1;
  bool currentPageBookmarked = false;
  bool bookmarkSaveFailed = false;
  bool bookmarkRemoved = false;
  bool bookmarkCacheValid = true;
  bool pendingPercentJump = false;
  bool pendingLastPageJump = false;
  bool pendingBuildError = false;
  bool showBookmarkMessage = false;
  unsigned long bookmarkMessageTime = 0;
  int updates = 0;
  bool hasPendingSectionJump() const { return pendingPercentJump || pendingLastPageJump; }
  CrossPointPosition getCurrentPosition() const { return {currentSpineIndex, section->currentPage}; }
  void requestUpdate() { ++updates; }
  void addBookmark();
  void loadCachedBookmarks();
  void updateBookmarkFlag();
  void renderFeedback();
};

struct KeyboardResult {
  std::string text;
};
struct ProgressChangeResult {
  std::string xpath;
  float percentage = 0;
  bool hasSavedProgress = false;
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 0;
  int spineIndex = -1;
  int page = -1;
  int totalPages = 0;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<std::monostate, KeyboardResult, ProgressChangeResult> data;
};
enum class InputType { Text };
struct KeyboardEntryActivity {
  std::string title;
  std::string initial;
  size_t maximum;
  KeyboardEntryActivity(RendererFixture&, int, const char* title, const std::string& initial, size_t maximum, InputType)
      : title(title), initial(initial), maximum(maximum) {}
};
inline bool failKeyboardAllocation = false;
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (failKeyboardAllocation) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}

namespace fui {
struct Insets {
  int16_t top, right, bottom, left;
};
using Rect = ::Rect;
struct ListItem {};
struct ListProps {
  const ListItem* items = nullptr;
  uint16_t count = 0;
  int action = 0;
  int inputMask = 0;
};
inline constexpr int InputTouch = 1;
inline constexpr int InputLongPress = 2;
}  // namespace fui
struct UiScreen {
  std::string message;
  int listCount = 0;
  struct Theme {
    int bodyText = 0;
  };
  Theme theme() const { return {}; }
  void setContentMarginFromScreen(fui::Insets) {}
  void spacer(int16_t) {}
  void centeredText(const char* text, int) { message = text; }
  fui::Rect takeBottom(int16_t height) { return {0, 0, 480, height}; }
  void list(const fui::ListProps& props) { listCount = props.count; }
};
struct UITheme {
  struct Metrics {
    int topPadding = 4, headerHeight = 24, verticalSpacing = 8;
  };
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  Metrics getMetrics() const { return {}; }
  Rect getScreenSafeArea(RendererFixture&, bool, bool) const { return {0, 0, 480, 800}; }
};
struct UiListActivity {
  bool entering = false;
  void onEnter() { entering = true; }
};
struct MappedInputManager {
  enum class Button { Confirm, Back };
  bool back = false;
  bool confirm = false;
  bool longConfirm = false;
  bool hasTouch() const { return false; }
  bool wasLongPressed(Button, int) const { return longConfirm; }
  bool wasReleased(Button button) const { return button == Button::Back ? back : confirm; }
  operator int() const { return 0; }
};
inline constexpr int SMALL_FONT_ID = 0;
inline constexpr int ACTION_ROW = 1;
inline constexpr int ENTER_ACTIONS_MODE_MS = 700;

struct EpubReaderBookmarksActivity : UiListActivity {
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::string epubPath = "/book.epub";
  std::vector<BookmarkEntry> bookmarks;
  struct Navigation {
    int selected = 0;
    int follows = 0;
    int lastCount = 0;
    void follow(int count) {
      ++follows;
      lastCount = count;
    }
  } nav;
  struct App {
    int tapClears = 0;
    void clearTapFlash() { ++tapClears; }
  } app;
  RendererFixture renderer;
  MappedInputManager mappedInput;
  bool loadFailed = false;
  std::vector<fui::ListItem> bookmarkRowItems;
  int routingCloses = 0;
  int rebuilds = 0;
  int updates = 0;
  bool lastUpdateImmediate = false;
  int finishes = 0;
  std::optional<ActivityResult> result;
  std::vector<std::string> rowNames;
  std::unique_ptr<KeyboardEntryActivity> keyboard;
  std::function<void(const ActivityResult&)> keyboardResult;
  int listCount() const { return static_cast<int>(bookmarks.size()); }
  void closeRouting() {
    EXPECT_TRUE(RenderLock::held);
    BookmarkSaveSpy::events.emplace_back("close routing");
    ++routingCloses;
  }
  void rebuildBookmarkRowItems() {
    EXPECT_TRUE(RenderLock::held || entering);
    entering = false;
    BookmarkSaveSpy::events.emplace_back("rebuild");
    ++rebuilds;
    rowNames.clear();
    for (const auto& bookmark : bookmarks) rowNames.push_back(bookmark.name);
  }
  void requestUpdate(bool immediate = false) {
    ++updates;
    lastUpdateImmediate = immediate;
  }
  void setResult(ActivityResult&& value) { result = std::move(value); }
  void setResult(ProgressChangeResult&& value) { result = ActivityResult{false, std::move(value)}; }
  void finish() { ++finishes; }
  void startActivityForResult(std::unique_ptr<KeyboardEntryActivity>&& child,
                              std::function<void(const ActivityResult&)> callback) {
    keyboard = std::move(child);
    keyboardResult = std::move(callback);
  }
  void syncListViewport(UiScreen&, fui::ListProps&) {}
  void showBookmarkActions() {}
  void onEnter();
  void buildScreen(UiScreen&);
  bool handleButtons();
  void startRename();
  void deleteSelectedBookmark();
  void openSelectedBookmark();
};
