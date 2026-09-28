#pragma once
#include <Arduino.h>
#include <BookMetadataCache.h>
#include <ContentOpfParser.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <TocNavParser.h>
#include <TocNcxParser.h>
#include <Utf8.h>
#include <ZipFile.h>

#include <cstring>
#include <memory>
#include <vector>

#include "Archive.h"
inline uint32_t millis() { return 0; }
class CssParser {
 public:
  enum class CacheStatus { Invalid, Partial, Complete };
  enum class CacheLoadResult { Invalid, LowMemory, Complete };
  enum class ParseResult { Error, Partial, Complete };
  explicit CssParser(const std::string&) {}
  CacheStatus inspectCache() { return CacheStatus::Complete; }
  CacheLoadResult loadFromCache() { return CacheLoadResult::Complete; }
  void deleteCache() {}
  void clear() {}
};
// These fixtures open every book as a plain ZIP; no entry is protected.
namespace freeink::content {
struct ContentDecryptor {};
inline std::unique_ptr<ContentDecryptor> openProtectedBook(const std::string&, std::string&) { return nullptr; }
}  // namespace freeink::content
class Epub {
 public:
  std::string filepath = "/book.epub", cachePath = "/cache", contentBasePath;
  std::unique_ptr<freeink::content::ContentDecryptor> decryptor;
  std::string protectionError;
  std::string tocNavItem, tocNcxItem;
  std::vector<std::string> cssFiles;
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  std::unique_ptr<CssParser> cssParser;
  bool load(bool buildIfMissing = true, bool skipLoadingCss = true);
  struct LibraryMetadata {
    std::string title, author, series, seriesIndexText, titleSort, authorSort, uuid;
  };
  bool parseContentOpf(BookMetadataCache::BookMetadata&, bool writeSpine = true, bool metadataOnly = false,
                       ZipFile* zip = nullptr, LibraryMetadata* libraryOut = nullptr);
  bool parseTocNavFile() const;
  bool parseTocNcxFile() const;
  const std::string& getCachePath() const { return cachePath; }
  const std::string& getBasePath() const { return contentBasePath; }
  bool findContentOpfFile(std::string* path, ZipFile*) const {
    *path = "OEBPS/content.opf";
    return true;
  }
  bool getItemSize(const std::string& path, size_t* size) const { return index_test::size(path, size); }
  bool readItemContentsToStream(const std::string& path, Print& target, size_t chunk, bool shortOk = false) const {
    return index_test::stream(path, target, chunk, shortOk);
  }
  uint8_t* readItemContentsToBytes(const std::string&, size_t*, bool) const { return nullptr; }
  void setupCacheDir() const {}
  void discoverCssFilesFromZip() {}
  CssParser::ParseResult parseCssFiles(CssParser::CacheStatus) { return CssParser::ParseResult::Complete; }
};
