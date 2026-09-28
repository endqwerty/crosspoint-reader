#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
struct State {
  std::string openEpubPath;
  std::vector<std::string> savedPaths;
  void saveToFile() { savedPaths.push_back(openEpubPath); }
};
inline State APP_STATE;
struct Recent {
  int additions = 0;
  std::string path, title, author, thumb;
  void addBook(const std::string& p, const std::string& t, const std::string& a, const std::string& b) {
    ++additions;
    path = p;
    title = t;
    author = a;
    thumb = b;
  }
};
inline Recent RECENT_BOOKS;
struct StorageSeam {
  bool present = true;
  bool exists(const char*) const { return present; }
};
inline StorageSeam Storage;
struct Renderer {
  bool committed = true;
  int displays = 0;
  void clearScreen() {}
  void displayBuffer() { ++displays; }
  bool displayCommitted() const { return committed; }
};
struct FontSystem {
  void ensureLoaded(Renderer&) {}
};
inline FontSystem sdFontSystem;
struct Activity {
  void onEnter() {}
};
struct RenderLock {};
inline unsigned long millis() { return 4321; }
namespace trustedtime {
inline int64_t trustedNow() { return 1700000000; }
}  // namespace trustedtime
namespace pluginevents {
enum class Event { ReaderOpen };
struct Var {
  const char* key;
  const char* value;
};
inline std::vector<std::string> openedBooks;
inline void emit(Event, const Var* vars, size_t count) {
  openedBooks.emplace_back(count == 1 && std::string(vars[0].key) == "book" ? vars[0].value : "");
}
}  // namespace pluginevents
struct ReaderSession {
  int renders = 0;
  uint32_t lastMs = 0;
  int64_t lastEpoch = 0;
  int lastProgressBp = -1;
  void onRenderComplete(uint32_t monotonicMs, int64_t trustedEpoch, int progressBp) {
    ++renders;
    lastMs = monotonicMs;
    lastEpoch = trustedEpoch;
    lastProgressBp = progressBp;
  }
};
struct EndOfBookOptions {
  explicit EndOfBookOptions(Renderer&) {}
  void loadOnce(const std::string&) {}
  void render(Renderer&, int) {}
};
template <class T, class... Args>
auto makeUniqueNoThrow(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct ReaderActivity : Activity {
  std::string bookPath = "/books/current.epub";
  bool bookRemembered = false;
  std::atomic<bool> pageRendered{false};
  std::atomic<bool> endOfBookOptionsReady{false};
  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  Renderer renderer;
  int mappedInput = 0, loads = 0, updates = 0, finishes = 0, renders = 0;
  bool loadSucceeds = true, end = false, rememberedDuringLoad = false, loadFailureHandled = false;
  ReaderSession readerSession;
  bool handleLoadFailure() { return loadFailureHandled; }
  int getProgressBasisPoints() const { return 2500; }
  std::string getBookTitle() const { return "A title"; }
  std::string getBookAuthor() const { return "An author"; }
  std::string getBookThumbBmpPath() const { return "/thumb.bmp"; }
  bool loadBook() {
    ++loads;
    rememberedDuringLoad = !APP_STATE.openEpubPath.empty();
    return loadSucceeds;
  }
  void finish() { ++finishes; }
  void requestUpdate() { ++updates; }
  void applyInitialOrientation() {}
  bool isAtEndOfBook() const { return end; }
  void onEndOfBookRendered() {}
  void renderBook() { ++renders; }
#include "ReaderPublication.h"
  void rememberBookOnceRendered();
  void onEnter();
  void render(RenderLock&&);
};
