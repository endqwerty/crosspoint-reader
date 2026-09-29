#pragma once
#include <Arduino.h>
#include <FolderSearch.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <LibraryBookState.h>
#include <LibrarySession.h>
#include <LibraryText.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <components/lists/list.h>
#include <util/BookStateMove.h>
#include <util/BookmarkUtil.h>

#include <cassert>
#include <functional>
#include <variant>

#include "FileBrowserStrings.h"
namespace fui = freeink::ui;
inline constexpr unsigned long GO_HOME_MS = 1000;
inline constexpr size_t NAME_BUFFER_SIZE = 500;
inline int lockDepth = 0;
struct RenderLock {
  bool held = true;
  template <typename T>
  explicit RenderLock(T&) {
    assert(lockDepth == 0);
    ++lockDepth;
  }
  ~RenderLock() {
    if (held) --lockDepth;
  }
  void unlock() {
    if (held) {
      --lockDepth;
      held = false;
    }
  }
};
struct Input {
  enum class Button { Confirm, Back };
  bool confirmPressed = false, backPressed = false, confirmReleased = false, backReleased = false;
  bool confirmLongPressed = false;
  unsigned long held = 0;
  bool isPressed(Button b) { return b == Button::Confirm ? confirmPressed : backPressed; }
  bool wasReleased(Button b) { return b == Button::Confirm ? confirmReleased : backReleased; }
  bool wasLongPressed(Button, unsigned long) { return std::exchange(confirmLongPressed, false); }
  unsigned long getHeldTime() { return held; }
};
using MappedInputManager = Input;
struct Settings {
  bool showHiddenFiles = false;
};
inline Settings SETTINGS;
struct UITheme {
  bool icons = false;
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  const UITheme& getTheme() const { return *this; }
  bool showsFileIcons() const { return icons; }
  static int getFileIcon(const std::string&) { return 1; }
};
inline fui::BitmapRef listIconFor(int) { return {}; }
struct Renderer {
  std::vector<std::string> prewarmed;
  unsigned calls = 0;
  template <typename Fn>
  void prewarmFallbackText(int, Fn fn, const void* ctx, uint32_t count) {
    ++calls;
    prewarmed.clear();
    for (uint32_t i = 0; i < count; ++i) prewarmed.emplace_back(fn(ctx, i));
  }
};
struct Scale {
  int smallFontId = 1;
};
inline Scale uiScaleSpec() { return {}; }
struct App {
  void clearTapFlash() {}
};
struct KeyboardResult {
  std::string text;
};
struct FilePathResult {
  std::string path;
};
struct ActivityResult {
  std::variant<KeyboardResult, FilePathResult> data;
  bool isCancelled = false;
};
enum class InputType { Text };
struct Child {
  std::string initial;
  virtual ~Child() = default;
};
struct KeyboardEntryActivity : Child {
  KeyboardEntryActivity(Renderer&, Input&, const char*, const std::string& text, size_t, InputType) { initial = text; }
};
struct ConfirmationActivity : Child {
  ConfirmationActivity(Renderer&, Input&, const std::string&, const std::string& text) { initial = text; }
};
struct Popup {
  bool active = false;
  std::function<void(int)> callback;
  int inputCalls = 0;
  bool isActive() const { return active; }
  void show(StrId, const StrId*, int, int, std::function<void(int)> cb) {
    active = true;
    callback = std::move(cb);
  }
  template <typename Fn>
  bool handleInput(Input& input, Fn) {
    if (!active) return false;
    ++inputCalls;
    if (input.confirmReleased) {
      active = false;
      callback(0);
    }
    if (input.backReleased) active = false;
    return true;
  }
};
struct RecentStore {
  std::string oldPath, newPath;
  unsigned changes = 0;
  void updatePath(const std::string& oldValue, const std::string& newValue, const std::string&, const std::string&) {
    oldPath = oldValue;
    newPath = newValue;
    ++changes;
  }
};
inline RecentStore RECENT_BOOKS;
struct State {
  std::string openEpubPath;
  unsigned saves = 0;
  bool saveToFile() {
    ++saves;
    return true;
  }
};
inline State APP_STATE;
namespace library {
inline unsigned dirtyCalls = 0;
inline bool markLibraryIndexDirty() {
  ++dirtyCalls;
  librarySession.invalidate();
  return true;
}
}  // namespace library
class FileBrowserActivity {
 public:
  enum class Mode { Books, PickFirmware };
  Mode mode = Mode::Books;
  std::string basepath = "/", searchQuery, foldedQuery;
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer = std::make_unique<char[]>(NAME_BUFFER_SIZE);
  FolderSearch<HalFile> folderSearch;
  bool searching = false, searchIncomplete = false, swallowConfirmRelease = false, swallowBackRelease = false;
  static constexpr size_t ROW_NAME_BUF_SIZE = 512;
  char rowNameBuf[ROW_NAME_BUF_SIZE]{}, rowExtBuf[16]{};
  static constexpr int PREWARM_WINDOW = 24;
  int prewarmedStart = -1;
  unsigned routingCloses = 0, updates = 0, homes = 0;
  bool finished = false;
  std::string selectedBook;
  Renderer renderer;
  Input mappedInput;
  App app;
  Popup optionPopup;
  fui::ListNav nav;
  ActivityResult result;
  std::unique_ptr<Child> child;
  std::function<void(const ActivityResult&)> callback;
  void closeRouting() { ++routingCloses; }
  void requestUpdate(bool = false) { ++updates; }
  void setResult(ActivityResult value) { result = std::move(value); }
  void finish() { finished = true; }
  void onSelectBook(const std::string& path) { selectedBook = path; }
  void onGoHome() { ++homes; }
  template <typename T, typename Fn>
  void startActivityForResult(std::unique_ptr<T> next, Fn cb) {
    child = std::move(next);
    callback = cb;
  }
  bool removeDirFile(const std::string& path) { return Storage.remove(path.c_str()); }
  int fileRowOffset() const { return mode == Mode::Books ? 1 : 0; }
  int listCount() const { return static_cast<int>(files.size()) + fileRowOffset(); }
  void loadFiles();
  void advanceSearch();
  void openSearch();
  void clearSearch();
  static void provideRow(void*, uint16_t, fui::ListItem&);
  void prewarmRowGlyphs(int);
  void activateSelected();
  void showEntryActions();
  void deleteSelected();
  void startRename();
  void renameSelectedFile(const std::string&, const std::string&, const std::string&, const std::string&);
  bool handleCustomInput();
  bool handleButtons();
  size_t findEntry(const std::string&) const;
};
std::string getBookCachePath(const std::string&);
std::string getFileExtension(const std::string&);
