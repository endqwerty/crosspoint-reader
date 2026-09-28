#pragma once

#include <cstdint>
#include <optional>

// Back navigation is shared by ordinary links and temporary footnote excursions.
class ReaderNavigationHistory {
 public:
  enum class Jump { Link, Footnote };
  struct Position {
    int spineIndex;
    int pageNumber;
    Jump jump;
    int pageCount;
    std::optional<uint32_t> visibleTextOffset;
  };

  static constexpr int CAPACITY = 3;

  bool empty() const { return count == 0; }
  int size() const { return count; }
  void clear() { count = 0; }
  // Oldest first; index must be below size().
  const Position& at(int index) const { return positions[index]; }

  void push(int spineIndex, int pageNumber, Jump jump, int pageCount = 0,
            std::optional<uint32_t> visibleTextOffset = std::nullopt) {
    if (count == CAPACITY) {
      // Keep the outermost note's resume point even when nested navigation fills the history.
      const int evict = positions[0].jump == Jump::Footnote ? 1 : 0;
      for (int i = evict; i < count - 1; ++i) positions[i] = positions[i + 1];
      --count;
    }
    positions[count++] = {spineIndex, pageNumber, jump, pageCount, visibleTextOffset};
  }

  std::optional<Position> pop() {
    if (empty()) return std::nullopt;
    return positions[--count];
  }

  const Position* footnoteOrigin() const {
    for (int i = 0; i < count; ++i) {
      if (positions[i].jump == Jump::Footnote) return &positions[i];
    }
    return nullptr;
  }

 private:
  Position positions[CAPACITY]{};
  int count = 0;
};
