#pragma once

#include <cstdint>
#include <string_view>

namespace library {
enum class ReadingState : uint8_t { Unread, Reading, Finished };
enum class ShelfFilter : uint8_t { All, Favorites, Unread, Reading, Finished };
struct BookState {
  bool favorite = false;
  ReadingState reading = ReadingState::Unread;
};
uint64_t bookStateKey(std::string_view path);
bool readBookState(uint64_t key, BookState& out);
bool writeBookState(uint64_t key, const BookState& state);
// Deletes the state record and its backup/staging files; true when none remain.
bool removeBookState(uint64_t key);
bool markBookReading(std::string_view path);
constexpr bool matchesShelfFilter(const BookState& state, const ShelfFilter filter) {
  switch (filter) {
    case ShelfFilter::All:
      return true;
    case ShelfFilter::Favorites:
      return state.favorite;
    case ShelfFilter::Unread:
      return state.reading == ReadingState::Unread;
    case ShelfFilter::Reading:
      return state.reading == ReadingState::Reading;
    case ShelfFilter::Finished:
      return state.reading == ReadingState::Finished;
  }
  return false;
}
}  // namespace library
