#pragma once

#include <Memory.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace load_test {
enum class Fault { None, BeginWrite, BeginOpf, ParseOpf, EndOpf, BeginToc, EndToc, EndWrite, Build, Reload };
struct State {
  Fault fault = Fault::None;
  bool cached = true, published = false, navOk = true, ncxOk = true, cleanupOk = true;
  int metadataLive = 0, cssLive = 0, metadataPeak = 0, cssPeak = 0, openHandles = 0;
  int metadataCreated = 0, cssCreated = 0, loads = 0, opfCalls = 0, cssParses = 0;
  int cssClears = 0, cssDeletes = 0, sectionDeletes = 0, navCalls = 0, ncxCalls = 0, cleanupCalls = 0;
  int discoveries = 0, directoryCalls = 0, errors = 0;
  int metadataValuesLive = 0, metadataCopies = 0, metadataValuesDuringCss = -1, metadataValuesDuringReload = -1;
  bool opfInputEmpty = false, builtMetadataCorrect = false;
  std::vector<std::string> events;
};
inline State state;
inline void debug(const char*, const char*, ...) {}
inline void error(const char*, const char*, ...) { ++state.errors; }
inline bool step(const char* name, Fault fault) {
  state.events.emplace_back(name);
  return state.fault != fault;
}
}  // namespace load_test
#define LOG_DBG(...) load_test::debug(__VA_ARGS__)
#define LOG_ERR(...) load_test::error(__VA_ARGS__)
inline uint32_t millis() { return 0; }

// Collaborators model cache/CSS outcomes; the complete load method is extracted
// from production. Serialization and parser internals have their own real-code tests.
class BookMetadataCache {
 public:
  struct BookMetadata {
    std::string title, author, language, coverItemHref, textReferenceHref;
    BookMetadata() { ++load_test::state.metadataValuesLive; }
    BookMetadata(const BookMetadata& other)
        : title(other.title),
          author(other.author),
          language(other.language),
          coverItemHref(other.coverItemHref),
          textReferenceHref(other.textReferenceHref) {
      ++load_test::state.metadataValuesLive;
      ++load_test::state.metadataCopies;
    }
    ~BookMetadata() { --load_test::state.metadataValuesLive; }
  } coreMetadata;
  explicit BookMetadataCache(const std::string&) {
    auto& s = load_test::state;
    ++s.metadataCreated;
    s.metadataPeak = std::max(s.metadataPeak, ++s.metadataLive);
    s.events.emplace_back("metadata+");
  }
  ~BookMetadataCache() {
    close();
    --load_test::state.metadataLive;
    load_test::state.events.emplace_back("metadata-");
  }
  bool load() {
    close();
    auto& s = load_test::state;
    ++s.loads;
    if (s.loads > 1) s.metadataValuesDuringReload = s.metadataValuesLive;
    s.events.emplace_back("load");
    if ((s.loads > 1 && s.fault == load_test::Fault::Reload) || (!s.cached && !s.published)) return false;
    coreMetadata.title = "Cached title";
    coreMetadata.author = "Cached author";
    open();
    return true;
  }
  bool beginWrite() {
    close();
    return load_test::step("beginWrite", load_test::Fault::BeginWrite);
  }
  bool beginContentOpfPass() {
    if (!load_test::step("beginOpf", load_test::Fault::BeginOpf)) return false;
    open();
    return true;
  }
  bool endContentOpfPass() {
    close();
    return load_test::step("endOpf", load_test::Fault::EndOpf);
  }
  bool beginTocPass() {
    if (!load_test::step("beginToc", load_test::Fault::BeginToc)) return false;
    open();
    return true;
  }
  bool endTocPass() {
    close();
    return load_test::step("endToc", load_test::Fault::EndToc);
  }
  bool endWrite() { return load_test::step("endWrite", load_test::Fault::EndWrite); }
  bool buildBookBin(const std::string&, const BookMetadata& metadata) {
    load_test::state.builtMetadataCorrect = metadata.title == std::string(4096, 't') &&
                                            metadata.author == std::string(4096, 'a') && metadata.language == "en" &&
                                            metadata.coverItemHref == "cover.jpg" &&
                                            metadata.textReferenceHref == "chapter.xhtml";
    if (!load_test::step("build", load_test::Fault::Build)) return false;
    load_test::state.published = true;
    return true;
  }
  bool cleanupTmpFiles() {
    ++load_test::state.cleanupCalls;
    return load_test::state.cleanupOk;
  }

