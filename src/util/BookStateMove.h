#pragma once

#include <string>

// Reader cache directory for a book path, or empty when the file type has none.
std::string getBookCachePath(const std::string& path);

// True when neither a book nor any of its path-keyed state exists at path.
bool isBookPathFree(const std::string& path);

// Moves a book together with its path-keyed state: reader cache, bookmark files
// and Library reading state. On failure every moved path is restored.
bool moveBookWithState(const std::string& oldPath, const std::string& newPath);
