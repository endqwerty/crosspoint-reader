#pragma once

#include <FsHelpers.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Just enough of Epub for the production resolveHrefToSpineIndex() body.
class BookMetadataCache {
 public:
  struct SpineEntry {
    std::string href;
  };
  bool isLoaded() const { return true; }
};

class Epub {
 public:
  explicit Epub(std::vector<std::string> hrefs)
      : bookMetadataCache(std::make_unique<BookMetadataCache>()), spine(std::move(hrefs)) {}

  int resolveHrefToSpineIndex(const std::string& href) const;

 private:
  BookMetadataCache::SpineEntry getSpineItem(const int i) const { return {spine[static_cast<size_t>(i)]}; }
  int getSpineItemsCount() const { return static_cast<int>(spine.size()); }

  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  std::vector<std::string> spine;
};
