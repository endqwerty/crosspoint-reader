#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Every optional reader-menu entry can be switched on from here.
struct FrontlightFixture {
  bool fitted = true;
  bool present() const { return fitted; }
};
inline FrontlightFixture Frontlight;
inline bool slideSwitchOwned = false;
inline bool slideSwitchOwnsLight() { return slideSwitchOwned; }

namespace bleturner {
struct Status {
  bool compiledIn = true;
};
inline Status statusValue;
inline Status status() { return statusValue; }
}  // namespace bleturner

struct CrossPointSettings {
  enum { CHAPTER_PAGE_NUMBERS, BOOK_PAGE_NUMBERS };
  char dictionaryName[32] = "dictionary";
  uint8_t statusBarPageNumbers = CHAPTER_PAGE_NUMBERS;
};
inline CrossPointSettings SETTINGS;
