#pragma once
#include <BookMetadataCache.h>

#include <memory>

// Only the state needed by the extracted production accessors and EOF predicate.
struct Epub {
  std::shared_ptr<BookMetadataCache> bookMetadataCache;
  int getSpineItemsCount() const;
  size_t getCumulativeSpineItemSize(int spineIndex) const;
};
struct EpubReaderActivity {
  Epub* epub;
  int currentSpineIndex;
  bool isAtEndOfBook() const;
};
