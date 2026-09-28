#pragma once
#include <string>
#include <vector>

#include "../BookmarkEntry.h"

// Per-book bookmark persistence. Takes the book's path; bookmark-file path
// derivation (BookmarkUtil) and directory creation are hidden inside.
namespace BookmarkFile {

// Serialize a proposed edit before changing the live list. The predicate must
// be side-effect free; its context is borrowed only for this synchronous save.
struct SaveOptions {
  const BookmarkEntry* prepend = nullptr;
  bool (*exclude)(const BookmarkEntry&, const void*) = nullptr;
  const void* context = nullptr;
};

// Loads the bookmarks for bookPath. The vector is cleared first; a missing or
// empty bookmark file yields an empty list and returns false. On failure, exists
// is false only when absence is known; unreadable files must not be overwritten.
bool load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks, bool* exists = nullptr);

// Saves the bookmarks for bookPath, creating the bookmarks directory as needed.
bool save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks, const SaveOptions& options = {});

}  // namespace BookmarkFile
