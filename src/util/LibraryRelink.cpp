#include "LibraryRelink.h"

#include <Logging.h>

#include "BookStateMove.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"

bool relinkRenamedBook(const std::string& oldPath, const std::string& newPath) {
  if (!relinkBookState(oldPath, newPath)) return false;
  LOG_INF("RELINK", "Kept reading state: %s -> %s", oldPath.c_str(), newPath.c_str());
  RECENT_BOOKS.updatePath(oldPath, newPath, getBookCachePath(oldPath), getBookCachePath(newPath));
  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    if (!APP_STATE.saveToFile()) LOG_ERR("RELINK", "Failed to save relinked open-book path");
  }
  return true;
}
