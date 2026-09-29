#include "BookStateMove.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <LibraryBookState.h>
#include <Logging.h>
#include <Memory.h>

#include <functional>
#include <string_view>

#include "BookmarkUtil.h"
#include "ClippingStore.h"

std::string getBookCachePath(const std::string& path) {
  const char* prefix = nullptr;
  if (FsHelpers::hasReflowableBookExtension(std::string_view(path))) {
    prefix = "epub_";
  } else if (FsHelpers::hasXtcExtension(path)) {
    prefix = "xtc_";
  } else {
    return "";
  }
  return std::string("/.crosspoint/") + prefix + std::to_string(std::hash<std::string>{}(path));
}

namespace {
constexpr int STATE_PATH_COUNT = 4;

// Path-keyed state beside a book: reader cache, then bookmark primary, backup and staging files.
void getStatePaths(const std::string& bookPath, const bool withBookmarks, std::string (&paths)[STATE_PATH_COUNT]) {
  paths[0] = getBookCachePath(bookPath);
  if (!withBookmarks) return;
  paths[1] = BookmarkUtil::getBookmarkPath(bookPath);
  paths[2] = paths[1] + ".bak";
  paths[3] = paths[1] + ".new";
}

// The move owns a bounded set of paths only while it runs. Keeping this
// transaction on the heap avoids a large task-stack frame.
class RenameState {
 public:
  RenameState(const std::string& oldPath, const std::string& newPath)
      : oldKey(library::bookStateKey(oldPath)), newKey(library::bookStateKey(newPath)) {
    const bool withBookmarks = FsHelpers::hasReflowableBookExtension(std::string_view(oldPath));
    getStatePaths(oldPath, withBookmarks, oldPaths);
    getStatePaths(newPath, withBookmarks, newPaths);
  }
  ~RenameState() {
    if (committed) return;
    for (int i = STATE_PATH_COUNT - 1; i >= 0; --i) {
      if (moved[i] && !Storage.rename(newPaths[i].c_str(), oldPaths[i].c_str())) {
        LOG_ERR("BookMove", "Failed to roll back rename state: %s", oldPaths[i].c_str());
      }
    }
    if (stateWritten && !library::writeBookState(newKey, previousState)) {
      LOG_ERR("BookMove", "Failed to restore rename target reading state");
    }
  }
  bool prepare() {
    for (int i = 0; i < STATE_PATH_COUNT; ++i) {
      if (!newPaths[i].empty() && oldPaths[i] != newPaths[i] && Storage.exists(newPaths[i].c_str())) {
        LOG_ERR("BookMove", "Rename state target already exists: %s", newPaths[i].c_str());
        return false;
      }
    }
    library::BookState state;
    if (!library::readBookState(oldKey, state) || !library::readBookState(newKey, previousState)) {
      LOG_ERR("BookMove", "Cannot read rename reading state");
      return false;
    }
    if (!library::writeBookState(newKey, state)) {
      LOG_ERR("BookMove", "Cannot preserve rename reading state");
      return false;
    }
    stateWritten = oldKey != newKey;
    for (int i = 0; i < STATE_PATH_COUNT; ++i) {
      if (oldPaths[i].empty() || oldPaths[i] == newPaths[i] || !Storage.exists(oldPaths[i].c_str())) continue;
      if (!Storage.rename(oldPaths[i].c_str(), newPaths[i].c_str())) {
        LOG_ERR("BookMove", "Failed to move rename state: %s", oldPaths[i].c_str());
        return false;
      }
      moved[i] = true;
    }
    return true;
  }
  void commit() { committed = true; }

 private:
  std::string oldPaths[STATE_PATH_COUNT], newPaths[STATE_PATH_COUNT];
  bool moved[STATE_PATH_COUNT]{};
  uint64_t oldKey, newKey;
  library::BookState previousState;
  bool stateWritten = false;
  bool committed = false;
};
}  // namespace

bool isBookPathFree(const std::string& path) {
  if (Storage.exists(path.c_str())) return false;
  std::string paths[STATE_PATH_COUNT];
  getStatePaths(path, FsHelpers::hasReflowableBookExtension(std::string_view(path)), paths);
  for (const std::string& statePath : paths) {
    if (!statePath.empty() && Storage.exists(statePath.c_str())) return false;
  }
  return true;
}

bool moveBookWithState(const std::string& oldPath, const std::string& newPath) {
  auto state = makeUniqueNoThrow<RenameState>(oldPath, newPath);
  if (!state) {
    LOG_ERR("BookMove", "OOM: move state");
    return false;
  }
  if (!state->prepare()) return false;
  if (!ClippingStore::moveBook(oldPath, newPath)) {
    LOG_ERR("BookMove", "Failed to move book: %s -> %s", oldPath.c_str(), newPath.c_str());
    return false;
  }
  state->commit();
  return true;
}
