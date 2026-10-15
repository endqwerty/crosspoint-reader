#pragma once

#include <cstdint>
#include <string>

// Reader caches are keyed by the book's path, so a book replaced at the same path
// would keep its old cache. source.bin records the size and FAT timestamp of the
// file each cache was built from.
namespace CacheSource {

enum class Result : uint8_t { Unchanged, Replaced, Failed };

// Removes everything but progress.bin and source.bin from cacheDir when the book
// changed since the cache was built; record() replaces the stamp after the
// rebuild. A cache without source.bin is taken as built from the current file.
// Failed means stale entries may remain, so the cache must not be used.
Result discardIfChanged(const std::string& bookPath, const std::string& cacheDir);

// Records the current file after a cache build.
void record(const std::string& bookPath, const std::string& cacheDir);

}  // namespace CacheSource
