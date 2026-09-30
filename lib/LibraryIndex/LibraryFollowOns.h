#pragma once

// Books to offer when a reader reaches the end of a book, taken from the Library
// index so they work when every book sits in its own folder (Calibre's
// "Author/Title/file.epub" layout), where a folder scan finds nothing.

#include <cstddef>
#include <string>
#include <string_view>

#include "LibraryIndexFile.h"

namespace library {

struct FollowOn {
  std::string path;
  std::string title;
};

// Fills `out` with up to `maxCount` books that follow `bookPath`: the later
// volumes of its series in series order, or, when it has none, the same author's
// later titles in author order. Finished books and files missing from the card
// are skipped. Returns how many were written; 0 when the book is not in the
// index or nothing follows it. `index` must be open.
size_t findFollowOns(LibraryIndexFile& index, std::string_view bookPath, FollowOn* out, size_t maxCount);

}  // namespace library
