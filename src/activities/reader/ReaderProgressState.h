#pragma once

#include <cstdint>
#include <optional>

// Compare the saved payload, including an estimated total while a section is partial.
class ReaderProgressState {
 public:
  struct Position {
    int spineIndex = -1;
    int pageNumber = -1;
    int pageCount = -1;
    std::optional<uint32_t> visibleTextOffset;

    bool operator==(const Position&) const = default;
  };

  bool needsSave(const Position& position) const { return position != lastSaved; }

  void recordSaveResult(const Position& position, const bool succeeded) {
    if (succeeded) lastSaved = position;
  }

  void reset() { lastSaved = {}; }

 private:
  Position lastSaved;
};
