#pragma once
#include <LibraryBookState.h>
#include <LibraryFormat.h>
#include <LibrarySession.h>
#include <LibraryText.h>
#include <Utf8.h>
#include <components/lists/list.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "LibraryUiStrings.h"

#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
inline void delay(int) {}
inline int lockDepth = 0;
inline int locksTaken = 0;
struct RenderLock {
  template <class T>
  explicit RenderLock(T&) {
    ++lockDepth;
    ++locksTaken;
  }
  ~RenderLock() { --lockDepth; }
};
inline bool failNextAllocation = false;
template <class T, class... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (failNextAllocation) {
    failNextAllocation = false;
    return {};
  }
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct Settings {
  int libraryUseMetadata = 1;
  bool libraryGroupBySeries = false;
  int saves = 0;
  bool saveToFile() {
    ++saves;
    return true;
  }
};
inline Settings SETTINGS;
namespace library {
inline bool dirtyIndex = false;
inline bool isLibraryIndexDirty() { return dirtyIndex; }
}  // namespace library
struct MappedInputManager {
  enum class Button { Confirm, Back, NavNext, NavPrevious };
  bool confirmPressed = false, backPressed = false, confirmReleased = false, backReleased = false;
  bool nextPressed = false, previousPressed = false;
  bool confirmLongPress = false;
  unsigned long held = 0;
  bool isPressed(Button b) const { return b == Button::Confirm ? confirmPressed : backPressed; }
  bool wasPressed(Button b) const {
    return b == Button::NavNext ? nextPressed : b == Button::NavPrevious && previousPressed;
  }
  bool wasReleased(Button b) const { return b == Button::Confirm ? confirmReleased : backReleased; }
  unsigned long getHeldTime() const { return held; }
  bool wasLongPressed(Button b, unsigned long) {
    return b == Button::Confirm && std::exchange(confirmLongPress, false);
  }
};
struct RecentBook {
  std::string path, title, author;
};
struct RecentStore {
  std::vector<RecentBook> books;
  int saves = 0, removals = 0, prunes = 0;
  bool pruneResult = false;
  const auto& getBooks() const { return books; }
  bool pruneMissing() {
    ++prunes;
    return pruneResult;
  }
  bool saveToFile() {
    ++saves;
    return true;
  }
  bool removeByPath(const std::string& path) {
    const auto oldSize = books.size();
    books.erase(std::remove_if(books.begin(), books.end(), [&](const auto& b) { return b.path == path; }), books.end());
    if (oldSize != books.size()) {
      ++removals;
      ++saves;
      return true;
    }
    return false;
  }
};
inline RecentStore RECENT_BOOKS;
inline std::vector<std::string> clearedCaches;
inline void clearBookCache(const std::string& path) { clearedCaches.push_back(path); }
struct StorageFake {
  bool removeOk = true;
  std::vector<std::string> removed;
  bool remove(const char* path) {
    if (!removeOk) return false;
    removed.emplace_back(path);
    return true;
  }
  bool exists(const char*) const { return true; }
};
inline StorageFake Storage;
namespace library {
enum class SortOrder : uint8_t {
  AddedAsc,
  AddedDesc,
  TitleAsc,
  TitleDesc,
  AuthorAsc,
  AuthorDesc,
  SeriesAsc,
  SeriesDesc
};
inline const char* libraryIndexPath() { return "/index"; }
inline std::map<uint64_t, BookState> states;
inline bool stateReadOk = true, stateWriteOk = true;
inline unsigned stateReadCalls = 0;
inline uint64_t failStateKey = UINT64_MAX;
inline uint64_t bookStateKey(std::string_view path) { return path.empty() ? 0 : static_cast<uint64_t>(path.back()); }
inline bool readBookState(uint64_t key, BookState& state) {
  ++stateReadCalls;
  if (!stateReadOk || key == failStateKey) return false;
  state = states[key];
  return true;
}
inline bool writeBookState(uint64_t key, const BookState& state) {
  if (!stateWriteOk) return false;
  states[key] = state;
  return true;
}
struct Book {
  std::string title, author, path, series;
  uint16_t seriesId = CLIX_SERIES_NONE, position = SERIES_INDEX_NONE;
  uint64_t key = 0;
  bool hasTitleMetadata = true;
  std::string fileName{};
  std::string sourceAuthor{};
  std::string authorSort{};
  std::string sortTitle{};  // folded into the record instead of the title when set
};
struct PathIdentity {
  std::string_view path;
  uint64_t pathHash;
  uint32_t fileSize = 0;
};
struct Index {
  std::vector<Book> books;
  std::array<std::vector<uint16_t>, 8> permutations;
  ClixHeader head{};
  bool opened = true, openOk = true, failed = false, degraded = false;
  int opens = 0, closes = 0, failOrdinal = -1;
  int readCalls = 0;
  int titleReadCalls = 0, nameReadCalls = 0, pathHashReadCalls = 0;
  int authorReadCalls = 0, seriesRefReadCalls = 0, seriesEntryReadCalls = 0, metadataReadCalls = 0;
  int failTitleOrdinal = -1, failNameOrdinal = -1, failPathHashOrdinal = -1;
  int failRankRow = -1, failAuthorOrdinal = -1, failSeriesRefOrdinal = -1, failSeriesId = -1;
  int malformedAuthorOrdinal = -1;
  bool pathLookupOk = true;
  int pathLookups = 0;
  bool rowsForPaths(SortOrder order, const PathIdentity* paths, size_t count, uint16_t* out) {
    ++pathLookups;
    for (size_t i = 0; i < count; ++i) out[i] = 0xffff;
    if (!pathLookupOk) return false;
    for (uint16_t row = 0; row < bookCount(); ++row) {
      const auto ordinal = ordinalForRow(order, row);
      if (ordinal == 0xffff) return false;
      for (size_t i = 0; i < count; ++i)
        if (books[ordinal].path == paths[i].path && books[ordinal].key == paths[i].pathHash) out[i] = row;
    }
    return true;
  }
  bool open(const char*) {
    ++opens;
    opened = openOk;
    return opened;
  }
  void close() {
    ++closes;
    opened = false;
  }
  bool isOpen() const { return opened; }
  const auto& header() const { return head; }
  int bookCount() const { return opened ? static_cast<int>(books.size()) : 0; }
  bool ranksDegraded() const { return degraded; }
  bool limitsReached() const { return false; }
  bool dedupDegraded() const { return false; }
  bool ioFailed() const { return failed; }
  uint16_t ordinalForRow(SortOrder order, uint16_t row) {
    if (!opened || row >= books.size()) return 0xffff;
    if (row == failRankRow) {
      failed = true;
      return 0xffff;
    }
    const auto& permutation = permutations[static_cast<size_t>(order)];
    return permutation.empty() ? row : permutation[row];
  }
  bool readRecord(uint16_t ordinal, ClixRecord& out) {
    ++readCalls;
    if (!opened || ordinal >= books.size() || ordinal == failOrdinal) {
      failed = true;
      return false;
    }
    out = {};
    out.nameOff = ordinal;
    const auto& book = books[ordinal];
    std::string title = book.title;
    if (!book.hasTitleMetadata) {
      title = book.fileName.empty() ? book.path.substr(book.path.find_last_of('/') + 1) : book.fileName;
      const size_t dot = title.find_last_of('.');
      if (dot != std::string::npos && dot != 0) title.resize(dot);
    }
    const auto folded = fold(book.sortTitle.empty() ? title : book.sortTitle);
    out.foldLen = utf8SafeTruncateBuffer(folded.data(), static_cast<int>(std::min(folded.size(), sizeof(out.fold))));
    std::memcpy(out.fold, folded.data(), out.foldLen);
    out.authorKeyLen =
        writeAuthorKey(authorIdentity(book.sourceAuthor.empty() ? book.author : book.sourceAuthor), out.authorKey);
    return true;
  }
  bool readPathHash(const ClixRecord& r, uint64_t& key) {
    ++pathHashReadCalls;
    if (static_cast<int>(r.nameOff) == failPathHashOrdinal) {
      failed = true;
      return false;
    }
    key = books[r.nameOff].key;
    return true;
  }
  bool readAuthor(const ClixRecord& r, std::string& text);
  bool readAuthorSort(const ClixRecord& r, std::string& text) {
    text = books[r.nameOff].authorSort;
    return true;
  }
  bool readBlobField(const ClixRecord& r, uint8_t field, std::string& text) {
    ++authorReadCalls;
    text.clear();
    if (field != 0 || static_cast<int>(r.nameOff) == failAuthorOrdinal) {
      failed = true;
      return false;
    }
    if (static_cast<int>(r.nameOff) == malformedAuthorOrdinal) return false;
    text = books[r.nameOff].author;
    return true;
  }
  bool readTitle(const ClixRecord& r, std::string& text) {
    ++titleReadCalls;
    text.clear();
    if (static_cast<int>(r.nameOff) == failTitleOrdinal) {
      failed = true;
      return false;
    }
    if (books[r.nameOff].hasTitleMetadata) {
      const auto& title = books[r.nameOff].title;
      const int bytes = static_cast<int>(std::min<size_t>(title.size(), UINT8_MAX));
      text.assign(title.data(), utf8SafeTruncateBuffer(title.data(), bytes));
    }
    return !text.empty();
  }
  bool readTitleAndAuthor(const ClixRecord& r, std::string& title, std::string& author) {
    ++metadataReadCalls;
    title.clear();
    author.clear();
    if (static_cast<int>(r.nameOff) == failTitleOrdinal || static_cast<int>(r.nameOff) == failAuthorOrdinal) {
      failed = true;
      return false;
    }
    if (static_cast<int>(r.nameOff) == malformedAuthorOrdinal) return false;
    const auto& book = books[r.nameOff];
    if (book.hasTitleMetadata) {
      const int bytes = static_cast<int>(std::min<size_t>(book.title.size(), UINT8_MAX));
      title.assign(book.title.data(), utf8SafeTruncateBuffer(book.title.data(), bytes));
    }
    author = book.author;
    return true;
  }
  bool readTitleAuthorAndSort(const ClixRecord& r, std::string& title, std::string& author, std::string& sort) {
    if (!readTitleAndAuthor(r, title, author)) return false;
    sort = books[r.nameOff].authorSort;
    return true;
  }
  bool readPath(const ClixRecord& r, std::string& text) {
    text = books[r.nameOff].path;
    return true;
  }
  bool readName(const ClixRecord& r, std::string& text) {
    ++nameReadCalls;
    text.clear();
    if (static_cast<int>(r.nameOff) == failNameOrdinal) {
      failed = true;
      return false;
    }
    const auto& book = books[r.nameOff];
    text = book.fileName.empty() ? book.path.substr(book.path.find_last_of('/') + 1) : book.fileName;
    return true;
  }
  bool readSeriesRef(uint16_t ordinal, ClixSeriesRef& ref) {
    ++seriesRefReadCalls;
    if (!opened || ordinal >= books.size() || ordinal == failSeriesRefOrdinal) {
      failed = true;
      return false;
    }
    ref.seriesId = books[ordinal].seriesId;
    ref.seriesIndex = books[ordinal].position;
    return true;
  }
  bool readSeries(uint16_t id, std::string& text, uint16_t& count) {
    ++seriesEntryReadCalls;
    count = 0;
    if (id == failSeriesId) {
      failed = true;
      return false;
    }
    for (const auto& b : books)
      if (b.seriesId == id) {
        text = b.series;
        ++count;
      }
    return count > 0;
  }
};
}  // namespace library
namespace fui = freeink::ui;
struct UiScreen {
  struct Theme {
    fui::TextStyle bodyText;
    int16_t listRowGap = 0;
  } themeValue;
  fui::ListProps props;
  int listCalls = 0, centeredTextCalls = 0;
  std::string centeredMessage;
  const auto& theme() const { return themeValue; }
  void list(fui::ListProps value) {
    ++listCalls;
    props = value;
  }
  void centeredText(const char* text) {
    ++centeredTextCalls;
    centeredMessage = text;
  }
};
struct UITheme {
  static int getFileIcon(const std::string&) { return 1; }
};
inline fui::BitmapRef listIconFor(int, int) { return {}; }
struct AppFake {
  int cleared = 0;
  template <class... T>
  void on(T&&...) {}
  void clearTapFlash() { ++cleared; }
};
struct GuiFake {
  int popups = 0;
  void drawPopup(int, const char*) { ++popups; }
};
inline GuiFake GUI;
struct Activity {
  static void onExit() {}
};
struct UiTabListActivity {
  static void onEnter() {}
};
struct KeyboardResult {
  std::string text;
};
struct MenuResult {
  int action = 0;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<std::monostate, KeyboardResult, MenuResult> data;
};
enum class InputType { Text };
struct Child {
  enum Kind { Confirmation, Keyboard, Menu, Details } kind;
  std::string title, path;
  std::vector<StrId> labels;
  explicit Child(Kind k) : kind(k) {}
};
struct ConfirmationActivity : Child {
  ConfirmationActivity(int, MappedInputManager&, const char*, const std::string& t) : Child(Confirmation) { title = t; }
};
struct KeyboardEntryActivity : Child {
  KeyboardEntryActivity(int, MappedInputManager&, const char*, const std::string& t, int, InputType) : Child(Keyboard) {
    title = t;
  }
};
struct LibraryMenuActivity : Child {
  LibraryMenuActivity(int, MappedInputManager&, const std::string& t, const StrId* values, int count) : Child(Menu) {
    title = t;
    labels.assign(values, values + count);
  }
};
struct LibraryBookDetailsActivity : Child {
  LibraryBookDetailsActivity(int, MappedInputManager&) : Child(Details) {}
  bool setBook(const std::string& t, const std::string& p) {
    title = t;
    path = p;
    return true;
  }
};
inline constexpr int RECENT_TAB = 0, ADDED_TAB = 1, TITLE_TAB = 2, AUTHOR_TAB = 3, TAB_SLOTS = 4;
inline constexpr unsigned long LONG_PRESS_MS = 1000;
inline bool isAddedSort(library::SortOrder o) {
  return o == library::SortOrder::AddedAsc || o == library::SortOrder::AddedDesc;
}
inline bool isAuthorSort(library::SortOrder o) {
  return o == library::SortOrder::AuthorAsc || o == library::SortOrder::AuthorDesc;
}
inline bool isSeriesSort(library::SortOrder o) {
  return o == library::SortOrder::SeriesAsc || o == library::SortOrder::SeriesDesc;
}
struct ButtonNavigator {
  bool nextPress = false, previousPress = false, previousRelease = false, nextHold = false, previousHold = false;
  template <class Fn>
  void onNextPress(Fn fn) {
    if (std::exchange(nextPress, false)) fn();
  }
  template <class Fn>
  void onPreviousPress(Fn fn) {
    if (std::exchange(previousPress, false)) fn();
  }
  template <class Fn>
  void onPreviousRelease(Fn fn) {
    if (std::exchange(previousRelease, false)) fn();
  }
  template <class Fn>
  void onNextContinuous(Fn fn) {
    if (std::exchange(nextHold, false)) fn();
  }
  template <class Fn>
  void onPreviousContinuous(Fn fn) {
    if (std::exchange(previousHold, false)) fn();
  }
  static int nextIndex(int, int);
  static int previousIndex(int, int);
  static int nextPageIndex(int, int, int);
  static int previousPageIndex(int, int, int);
};
class LibraryListActivity : public Activity, public UiTabListActivity {
 public:
  LibraryListActivity() {
    for (auto& nav : navs) {
      nav.visibleRows = 4;
      nav.followOnBuild = false;
    }
  }
  library::Index index;
  library::ShelfFilter shelfFilter = library::ShelfFilter::All;
  library::SortOrder sortOrder = library::SortOrder::AddedDesc;
  int activeTabIndex = 0;
  uint8_t descendingTabs = 1u << 1;
  std::string query;
  std::string headerSearchTitle, groupTitle;
  int selectedGroup = -1;
  std::unique_ptr<uint16_t[]> filtered, groupStarts;
  uint16_t filteredCount = 0, groupCapacity = 0, groupCount = 0;
  bool filterFailed = false, degraded = false, refreshFailed = false, groupsCollapsed = false;
  bool lockNextConfirmRelease = false, lockNextBackRelease = false;
  bool navigationStartedOnTabs = false;
  std::array<fui::ListNav, 4> navs;
  fui::ListNav expandedNav;
  MappedInputManager mappedInput;
  ButtonNavigator buttonNavigator;
  AppFake app;
  int renderer = 0, updates = 0, routingCloses = 0, homes = 0, rebuilds = 0;
  bool rebuildOk = true;
  std::function<void(library::Index&)> rebuildHook;
  std::string selectedPath;
  std::vector<fui::ListItem> winItems;
  std::vector<std::string> winTitles, winAuthors, winHeaders;
  std::unique_ptr<Child> child;
  std::function<void(const ActivityResult&)> callback;
  bool childStartedWithIndexOpen = false;
  static constexpr int ACTION_SEARCH = 5, ACTION_OPTIONS = 6, ACTION_ROW = 7, ACTION_REBUILD = 8, ACTION_BACK = 9;
  static void searchActionTrampoline(const fui::ActionEvent&, void*) {}
  static void rebuildActionTrampoline(const fui::ActionEvent&, void*);
  static void optionsActionTrampoline(const fui::ActionEvent&, void*) {}
  void requestUpdate(bool = false) { ++updates; }
  void closeRouting() { ++routingCloses; }
  fui::ListNav& activeNav() { return navs[activeTabIndex]; }
  const fui::ListNav& activeNav() const { return navs[activeTabIndex]; }
  int ringPos() const { return activeNav().selected; }
  bool tabsFocused() const { return ringPos() == 0; }
  // One input frame: a press fires its step immediately; later frames carry the hold or release.
  void pressNext() {
    mappedInput.nextPressed = buttonNavigator.nextPress = true;
    navigateButtons();
    mappedInput.nextPressed = false;
  }
  void pressPrevious() {
    mappedInput.previousPressed = buttonNavigator.previousPress = true;
    navigateButtons();
    mappedInput.previousPressed = false;
  }
  bool hasFilter() const { return !query.empty() || shelfFilter != library::ShelfFilter::All; }
  bool rebuildIndex() {
    ++rebuilds;
    if (rebuildHook) rebuildHook(index);
    return rebuildOk;
  }
  void onSelectBook(const std::string& path) { selectedPath = path; }
  void onGoHome() { ++homes; }
  template <class T, class Fn>
  void startActivityForResult(std::unique_ptr<T> next, Fn cb) {
    childStartedWithIndexOpen = index.isOpen();
    child = std::move(next);
    callback = cb;
  }
  void returnChild(ActivityResult result) {
    auto cb = std::move(callback);
    child.reset();
    cb(result);
  }
  void syncTabListViewport(UiScreen&, fui::ListProps& props) {
    auto& nav = activeNav();
    props.partialTrailingRow = true;
    nav.syncToProps({0, 0, 200, static_cast<int16_t>(nav.visibleRows * 10)}, 10, 0, listCount(), props, 1);
  }
  void navigateButtons();
  void moveRingTo(int);
  void buildRows(UiScreen&);
  static void formatInitialHeading(uint32_t, std::string&);
  void authorHeadingFor(const std::string&, const std::string&, std::string&) const;
  void formatAuthorHeading(const std::string&, std::string&) const;
  void onEnter();
  void onExit();
  void swallowHeldReleases();
  int selectedEntry() const;
  bool showingRecents() const;
  void openSelectedBook();
  void activateIndex(int);
  void onRowLongPress(int);
  void promptRemoveRecentBook(const std::string&, const std::string&);
  void openSearch();
  void stepTab(int);
  void onTabAction(int);
  void selectTab(int, bool);
  void toggleSortDirection();
  int tabCount() const;
  int activeTab() const;
  const char* headerTitle() const;
  int totalBookRowCount() const;
  int bookRowCount() const;
  bool browsesGroups() const;
  int listCount() const;
  int rowFor(int) const;
  bool groupable() const;
  uint32_t titleInitialFor(int);
  bool buildGroupStarts();
  int groupForBook(int) const;
  bool collapseGroups(int);
  void expandGroup(int);
  void restoreExpandedList();
  void applyFilter();
  void refilterAfterBookChange(int);
  void filterBooks();
  bool rowTextFor(int, std::string&, std::string&, uint32_t* = nullptr, std::string* = nullptr);
  bool authorFor(int, std::string&, std::string* = nullptr);
  bool handleCustomInput();
  bool handleButtons();
  void handleBackAction();
  static void backActionTrampoline(const fui::ActionEvent&, void*);
  bool seriesFor(int, std::string&, uint16_t* = nullptr, uint16_t* = nullptr);
  bool releaseIndexForChild();
  bool restoreIndexAfterChild(bool);
  bool resolveBook(int, std::string&, std::string&);
  void promptDeleteBook(const std::string&, const std::string&);
  void openBookOptions(int);
  void openBookDetails(const std::string&, const std::string&);
  void openOptions();
  void openGrouping();
  void openShelfFilter();
  struct RefreshSelection {
    std::string paths[2];
    uint64_t hashes[2]{};
    uint32_t sizes[2]{};
    int entry = 0;
    int group = -1;
    bool collapsed = false;
    bool inGroup = false;
    bool tabFocus = true;
  };
  RefreshSelection captureRefreshSelection();
  void restoreRefreshSelection(const RefreshSelection& selection);
  void refreshLibrary();
};
