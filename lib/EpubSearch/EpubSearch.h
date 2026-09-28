#pragma once

#include <Print.h>
#include <expat.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace epub_search {

inline constexpr size_t MAX_QUERY_BYTES = 64;
inline constexpr size_t MAX_RESULTS = 32;
inline constexpr size_t CONTEXT_CODEPOINTS = 12;
inline constexpr size_t SNIPPET_BYTES = MAX_QUERY_BYTES + 2 * CONTEXT_CODEPOINTS * 4 + 1;
inline constexpr size_t CHUNK_BYTES = 1024;
inline constexpr uint32_t MAX_CHAPTER_BYTES = 4 * 1024 * 1024;
inline constexpr uint32_t MAX_BOOK_BYTES = 64 * 1024 * 1024;
inline constexpr int MAX_SPINE_ITEMS = 4096;
inline constexpr size_t MAX_XML_BYTES = 32 * 1024;

struct Result {
  int spineIndex = 0;
  uint32_t visibleTextOffset = 0;
  char snippet[SNIPPET_BYTES] = {};
};

struct Results {
  Result items[MAX_RESULTS] = {};
  uint8_t count = 0;
};

enum class Status : uint8_t { Running, Complete, LimitReached, Cancelled, InvalidInput, OutOfMemory };

// Search-only SAX sink. The owner allocates it on opening Search and destroys it
// before returning to the reader. No layout/cache changes or persistent index.
class ChapterSearch final : public Print {
 public:
  using Cancel = bool (*)(void*);
  ChapterSearch(Results& results, int spineIndex, Cancel cancel = nullptr, void* context = nullptr,
                size_t xmlBudget = MAX_XML_BYTES, uint32_t byteBudget = MAX_CHAPTER_BYTES);
  ~ChapterSearch() override;
  ChapterSearch(const ChapterSearch&) = delete;
  ChapterSearch& operator=(const ChapterSearch&) = delete;

  bool begin(std::string_view query);
  size_t write(uint8_t value) override;
  size_t write(const uint8_t* data, size_t size) override;
  Status finish(bool streamSucceeded = true);
  Status status() const { return state; }
  uint32_t bytesRead() const { return inputBytes; }
  size_t xmlBytesInUse() const { return allocatedXmlBytes; }
  size_t peakXmlBytes() const { return peakAllocatedXmlBytes; }
  static bool validQuery(std::string_view query);

 private:
  static constexpr size_t HISTORY_SIZE = MAX_QUERY_BYTES + CONTEXT_CODEPOINTS;
  static constexpr size_t MAX_MARKUP_BYTES = 8192;
  static constexpr unsigned MAX_DEPTH = 64;
  struct Character {
    uint32_t value;
    uint32_t offset;
  };
  enum class Lex : uint8_t { Text, Markup, Entity };

  static void XMLCALL startElement(void*, const XML_Char*, const XML_Char**);
  static void XMLCALL endElement(void*, const XML_Char*);
  static void XMLCALL characterData(void*, const XML_Char*, int);
  static void XMLCALL doctype(void*, const XML_Char*, const XML_Char*, const XML_Char*, int);
  static int XMLCALL externalEntity(XML_Parser, const XML_Char*, const XML_Char*, const XML_Char*, const XML_Char*);
  static void* allocateXml(size_t size);
  static void* reallocateXml(void* pointer, size_t size);
  static void freeXml(void* pointer);
  bool claimAllocator();
  void releaseParser();
  void stop(Status reason);
  bool flush(bool final = false);
  bool output(char value);
  bool consume(uint8_t value);
  bool expandEntity();
  void visible(uint32_t value, bool synthetic = false);
  void match(uint32_t value, uint32_t offset);
  void append(Result& result, uint32_t value);
  const Character& historyAt(size_t age) const;

  Results& results;
  const int spineIndex;
  const uint8_t firstResult;
  Cancel cancel;
  void* context;
  XML_Parser parser = nullptr;
  const size_t xmlBudget;
  const uint32_t byteBudget;
  size_t allocatedXmlBytes = 0;
  size_t peakAllocatedXmlBytes = 0;
  Status state = Status::InvalidInput;
  uint32_t inputBytes = 0;
  uint32_t visibleOffset = 0;
  uint32_t pattern[MAX_QUERY_BYTES] = {};
  uint8_t prefix[MAX_QUERY_BYTES] = {};
  uint8_t patternSize = 0;
  uint8_t matched = 0;
  Character history[HISTORY_SIZE] = {};
  uint16_t historyNext = 0;
  uint16_t historyCount = 0;
  uint8_t trailing[MAX_RESULTS] = {};
  uint16_t snippetLengths[MAX_RESULTS] = {};
  unsigned depth = 0;
  unsigned nonVisibleDepth = 0;
  bool insideBody = false;
  bool sawBody = false;
  bool previousSpace = true;
  Lex lex = Lex::Text;
  char entity[64] = {};
  uint8_t entitySize = 0;
  char markupPrefix[10] = {};
  uint8_t markupPrefixSize = 0;
  size_t markupBytes = 0;
  char quote = 0;
  char previousMarkup = 0;
  char beforePreviousMarkup = 0;
  char parseBuffer[CHUNK_BYTES] = {};
  size_t parseSize = 0;
};

}  // namespace epub_search
