#pragma once

#include <string>

// Rename handler for `library::buildLibraryIndex`: a book that moved from oldPath
// to newPath outside the device (same book UUID) keeps its reader cache,
// bookmarks and Library reading state, and its Recent entry and open-book path
// follow it. Returns true when state was carried over.
bool relinkRenamedBook(const std::string& oldPath, const std::string& newPath);