 private:
  bool opened = false;
  void open() {
    if (!opened) ++load_test::state.openHandles;
    opened = true;
  }
  void close() {
    if (opened) --load_test::state.openHandles;
    opened = false;
  }
};

class CssParser {
 public:
  enum class CacheStatus { Missing, Invalid, Partial, Complete };
  enum class CacheLoadResult { Invalid, LowMemory, Complete };
  enum class ParseResult { Error, Partial, Complete };
  static inline CacheStatus status = CacheStatus::Complete;
  static inline CacheLoadResult loadResult = CacheLoadResult::Complete;
  static inline ParseResult parseResult = ParseResult::Complete;
  explicit CssParser(const std::string&) {
    auto& s = load_test::state;
    ++s.cssCreated;
    s.cssPeak = std::max(s.cssPeak, ++s.cssLive);
    s.events.emplace_back("css+");
  }
  ~CssParser() {
    --load_test::state.cssLive;
    load_test::state.events.emplace_back("css-");
  }
  CacheStatus inspectCache() const { return status; }
  CacheLoadResult loadFromCache() const { return loadResult; }
  void deleteCache() { ++load_test::state.cssDeletes; }
  void clear() { ++load_test::state.cssClears; }
};
static_assert(sizeof(CssParser) != sizeof(BookMetadataCache));

struct StorageFake {
  bool removeDir(const char* path) {
    if (std::string(path) == "/cache/sections") ++load_test::state.sectionDeletes;
    return true;
  }
};
inline StorageFake Storage;

// These fixtures open every book as a plain ZIP; no entry is protected.
namespace freeink::content {
struct ContentDecryptor {};
inline std::unique_ptr<ContentDecryptor> openProtectedBook(const std::string&, std::string&) { return nullptr; }
}  // namespace freeink::content

class Epub {
 public:
  std::string filepath = "/book.epub", cachePath = "/cache", tocNavItem = "nav.xhtml", tocNcxItem = "toc.ncx";
  std::unique_ptr<freeink::content::ContentDecryptor> decryptor;
  std::string protectionError;
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  std::unique_ptr<CssParser> cssParser;
  bool load(bool buildIfMissing = true, bool skipLoadingCss = false);
  bool parseContentOpf(BookMetadataCache::BookMetadata& metadata, bool = true) {
    load_test::state.opfInputEmpty = metadata.title.empty() && metadata.author.empty() && metadata.language.empty() &&
                                     metadata.coverItemHref.empty() && metadata.textReferenceHref.empty();
    metadata.title.assign(4096, 't');
    metadata.author.assign(4096, 'a');
    metadata.language = "en";
    metadata.coverItemHref = "cover.jpg";
    metadata.textReferenceHref = "chapter.xhtml";
    ++load_test::state.opfCalls;
    return load_test::state.fault != load_test::Fault::ParseOpf;
  }
  void discoverCssFilesFromZip() { ++load_test::state.discoveries; }
  CssParser::ParseResult parseCssFiles(CssParser::CacheStatus) {
    ++load_test::state.cssParses;
    load_test::state.metadataValuesDuringCss = load_test::state.metadataValuesLive;
    load_test::state.events.emplace_back("parseCss");
    return CssParser::parseResult;
  }
  bool parseTocNavFile() {
    ++load_test::state.navCalls;
    return load_test::state.navOk;
  }
  bool parseTocNcxFile() {
    ++load_test::state.ncxCalls;
    return load_test::state.ncxOk;
  }
  void setupCacheDir() const { ++load_test::state.directoryCalls; }
};
