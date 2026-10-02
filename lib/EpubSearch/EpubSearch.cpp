#include "EpubSearch.h"

#include <Logging.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Epub/VisibleTextUtils.h"
#include "Epub/htmlEntities.h"

namespace epub_search {
namespace {

// Expat's allocator API has no user pointer. Only the duration of an Expat
// create/parse call claims this context; competing search parsers fail closed.
std::atomic<ChapterSearch*> allocatorContext{nullptr};

struct alignas(std::max_align_t) XmlAllocation {
  ChapterSearch* owner;
  size_t bytes;
};

bool whitespace(const uint32_t cp) {
  return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == 0xA0 || cp == 0x202F;
}

uint32_t fold(const uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  if ((cp >= 0xC0 && cp <= 0xD6) || (cp >= 0xD8 && cp <= 0xDE)) return cp + 32;
  if ((cp >= 0x391 && cp <= 0x3A1) || (cp >= 0x3A3 && cp <= 0x3AB)) return cp + 32;
  if (cp >= 0x410 && cp <= 0x42F) return cp + 32;
  if (cp >= 0x400 && cp <= 0x40F) return cp + 80;
  return cp;
}

bool decode(std::string_view text, size_t& cursor, uint32_t& cp) {
  if (cursor >= text.size()) return false;
  const uint8_t first = static_cast<uint8_t>(text[cursor++]);
  if (first < 0x80) {
    cp = first;
    return first != 0;
  }
  const unsigned extra = first >= 0xC2 && first <= 0xDF   ? 1
                         : first >= 0xE0 && first <= 0xEF ? 2
                         : first >= 0xF0 && first <= 0xF4 ? 3
                                                          : 0;
  if (!extra || text.size() - cursor < extra) return false;
  cp = first & ((1u << (6 - extra)) - 1u);
  for (unsigned i = 0; i < extra; i++) {
    const uint8_t next = static_cast<uint8_t>(text[cursor++]);
    if ((next & 0xC0) != 0x80) return false;
    cp = (cp << 6) | (next & 0x3F);
  }
  return cp >= (extra == 1   ? 0x80u
                : extra == 2 ? 0x800u
                             : 0x10000u) &&
         cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
}

size_t encode(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

bool block(const char* name) {
  static constexpr const char* TAGS[] = {"p",  "div", "br", "hr", "li", "ul", "ol", "blockquote", "h1",      "h2",
                                         "h3", "h4",  "h5", "h6", "tr", "td", "th", "section",    "article", "pre"};
  for (const auto* tag : TAGS)
    if (VisibleTextUtils::equalsTag(name, tag)) return true;
  return false;
}

}  // namespace

ChapterSearch::ChapterSearch(Results& results, const int spineIndex, Cancel cancel, void* context,
                             const size_t xmlBudget, const uint32_t byteBudget)
    : results(results),
      spineIndex(spineIndex),
      firstResult(results.count),
      cancel(cancel),
      context(context),
      xmlBudget(std::min(xmlBudget, MAX_XML_BYTES)),
      byteBudget(std::min(byteBudget, MAX_CHAPTER_BYTES)) {}

ChapterSearch::~ChapterSearch() { releaseParser(); }

bool ChapterSearch::claimAllocator() {
  ChapterSearch* expected = nullptr;
  if (allocatorContext.compare_exchange_strong(expected, this, std::memory_order_acquire)) return true;
  LOG_ERR("SRCH", "Search XML allocator already in use");
  state = Status::OutOfMemory;
  return false;
}

void* ChapterSearch::allocateXml(const size_t size) {
  auto* owner = allocatorContext.load(std::memory_order_relaxed);
  if (!owner || size > owner->xmlBudget || sizeof(XmlAllocation) > owner->xmlBudget - size) return nullptr;
  const size_t total = size + sizeof(XmlAllocation);
  if (total > owner->xmlBudget - owner->allocatedXmlBytes) return nullptr;
  // Expat owns these blocks and releases them through freeXml().
  auto* allocation = static_cast<XmlAllocation*>(std::malloc(total));
  if (!allocation) return nullptr;
  allocation->owner = owner;
  allocation->bytes = total;
  owner->allocatedXmlBytes += total;
  owner->peakAllocatedXmlBytes = std::max(owner->peakAllocatedXmlBytes, owner->allocatedXmlBytes);
  return allocation + 1;
}

void* ChapterSearch::reallocateXml(void* pointer, const size_t size) {
  if (!pointer) return allocateXml(size);
  if (!size) {
    freeXml(pointer);
    return nullptr;
  }
  auto* old = static_cast<XmlAllocation*>(pointer) - 1;
  auto* owner = old->owner;
  if (size > owner->xmlBudget || sizeof(XmlAllocation) > owner->xmlBudget - size) return nullptr;
  const size_t total = size + sizeof(XmlAllocation);
  const size_t retained = owner->allocatedXmlBytes - old->bytes;
  if (total > owner->xmlBudget - retained) return nullptr;
  auto* allocation = static_cast<XmlAllocation*>(std::realloc(old, total));
  if (!allocation) return nullptr;
  allocation->bytes = total;
  owner->allocatedXmlBytes = retained + total;
  owner->peakAllocatedXmlBytes = std::max(owner->peakAllocatedXmlBytes, owner->allocatedXmlBytes);
  return allocation + 1;
}

void ChapterSearch::freeXml(void* pointer) {
  if (!pointer) return;
  auto* allocation = static_cast<XmlAllocation*>(pointer) - 1;
  allocation->owner->allocatedXmlBytes -= allocation->bytes;
  std::free(allocation);
}

void ChapterSearch::releaseParser() {
  if (parser) {
    XML_ParserFree(parser);
    parser = nullptr;
  }
}

bool ChapterSearch::validQuery(const std::string_view query) {
  if (query.empty() || query.size() > MAX_QUERY_BYTES) return false;
  size_t cursor = 0;
  bool content = false;
  while (cursor < query.size()) {
    uint32_t cp = 0;
    if (!decode(query, cursor, cp)) return false;
    content = content || !whitespace(cp);
  }
  return content;
}

bool ChapterSearch::begin(const std::string_view query) {
  if (parser || patternSize || !validQuery(query) || results.count >= MAX_RESULTS) return false;
  size_t cursor = 0;
  bool space = true;
  while (cursor < query.size()) {
    uint32_t cp = 0;
    decode(query, cursor, cp);
    if (whitespace(cp)) {
      if (space) continue;
      cp = ' ';
    }
    pattern[patternSize++] = fold(cp);
    space = cp == ' ';
  }
  if (pattern[patternSize - 1] == ' ') patternSize--;
  for (uint8_t i = 1, previous = 0; i < patternSize; i++) {
    while (previous && pattern[i] != pattern[previous]) previous = prefix[previous - 1];
    if (pattern[i] == pattern[previous]) previous++;
    prefix[i] = previous;
  }
  if (!claimAllocator()) return false;
  const XML_Memory_Handling_Suite memory{allocateXml, reallocateXml, freeXml};
  parser = XML_ParserCreate_MM(nullptr, &memory, nullptr);
  allocatorContext.store(nullptr, std::memory_order_release);
  if (!parser) {
    state = Status::OutOfMemory;
    LOG_ERR("SRCH", "OOM: XML parser");
    return false;
  }
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  XML_SetStartDoctypeDeclHandler(parser, doctype);
  XML_SetExternalEntityRefHandler(parser, externalEntity);
  XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_NEVER);
  state = Status::Running;
  return true;
}

void ChapterSearch::stop(const Status reason) {
  if (state != Status::Running) return;
  state = reason;
  if (parser) XML_StopParser(parser, XML_FALSE);
}

bool ChapterSearch::flush(const bool final) {
  if (state != Status::Running) return false;
  if (!claimAllocator()) return false;
  const auto status = XML_Parse(parser, parseBuffer, static_cast<int>(parseSize), final);
  allocatorContext.store(nullptr, std::memory_order_release);
  parseSize = 0;
  if (status == XML_STATUS_ERROR && state == Status::Running) {
    const auto error = XML_GetErrorCode(parser);
    state = error == XML_ERROR_NO_MEMORY ? Status::OutOfMemory : Status::InvalidInput;
    LOG_ERR("SRCH", "Chapter %d XML: %s", spineIndex, XML_ErrorString(error));
  }
  return state == Status::Running;
}

bool ChapterSearch::output(const char value) {
  parseBuffer[parseSize++] = value;
  return parseSize < sizeof(parseBuffer) || flush();
}

bool ChapterSearch::expandEntity() {
  entity[entitySize] = '\0';
  const char* replacement = lookupHtmlEntity(entity, entitySize);
  if (!replacement) {
    // Numeric references stay with Expat; undeclared names match the reader's
    // literal fallback. Do not permit a DTD to define expansion work.
    const bool numeric = entitySize > 2 && entity[1] == '#';
    if (!numeric)
      for (const char c : std::string_view("&amp;"))
        if (!output(c)) return false;
    const size_t start = numeric ? 0 : 1;
    for (size_t i = start; i < entitySize; i++)
      if (!output(entity[i])) return false;
    return true;
  }
  size_t cursor = 0;
  const std::string_view text(replacement);
  while (cursor < text.size()) {
    uint32_t cp = 0;
    if (!decode(text, cursor, cp)) return false;
    char encoded[20];
    const int length = snprintf(encoded, sizeof(encoded), "&#%u;", static_cast<unsigned>(cp));
    for (int i = 0; i < length; i++)
      if (!output(encoded[i])) return false;
  }
  return true;
}

bool ChapterSearch::consume(const uint8_t value) {
  const char c = static_cast<char>(value);
  if (lex == Lex::Entity) {
    if (entitySize >= sizeof(entity) - 1 || c == '<' || c == '&') {
      stop(Status::InvalidInput);
      return false;
    }
    entity[entitySize++] = c;
    if (c == ';') {
      lex = Lex::Text;
      return expandEntity();
    }
    return true;
  }
  if (lex == Lex::Text) {
    if (c == '&') {
      lex = Lex::Entity;
      entitySize = 1;
      entity[0] = '&';
      return true;
    }
    if (c == '<') {
      lex = Lex::Markup;
      markupBytes = 0;
      markupPrefixSize = 0;
      quote = previousMarkup = beforePreviousMarkup = 0;
    } else
      return output(c);
  }
  if (++markupBytes > MAX_MARKUP_BYTES) {
    stop(Status::LimitReached);
    return false;
  }
  if (markupPrefixSize < sizeof(markupPrefix) - 1) {
    markupPrefix[markupPrefixSize++] = c;
    markupPrefix[markupPrefixSize] = '\0';
  }
  const bool comment = std::strncmp(markupPrefix, "<!--", 4) == 0;
  const bool cdata = std::strncmp(markupPrefix, "<![CDATA[", 9) == 0;
  const bool instruction = std::strncmp(markupPrefix, "<?", 2) == 0;
  const bool declaration = std::strncmp(markupPrefix, "<!DOCTYPE", 9) == 0;
  bool end = false;
  if (comment)
    end = markupBytes >= 7 && beforePreviousMarkup == '-' && previousMarkup == '-' && c == '>';
  else if (cdata)
    end = markupBytes >= 12 && beforePreviousMarkup == ']' && previousMarkup == ']' && c == '>';
  else if (instruction)
    end = previousMarkup == '?' && c == '>';
  else if (quote) {
    if (c == quote) quote = 0;
  } else if (c == '\'' || c == '"')
    quote = c;
  else if (declaration && c == '[') {
    stop(Status::InvalidInput);
    return false;
  } else
    end = c == '>';
  beforePreviousMarkup = previousMarkup;
  previousMarkup = c;
  if (end) lex = Lex::Text;
  return output(c);
}

size_t ChapterSearch::write(const uint8_t value) { return write(&value, 1); }

size_t ChapterSearch::write(const uint8_t* data, const size_t size) {
  if (state != Status::Running || !data) return 0;
  size_t consumed = 0;
  while (consumed < size && state == Status::Running) {
    if (inputBytes % CHUNK_BYTES == 0 && cancel && cancel(context)) {
      stop(Status::Cancelled);
      break;
    }
    if (inputBytes >= byteBudget) {
      stop(Status::LimitReached);
      break;
    }
    inputBytes++;
    if (!consume(data[consumed])) break;
    consumed++;
  }
  return consumed;
}

Status ChapterSearch::finish(const bool streamSucceeded) {
  if (state == Status::Running) {
    if (!streamSucceeded || lex != Lex::Text)
      stop(Status::InvalidInput);
    else if (flush(true))
      state = sawBody ? Status::Complete : Status::InvalidInput;
  }
  if (state == Status::InvalidInput || state == Status::OutOfMemory) results.count = firstResult;
  releaseParser();
  return state;
}

void XMLCALL ChapterSearch::doctype(void* context, const XML_Char*, const XML_Char*, const XML_Char*, int internal) {
  if (internal) static_cast<ChapterSearch*>(context)->stop(Status::InvalidInput);
}

int XMLCALL ChapterSearch::externalEntity(XML_Parser, const XML_Char*, const XML_Char*, const XML_Char*,
                                          const XML_Char*) {
  return XML_STATUS_ERROR;
}

void XMLCALL ChapterSearch::startElement(void* context, const XML_Char* name, const XML_Char** atts) {
  auto& self = *static_cast<ChapterSearch*>(context);
  if (++self.depth > MAX_DEPTH || std::strlen(name) > 128) {
    self.stop(Status::LimitReached);
    return;
  }
  if (VisibleTextUtils::equalsTag(name, "body")) {
    self.insideBody = true;
    self.sawBody = true;
  }
  if (self.insideBody && (self.nonVisibleDepth || VisibleTextUtils::isNonVisibleElement(name))) self.nonVisibleDepth++;
  // The reader lays out the HTML hidden attribute as display:none but still counts the text's offsets.
  bool hidden = self.hiddenDepth != 0;
  for (const XML_Char** att = atts; att && *att && !hidden; att += 2) hidden = std::strcmp(*att, "hidden") == 0;
  if (self.insideBody && hidden) self.hiddenDepth++;
  if (self.insideBody && !self.nonVisibleDepth && !self.hiddenDepth && block(name)) self.visible(' ', true);
}

void XMLCALL ChapterSearch::endElement(void* context, const XML_Char* name) {
  auto& self = *static_cast<ChapterSearch*>(context);
  if (self.nonVisibleDepth) self.nonVisibleDepth--;
  const bool wasHidden = self.hiddenDepth != 0;
  if (wasHidden) self.hiddenDepth--;
  if (self.insideBody && !self.nonVisibleDepth && !wasHidden && block(name)) self.visible(' ', true);
  if (VisibleTextUtils::equalsTag(name, "body")) self.insideBody = false;
  if (self.depth) self.depth--;
}

void XMLCALL ChapterSearch::characterData(void* context, const XML_Char* data, const int length) {
  auto& self = *static_cast<ChapterSearch*>(context);
  if (!self.insideBody || self.nonVisibleDepth || self.state != Status::Running) return;
  size_t cursor = 0;
  const std::string_view text(data, static_cast<size_t>(length));
  while (cursor < text.size() && self.state == Status::Running) {
    uint32_t cp = 0;
    if (!decode(text, cursor, cp)) {
      self.stop(Status::InvalidInput);
      return;
    }
    if (self.hiddenDepth) {
      self.visibleOffset++;
      continue;
    }
    self.visible(cp);
  }
}

void ChapterSearch::visible(uint32_t value, const bool synthetic) {
  if (state != Status::Running) return;
  const uint32_t offset = visibleOffset;
  if (!synthetic) visibleOffset++;
  if (value == 0xFEFF) return;
  if (whitespace(value)) {
    if (previousSpace) return;
    value = ' ';
  }
  previousSpace = value == ' ';
  match(value, offset);
}

void ChapterSearch::append(Result& result, const uint32_t value) {
  const size_t index = &result - results.items;
  char bytes[4];
  const size_t count = encode(value, bytes);
  auto& length = snippetLengths[index];
  if (length + count >= sizeof(result.snippet)) return;
  std::memcpy(result.snippet + length, bytes, count);
  length += count;
  result.snippet[length] = '\0';
}

const ChapterSearch::Character& ChapterSearch::historyAt(const size_t age) const {
  return history[(historyNext + HISTORY_SIZE - 1 - age) % HISTORY_SIZE];
}

void ChapterSearch::match(const uint32_t value, const uint32_t offset) {
  for (uint8_t i = firstResult; i < results.count; i++) {
    if (trailing[i]) {
      append(results.items[i], value);
      trailing[i]--;
    }
  }
  history[historyNext] = {value, offset};
  historyNext = (historyNext + 1) % HISTORY_SIZE;
  historyCount = std::min<size_t>(historyCount + 1, HISTORY_SIZE);
  const auto folded = fold(value);
  while (matched && folded != pattern[matched]) matched = prefix[matched - 1];
  if (folded == pattern[matched]) matched++;
  if (matched != patternSize) return;
  const uint8_t index = results.count++;
  auto& result = results.items[index];
  result.spineIndex = spineIndex;
  result.visibleTextOffset = historyAt(patternSize - 1).offset;
  result.snippet[0] = '\0';
  snippetLengths[index] = 0;
  const size_t take = std::min<size_t>(historyCount, patternSize + CONTEXT_CODEPOINTS);
  for (size_t i = take; i > 0; i--) append(result, historyAt(i - 1).value);
  trailing[index] = CONTEXT_CODEPOINTS;
  matched = prefix[matched - 1];
  if (results.count == MAX_RESULTS) stop(Status::LimitReached);
}

}  // namespace epub_search
