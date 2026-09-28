#pragma once

#include <HalStorage.h>
#include <SdCardFont.h>

#include <string>

namespace sd_font_fixture {
constexpr uint32_t FIRST = 0xAC00;
constexpr uint32_t GLYPHS = 513;
constexpr uint16_t BITMAP_BYTES = 128;

inline void put16(size_t at, uint16_t value) {
  sdFontTestFile[at] = value;
  sdFontTestFile[at + 1] = value >> 8;
}
inline void put32(size_t at, uint32_t value) {
  put16(at, value);
  put16(at + 2, value >> 16);
}
inline uint8_t bitmapByte(uint32_t glyphIndex, uint8_t style) { return (glyphIndex + style * 53) % 251; }

// Host-only fixture storage; no buffers or instrumentation enter firmware.
inline void makeFont(uint8_t styles = 1) {
  const size_t dataStart = 32 + 32 * styles;
  constexpr size_t STYLE_BYTES = 24 + GLYPHS * (sizeof(EpdGlyph) + BITMAP_BYTES);
  sdFontTestFile.assign(dataStart + styles * STYLE_BYTES, 0);
  std::memcpy(sdFontTestFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  sdFontTestFile[12] = styles;
  for (uint8_t style = 0; style < styles; ++style) {
    const size_t toc = 32 + style * 32;
    const size_t intervals = dataStart + style * STYLE_BYTES;
    const size_t glyphs = intervals + 24;
    const size_t bitmap = glyphs + GLYPHS * sizeof(EpdGlyph);
    sdFontTestFile[toc] = style;
    put32(toc + 4, 2);
    put32(toc + 8, GLYPHS);
    sdFontTestFile[toc + 12] = 32;
    put16(toc + 13, 32);
    put32(toc + 24, intervals);
    put32(intervals, FIRST);
    put32(intervals + 4, FIRST + GLYPHS - 2);
    put32(intervals + 12, 0xFFFD);
    put32(intervals + 16, 0xFFFD);
    put32(intervals + 20, GLYPHS - 1);
    for (uint32_t i = 0; i < GLYPHS; ++i) {
      EpdGlyph glyph{};
      glyph.width = 32;
      glyph.height = 32;
      glyph.advanceX = (32 + style) << 4;
      glyph.top = 32;
      glyph.dataLength = BITMAP_BYTES;
      // Reverse bitmap order catches metadata/bitmap ordering mistakes.
      glyph.dataOffset = (GLYPHS - 1 - i) * BITMAP_BYTES;
      std::memcpy(sdFontTestFile.data() + glyphs + i * sizeof(glyph), &glyph, sizeof(glyph));
      std::memset(sdFontTestFile.data() + bitmap + glyph.dataOffset, bitmapByte(i, style), BITMAP_BYTES);
    }
  }
}

inline std::string page(uint32_t first, uint32_t count, uint32_t stride = 1) {
  std::string text;
  text.reserve(count * 3);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t cp = first + i * stride;
    text.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
    text.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  return text;
}

inline uint32_t residentCount(SdCardFont& font, uint8_t style = 0) {
  const auto* data = font.getEpdFont(style)->data;
  uint32_t count = 0;
  for (uint32_t i = 0; i < data->intervalCount; ++i) {
    count += data->intervals[i].last - data->intervals[i].first + 1;
  }
  return count;
}

inline const EpdGlyph* residentGlyph(SdCardFont& font, uint32_t cp, uint8_t style = 0) {
  const auto* data = font.getEpdFont(style)->data;
  for (uint32_t i = 0; i < data->intervalCount; ++i) {
    const auto& interval = data->intervals[i];
    if (cp >= interval.first && cp <= interval.last) return data->glyph + interval.offset + cp - interval.first;
  }
  return nullptr;
}

// Check every bitmap byte, metrics and replacement glyph independently of the I/O counts.
inline bool pageIntact(SdCardFont& font, uint32_t first, uint32_t count, uint8_t styles = 1, uint32_t stride = 1) {
  for (uint8_t style = 0; style < styles; ++style) {
    const auto* data = font.getEpdFont(style)->data;
    for (uint32_t i = 0; i <= count; ++i) {
      const uint32_t cp = i == count ? 0xFFFD : first + i * stride;
      const uint32_t index = i == count ? GLYPHS - 1 : cp - FIRST;
      const auto* glyph = residentGlyph(font, cp, style);
      if (!glyph || !data->bitmap || glyph->dataLength != BITMAP_BYTES || glyph->advanceX != (32 + style) << 4 ||
          glyph->width != 32 || glyph->height != 32 || glyph->top != 32 ||
          glyph->dataOffset > residentCount(font, style) * BITMAP_BYTES - BITMAP_BYTES)
        return false;
      for (uint16_t b = 0; b < BITMAP_BYTES; ++b) {
        if (data->bitmap[glyph->dataOffset + b] != bitmapByte(index, style)) return false;
      }
    }
  }
  return true;
}
}  // namespace sd_font_fixture
