#include "LibraryBookState.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace library {
namespace {
constexpr char DIRECTORY[] = "/.crosspoint/library-state";
constexpr size_t RECORD_BYTES = 16;
constexpr size_t PATH_BYTES = sizeof(DIRECTORY) + 16 + sizeof(".bin");
void statePath(char* out, const size_t size, const uint64_t key, const char* extension) {
  snprintf(out, size, "%s/%08lx%08lx.%s", DIRECTORY, static_cast<unsigned long>(key >> 32),
           static_cast<unsigned long>(key & 0xffffffffu), extension);
}
bool readRecord(const char* path, const uint64_t key, BookState& out) {
  HalFile file;
  if (!Storage.openFileForRead("LIBSTATE", path, file)) return false;
  uint8_t record[RECORD_BYTES]{};
  if (file.fileSize() != sizeof(record) || file.read(record, sizeof(record)) != sizeof(record) ||
      memcmp(record, "LBS1", 4) != 0 || record[12] > 5 || record[13] != static_cast<uint8_t>(record[12] ^ 0xa5) ||
      record[14] != 0 || record[15] != 0)
    return false;
  for (size_t i = 0; i < sizeof(key); ++i) {
    if (record[4 + i] != static_cast<uint8_t>(key >> (i * 8))) return false;
  }
  out.favorite = (record[12] & 1u) != 0;
  out.reading = static_cast<ReadingState>(record[12] >> 1);
  return true;
}
}  // namespace

uint64_t bookStateKey(const std::string_view path) {
  uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char c : path) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool readBookState(const uint64_t key, BookState& out) {
  out = {};
  char path[PATH_BYTES];
  statePath(path, sizeof(path), key, "bin");
  if (Storage.exists(path)) {
    if (readRecord(path, key, out)) return true;
    LOG_ERR("LIBSTATE", "invalid or unreadable state: %s", path);
  }
  statePath(path, sizeof(path), key, "bak");
  if (Storage.exists(path)) return readRecord(path, key, out);
  statePath(path, sizeof(path), key, "bin");
  return !Storage.exists(path);
}

bool writeBookState(const uint64_t key, const BookState& state) {
  if (state.reading > ReadingState::Finished) return false;
  BookState old;
  if (!readBookState(key, old)) {
    LOG_ERR("LIBSTATE", "cannot preserve unreadable state");
    return false;
  }
  if (old.favorite == state.favorite && old.reading == state.reading) return true;
  if (!Storage.mkdir(DIRECTORY) && !Storage.exists(DIRECTORY)) {
    LOG_ERR("LIBSTATE", "cannot create state directory");
    return false;
  }
  char path[PATH_BYTES], temp[PATH_BYTES], backup[PATH_BYTES];
  statePath(path, sizeof(path), key, "bin");
  statePath(temp, sizeof(temp), key, "new");
  statePath(backup, sizeof(backup), key, "bak");
  uint8_t record[RECORD_BYTES]{};
  memcpy(record, "LBS1", 4);
  for (size_t i = 0; i < sizeof(key); ++i) record[4 + i] = static_cast<uint8_t>(key >> (i * 8));
  record[12] = (static_cast<uint8_t>(state.reading) << 1) | static_cast<uint8_t>(state.favorite);
  record[13] = record[12] ^ 0xa5;
  HalFile file;
  if (!Storage.openFileForWrite("LIBSTATE", temp, file)) return false;
  const bool written = file.write(record, sizeof(record)) == sizeof(record);
  const bool closed = file.close();
  if (!written || !closed) {
    LOG_ERR("LIBSTATE", "state write failed");
    return false;
  }
  BookState verified;
  if (!readRecord(temp, key, verified) || verified.favorite != state.favorite || verified.reading != state.reading) {
    LOG_ERR("LIBSTATE", "staged state validation failed");
    return false;
  }
  // Canonicalize the readable copy before removing a backup. A corrupt primary
  // must never replace the valid backup that readBookState recovered from.
  const bool primaryExists = Storage.exists(path);
  const bool primaryValid = primaryExists && readRecord(path, key, verified);
  if (!primaryValid && Storage.exists(backup)) {
    if (!readRecord(backup, key, verified) || (primaryExists && !Storage.remove(path)) ||
        !Storage.rename(backup, path)) {
      LOG_ERR("LIBSTATE", "state backup recovery failed");
      return false;
    }
  } else if (primaryExists && !primaryValid) {
    LOG_ERR("LIBSTATE", "primary state became unreadable");
    return false;
  }
  if (Storage.exists(backup) && !Storage.remove(backup)) {
    LOG_ERR("LIBSTATE", "cannot remove stale state backup");
    return false;
  }
  const bool hadPrevious = Storage.exists(path);
  if (hadPrevious && !Storage.rename(path, backup)) return false;
  if (!Storage.rename(temp, path)) {
    if (hadPrevious && !Storage.rename(backup, path)) LOG_ERR("LIBSTATE", "state rollback failed; backup retained");
    LOG_ERR("LIBSTATE", "state replacement failed");
    return false;
  }
  if (hadPrevious && !Storage.remove(backup)) LOG_ERR("LIBSTATE", "state backup retained");
  return true;
}

bool markBookReading(const std::string_view path) {
  const uint64_t key = bookStateKey(path);
  BookState state;
  if (!readBookState(key, state)) return false;
  if (state.reading != ReadingState::Unread) return true;
  state.reading = ReadingState::Reading;
  return writeBookState(key, state);
}
}  // namespace library
