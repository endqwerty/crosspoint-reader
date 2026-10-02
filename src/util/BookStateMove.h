#pragma once

#include <string>

// Reader cache directory for a book path, or empty when the file type has none.
std::string getBookCachePath(const std::string& path);

// True when no book/cache/bookmarks/key/rights exist and Library state is readable and default.
bool isBookPathFree(const std::string& path);

// Moves a book together with its path-keyed state: reader cache, bookmark files
// and Library reading state, plus reflowable-book .key/.rights sidecars.
// On failure rollback is attempted for every moved path; rollback I/O failures are logged.
bool moveBookWithState(const std::string& oldPath, const std::string& newPath);

// Carries the state of a book that was already renamed outside the device (a
// Calibre re-export) from its old path to its new one: reader cache, bookmark
// files and Library reading state, with rollback attempted on failure and best-effort
// cleanup of old reading state. Does nothing, returning false, unless the old book is gone, the
// new book exists, the old path has state and the new path has none yet: state
// the user already built at the new path is never overwritten.
bool relinkBookState(const std::string& oldPath, const std::string& newPath);
