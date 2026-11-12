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
inline bool slideSwitchOwns(uint8_t) { return slideSwitchOwned; }

namespace bleturner {
struct Status {
  bool compiledIn = true;
};
inline Status statusValue;
inline Status status() { return statusValue; }
}  // namespace bleturner

struct KOReaderStoreFixture {
  bool credentials = true;
  bool hasCredentials() const { return credentials; }
};
inline KOReaderStoreFixture KOREADER_STORE;

namespace pluginevents {
enum class Event : uint8_t { COUNT };
inline bool anySubscriber(Event) { return false; }
}  // namespace pluginevents

struct CrossPointSettings {
  enum { CHAPTER_PAGE_NUMBERS, BOOK_PAGE_NUMBERS };
  enum { SLIDE_SWITCH_NIGHT_MODE = 1 };
  struct {
    char peerAddr[18] = "00:11:22:33:44:55";
  } ble;
  char dictionaryName[32] = "dictionary";
  uint8_t statusBarPageNumbers = CHAPTER_PAGE_NUMBERS;
};
inline CrossPointSettings SETTINGS;
