#include "AtomicFile.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace atomic_file {
namespace {
std::mutex transactionMutex;

bool verify(const char* path, const char* data, const size_t size) {
  HalFile file;
  if (!Storage.openFileForRead("PERSIST", path, file) || file.fileSize() != size) return false;
  char buffer[128];
  for (size_t offset = 0; offset < size;) {
    const size_t count = std::min(sizeof(buffer), size - offset);
    if (file.read(buffer, count) != static_cast<int>(count) || memcmp(buffer, data + offset, count) != 0) return false;
    offset += count;
  }
  return true;
}

bool restore(const char* path, const char* backup) {
  if (!Storage.exists(backup)) return true;
  if ((Storage.exists(path) && !Storage.remove(path)) || !Storage.rename(backup, path)) {
    LOG_ERR("PERSIST", "Could not restore %s; backup retained", path);
    return false;
  }
  return true;
}
}  // namespace

bool write(const char* path, const char* data, const size_t size) {
  const std::lock_guard<std::mutex> lock(transactionMutex);
  if (size > MAX_FILE_BYTES) {
    LOG_ERR("PERSIST", "Document exceeds %zu bytes: %s", MAX_FILE_BYTES, path);
    return false;
  }
  const size_t pathLength = strlen(path);
  // Paths vary with the book's SD location; one checked allocation holds both suffixes.
  auto paths = makeUniqueNoThrow<char[]>(2 * (pathLength + 5));
  if (!paths) {
    LOG_ERR("PERSIST", "OOM staging paths for %s", path);
    return false;
  }
  char* const backup = paths.get();
  char* const temporary = backup + pathLength + 5;
  memcpy(backup, path, pathLength);
  memcpy(backup + pathLength, ".bak", 5);
  memcpy(temporary, path, pathLength);
  memcpy(temporary + pathLength, ".new", 5);
  if (!restore(path, backup)) return false;

  HalFile file;
  if (!Storage.openFileForWrite("PERSIST", temporary, file)) return false;
  const bool written = file.write(reinterpret_cast<const uint8_t*>(data), size) == size;
  // The handle must close successfully before verification or either rename.
  const bool closed = file.close();
  if (!written || !closed || !verify(temporary, data, size)) {
    LOG_ERR("PERSIST", "Staged write failed for %s", path);
    return false;
  }

  const bool hadPrevious = Storage.exists(path);
  if (hadPrevious && !Storage.rename(path, backup)) {
    LOG_ERR("PERSIST", "Could not retain previous %s", path);
    return false;
  }
  if (!Storage.rename(temporary, path)) {
    LOG_ERR("PERSIST", "Could not install %s", path);
    restore(path, backup);
    return false;
  }
  if (hadPrevious && !Storage.remove(backup)) {
    // Readers keep using the old copy; a retry first restores it to the primary.
    LOG_ERR("PERSIST", "Could not commit %s; backup retained", path);
    return false;
  }
  return true;
}

String read(const char* path, bool* exists) {
  const std::lock_guard<std::mutex> lock(transactionMutex);
  // An allocation or I/O failure must be reported as a failed read, not first boot.
  if (exists) *exists = true;
  const size_t pathLength = strlen(path);
  auto backup = makeUniqueNoThrow<char[]>(pathLength + 5);
  if (!backup) {
    LOG_ERR("PERSIST", "OOM reading path for %s", path);
    return {};
  }
  memcpy(backup.get(), path, pathLength);
  memcpy(backup.get() + pathLength, ".bak", 5);
  // A remaining backup means an interrupted or failed transaction.
  const char* selected = Storage.exists(backup.get()) ? backup.get() : path;
  const bool found = Storage.exists(selected);
  if (exists) *exists = found;
  if (!found) return {};
  HalFile file;
  if (!Storage.openFileForRead("PERSIST", selected, file)) return {};
  const size_t size = file.fileSize();
  String content;
  if (size > MAX_FILE_BYTES || !content.reserve(size)) {
    LOG_ERR("PERSIST", "Cannot allocate %zu-byte document: %s", size, path);
    return {};
  }
  char buffer[128];
  for (size_t offset = 0; offset < size;) {
    const size_t count = std::min(sizeof(buffer), size - offset);
    if (file.read(buffer, count) != static_cast<int>(count) || !content.concat(buffer, count)) {
      LOG_ERR("PERSIST", "Incomplete read for %s", path);
      return {};
    }
    offset += count;
  }
  return content;
}
}  // namespace atomic_file
