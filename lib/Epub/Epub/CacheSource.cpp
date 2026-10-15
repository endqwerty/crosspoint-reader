#include "CacheSource.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <cstring>

namespace {
constexpr char STAMP_NAME[] = "source.bin";
constexpr char PROGRESS_NAME[] = "progress.bin";

struct Stamp {
  uint32_t size;
  uint32_t time;
};

std::string stampPath(const std::string& cacheDir) { return cacheDir + "/" + STAMP_NAME; }

bool readCurrent(const std::string& bookPath, Stamp& out) {
  HalFile file;
  if (!Storage.openFileForRead("EBP", bookPath, file)) return false;
  out = {static_cast<uint32_t>(file.fileSize()), file.modificationTime()};
  return true;
}

void writeStamp(const std::string& cacheDir, const Stamp& stamp) {
  HalFile file;
  if (!Storage.openFileForWrite("EBP", stampPath(cacheDir), file) ||
      file.write(&stamp, sizeof(stamp)) != sizeof(stamp) || !file.close()) {
    LOG_ERR("EBP", "Could not record cache source: %s", cacheDir.c_str());
  }
}

// FAT keeps entry positions when an entry is deleted, so one pass removes
// everything; the pass that finds nothing left confirms it.
bool removeDerived(const std::string& cacheDir) {
  char name[128];
  bool removedAny = true;
  while (removedAny) {
    removedAny = false;
    HalFile dir = Storage.open(cacheDir.c_str());
    if (!dir || !dir.isDirectory()) return false;
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      if (std::strcmp(name, PROGRESS_NAME) == 0 || std::strcmp(name, STAMP_NAME) == 0) continue;
      const bool isDir = entry.isDirectory();
      entry.close();
      const std::string path = cacheDir + "/" + name;
      if (!(isDir ? Storage.removeDir(path.c_str()) : Storage.remove(path.c_str()))) {
        LOG_ERR("EBP", "Could not remove stale cache entry: %s", path.c_str());
        return false;
      }
      removedAny = true;
    }
    if (dir.hasError()) return false;
  }
  return true;
}
}  // namespace

namespace CacheSource {

Result discardIfChanged(const std::string& bookPath, const std::string& cacheDir) {
  Stamp current{};
  if (!readCurrent(bookPath, current)) return Result::Unchanged;
  const std::string index = cacheDir + "/book.bin";
  const std::string stamp = stampPath(cacheDir);
  if (!Storage.exists(stamp.c_str())) {
    if (Storage.exists(index.c_str())) writeStamp(cacheDir, current);
    return Result::Unchanged;
  }
  HalFile file;
  if (!Storage.openFileForRead("EBP", stamp, file)) return Result::Failed;
  Stamp cached{};
  const bool same = file.read(&cached, sizeof(cached)) == sizeof(cached) && cached.size == current.size &&
                    cached.time == current.time;
  file.close();
  if (same) return Result::Unchanged;
  LOG_INF("EBP", "Book changed since it was cached, rebuilding: %s", bookPath.c_str());
  // The index goes first: without it a partly removed cache is never loaded.
  if (Storage.exists(index.c_str()) && !Storage.remove(index.c_str())) return Result::Failed;
  return removeDerived(cacheDir) ? Result::Replaced : Result::Failed;
}

void record(const std::string& bookPath, const std::string& cacheDir) {
  Stamp current{};
  if (readCurrent(bookPath, current)) writeStamp(cacheDir, current);
}

}  // namespace CacheSource
