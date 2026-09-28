#pragma once

#include <EpdFontData.h>

#include <cstdlib>
#include <deque>
#include <string>

class EpdFont;

// These fixtures use built-in flash fonts. Fail loudly if a future change
// accidentally exercises an SD path instead of silently pretending it worked.
class SdCardFont {
 public:
  using TextGetter = const char* (*)(const void*, uint32_t);
  static SdCardFont* fromMissCtx(void*) { std::abort(); }
  bool isOverflowGlyph(const EpdGlyph*) const { std::abort(); }
  const uint8_t* getOverflowBitmap(const EpdGlyph*) const { std::abort(); }
  uint8_t resolveStyle(uint8_t) const { std::abort(); }
  int prewarm(const char*, uint8_t, bool = false, bool = true, bool = true) { std::abort(); }
  int prewarm(TextGetter, const void*, uint32_t, uint8_t, bool, bool) { std::abort(); }
  int buildAdvanceTable(const char*, uint8_t, const char*) { std::abort(); }
  int buildAdvanceTable(const std::deque<std::string>&, bool, uint8_t, const char*) { std::abort(); }
  int buildAdvanceTablePacked(const char* const*, const size_t*, size_t, bool, bool, uint8_t, const char*) {
    std::abort();
  }
  bool hasAdvanceTable() const { std::abort(); }
  uint16_t getAdvance(uint32_t, uint8_t) const { std::abort(); }
  EpdFont* getEpdFont(uint8_t = 0) { std::abort(); }
  void clearCache() { std::abort(); }
  void releaseResidentCaches() { std::abort(); }
  void logStats(const char*) { std::abort(); }
  void resetStats() { std::abort(); }
};
