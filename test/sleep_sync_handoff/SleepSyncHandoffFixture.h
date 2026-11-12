#pragma once

#include <memory>
#include <optional>
#include <string>

#include "CrossPointPosition.h"
#include "src/activities/reader/ReaderProgressState.h"

#define LOG_INF(...) ((void)0)

struct KOReaderStoreStub {
  bool syncOnSleep = true;
  bool credentials = true;
  bool getSyncOnSleep() const { return syncOnSleep; }
  bool hasCredentials() const { return credentials; }
};
inline KOReaderStoreStub KOREADER_STORE;

namespace syncEngine {
inline std::optional<CrossPointPosition> noted;
inline void noteSleepBook(const std::string&, const CrossPointPosition& position) { noted = position; }
}  // namespace syncEngine

struct EpubStub {
  int spineCount = 10;
  std::string getPath() const { return "/Books/Novel.epub"; }
  int getSpineItemsCount() const { return spineCount; }
};

struct SectionStub {
  int estimatedPages = 0;
  int pageCount = 0;
  std::optional<uint16_t> paragraph;
  bool building = false;
  bool partial = false;
  bool isBuilding() const { return building; }
  bool isPartial() const { return partial; }
  int estimatedTotalPages() const { return estimatedPages; }
  std::optional<uint16_t> getParagraphIndexForPage(uint16_t) const { return paragraph; }
};

struct ReaderActivity {
  void prepareForSleep() {}
};

struct EpubReaderActivity : ReaderActivity {
  std::shared_ptr<EpubStub> epub = std::make_shared<EpubStub>();
  std::unique_ptr<SectionStub> section;
  int currentSpineIndex = 0;
  std::optional<ReaderProgressState::Position> displayedPosition;
  bool finishedScreenShown = false;

  bool isAtEndOfBook() const { return epub && currentSpineIndex >= epub->getSpineItemsCount(); }
  void prepareForSleep();
};
