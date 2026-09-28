#include "TextBlock.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>
#include <Serialization.h>

#include <cstdint>
#include <cstring>

#include "../../../../src/fontIds.h"

namespace {
bool readRubyTexts(HalFile& file, const uint16_t wordCount, std::vector<std::string>& rubyTexts) {
  uint8_t buffer[64];
  size_t cursor = 0;
  size_t buffered = 0;
  // Read ahead only within the minimum bytes required by the remaining strings.
  const auto read = [&](void* destination, size_t count, const size_t remainingWords) {
    auto* output = static_cast<uint8_t*>(destination);
    while (count != 0) {
      if (cursor == buffered) {
        if (remainingWords == 0) return serialization::readBytesChecked(file, output, count);
        buffered = std::min(sizeof(buffer), count + remainingWords * sizeof(uint32_t));
        cursor = 0;
        if (!serialization::readBytesChecked(file, buffer, buffered)) return false;
      }
      const size_t copied = std::min(count, buffered - cursor);
      memcpy(output, buffer + cursor, copied);
      output += copied;
      cursor += copied;
      count -= copied;
    }
    return true;
  };

  size_t bytesRemaining = TextBlock::MAX_RUBY_BYTES;
  for (uint16_t i = 0; i < wordCount; ++i) {
    uint32_t length = 0;
    if (!read(&length, sizeof(length), wordCount - i - 1) || length > bytesRemaining) return false;
    if (length == 0) continue;
    const auto position = file.position();
    const auto fileSize = file.size();
    if (position > fileSize || length > fileSize - position + buffered - cursor) return false;
    if (rubyTexts.empty()) rubyTexts.resize(wordCount);
    auto& text = rubyTexts[i];
    text.resize(length);
    if (!read(text.data(), length, 0)) return false;
    bytesRemaining -= length;
  }
  return true;
}

bool readBlockStyle(HalFile& file, BlockStyle& style) {
  // The wire layout has no struct padding; keep multi-byte loads aligned.
  constexpr size_t FLAG_OFFSET = 2 + 9 * sizeof(int16_t);
  uint8_t bytes[FLAG_OFFSET + 3 + sizeof(int8_t)];
  if (!serialization::readBytesChecked(file, bytes, sizeof(bytes)) ||
      bytes[0] > static_cast<uint8_t>(CssTextAlign::None) || bytes[1] > 1 || bytes[FLAG_OFFSET] > 1 ||
      bytes[FLAG_OFFSET + 1] > 1 || bytes[FLAG_OFFSET + 2] > 1) {
    return false;
  }
  int16_t spacing[9];
  memcpy(spacing, bytes + 2, sizeof(spacing));
  style.alignment = static_cast<CssTextAlign>(bytes[0]);
  style.textAlignDefined = bytes[1] != 0;
  style.marginTop = spacing[0];
  style.marginBottom = spacing[1];
  style.marginLeft = spacing[2];
  style.marginRight = spacing[3];
  style.paddingTop = spacing[4];
  style.paddingBottom = spacing[5];
  style.paddingLeft = spacing[6];
  style.paddingRight = spacing[7];
  style.textIndent = spacing[8];
  style.textIndentDefined = bytes[FLAG_OFFSET] != 0;
  style.isRtl = bytes[FLAG_OFFSET + 1] != 0;
  style.directionDefined = bytes[FLAG_OFFSET + 2] != 0;
  memcpy(&style.characterSpacing, bytes + FLAG_OFFSET + 3, sizeof(style.characterSpacing));
  return true;
}
}  // namespace

size_t TextBlock::arenaSize(const uint16_t wordCount, const bool hasFocus, const uint16_t textBytes) {
  // Layout documented in TextBlock.h: 16-bit arrays first, then 8-bit arrays, then text.
  size_t size = static_cast<size_t>(wordCount) * (sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t));
  if (hasFocus) {
    size += static_cast<size_t>(wordCount) * (sizeof(uint16_t) + sizeof(uint8_t));
  }
  return size + textBytes;
}

size_t TextBlock::cacheBudgetBytes() const {
  if (!isValid) return SIZE_MAX;
  constexpr size_t ALLOCATION_ALLOWANCE = 64;
  size_t total = sizeof(TextBlock) + 2 * ALLOCATION_ALLOWANCE;
  const auto chargeAllocation = [&total](const size_t count, const size_t elementSize) {
    if (count == 0) return true;
    if (total > SIZE_MAX - ALLOCATION_ALLOWANCE || count > (SIZE_MAX - total - ALLOCATION_ALLOWANCE) / elementSize)
      return false;
    total += count * elementSize + ALLOCATION_ALLOWANCE;
    return true;
  };
  if ((arena && !chargeAllocation(arenaSize(numWords, focusPresent, textBytes), 1)) ||
      !chargeAllocation(rubyTexts.capacity(), sizeof(rubyTexts[0])) ||
      !chargeAllocation(linkSpans.capacity(), sizeof(linkSpans[0])))
    return SIZE_MAX;
  for (const auto& ruby : rubyTexts) {
    // Charge even inline strings as separate storage to avoid depending on STL small-string layout.
    if (ruby.capacity() == SIZE_MAX || !chargeAllocation(ruby.capacity() + 1, 1)) return SIZE_MAX;
  }
  return total;
}

void TextBlock::bindArenaPointers() {
  uint8_t* base = arena.get();
  const size_t wc = numWords;
  textOffArr = reinterpret_cast<const uint16_t*>(base);
  xposArr = reinterpret_cast<const int16_t*>(base + wc * 2);
  size_t off = wc * 4;
  if (focusPresent) {
    focusSuffixXArr = reinterpret_cast<const uint16_t*>(base + off);
    off += wc * 2;
  }
  stylesArr = base + off;
  off += wc;
  if (focusPresent) {
    focusBoundaryArr = base + off;
    off += wc;
  }
  textArr = reinterpret_cast<const char*>(base + off);
}

TextBlock::TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& focusBoundary,
                     const std::vector<uint16_t>& focusSuffixX, const BlockStyle& blockStyle,
                     std::vector<std::string> rubyTexts, std::vector<LinkSpan> linkSpans)
    : blockStyle(blockStyle), rubyTexts(std::move(rubyTexts)), linkSpans(std::move(linkSpans)) {
  // Same invariant as deserialize(): a block never holds an all-empty rubyTexts, so a
  // ruby-less line costs nothing beyond its arena. The layout engine hands one over for
  // every line it extracts, ruby or not; release it here rather than carrying it for the
  // block's lifetime. Move-assigning an empty vector frees the buffer (clear() would not).
  if (!hasRuby()) {
    this->rubyTexts = std::vector<std::string>{};
  }

  // Focus annotations are optional: empty vectors mean no word in this block has a split.
  // When present, they must be sized in lockstep with words[].
  const bool hasFocus = !focusBoundary.empty();
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() || words.size() > 10000 ||
      (hasFocus && (words.size() != focusBoundary.size() || words.size() != focusSuffixX.size()))) {
    LOG_ERR("TXB", "Construction failed: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u)",
            static_cast<uint32_t>(words.size()), static_cast<uint32_t>(wordXpos.size()),
            static_cast<uint32_t>(wordStyles.size()), static_cast<uint32_t>(focusBoundary.size()),
            static_cast<uint32_t>(focusSuffixX.size()));
    isValid = false;
    return;
  }

  numWords = static_cast<uint16_t>(words.size());
  focusPresent = hasFocus;
  if (numWords == 0) {
    return;  // valid empty block, no arena
  }

  // Pass 1: total text size, one NUL per word. A line is at most a physical
  // row of the page, so uint16_t offsets are ample; reject anything larger.
  size_t totalText = 0;
  for (const auto& w : words) totalText += w.size() + 1;
  if (totalText > UINT16_MAX) {
    LOG_ERR("TXB", "Construction failed: text size %u exceeds arena limit", static_cast<uint32_t>(totalText));
    numWords = 0;
    focusPresent = false;
    isValid = false;
    return;
  }
  textBytes = static_cast<uint16_t>(totalText);

  const size_t size = arenaSize(numWords, focusPresent, textBytes);
  arena = makeUniqueNoThrow<uint8_t[]>(size);
  if (!arena) {
    // Evict rebuildable caches (SD-font mini data, render glyph cache) and
    // retry once before declaring the line lost.
    freeink::MemoryManager::instance().ensureFree(size + 4 * 1024);
    arena = makeUniqueNoThrow<uint8_t[]>(size);
  }
  if (!arena) {
    LOG_ERR("TXB", "OOM: arena %u bytes", static_cast<uint32_t>(size));
    numWords = 0;
    textBytes = 0;
    focusPresent = false;
    isValid = false;
    return;
  }
  bindArenaPointers();

  // Pass 2: fill. Mutable aliases of the const views bound above.
  auto* textOff = const_cast<uint16_t*>(textOffArr);
  auto* xpos = const_cast<int16_t*>(xposArr);
  auto* styles = const_cast<uint8_t*>(stylesArr);
  auto* text = const_cast<char*>(textArr);
  uint16_t off = 0;
  for (uint16_t i = 0; i < numWords; i++) {
    textOff[i] = off;
    xpos[i] = wordXpos[i];
    styles[i] = static_cast<uint8_t>(wordStyles[i]);
    memcpy(text + off, words[i].data(), words[i].size());
    off += static_cast<uint16_t>(words[i].size());
    text[off++] = '\0';
  }
  if (focusPresent) {
    auto* suffixX = const_cast<uint16_t*>(focusSuffixXArr);
    auto* boundary = const_cast<uint8_t*>(focusBoundaryArr);
    for (uint16_t i = 0; i < numWords; i++) {
      suffixX[i] = focusSuffixX[i];
      boundary[i] = focusBoundary[i];
    }
  }
}

bool TextBlock::hasRuby() const {
  for (const auto& rt : rubyTexts) {
    if (!rt.empty()) return true;
  }
  return false;
}

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  if (!isValid) {
    LOG_ERR("TXB", "Render skipped: invalid block");
    return;
  }
  const int8_t tracking = blockStyle.characterSpacing;

  const bool scanning = renderer.isFontCacheScanning();
  const int ascender = renderer.getFontAscenderSize(fontId);

  // Resolve ruby positions. Layout (extractLine) has already reserved extraStartOffset on the
  // left and extraEndOffset on the right, so the centered rubyX is always within the page margins.
  struct RubyDrawInfo {
    int x;
    BidiUtils::BidiBaseDir baseDir;
  };
  const bool blockHasRuby = hasRuby();
  std::vector<RubyDrawInfo> rubies;
  if (blockHasRuby) {
    rubies.resize(numWords);
    for (uint16_t i = 0; i < numWords; i++) {
      if (i < rubyTexts.size() && !rubyTexts[i].empty() && (wordStyle(i) & EpdFontFamily::RUBY_CONTINUE) == 0) {
        int groupWordCount = 1;
        while (i + groupWordCount < numWords && (wordStyle(i + groupWordCount) & EpdFontFamily::RUBY_CONTINUE) != 0) {
          groupWordCount++;
        }
        int groupActualWidth = 0;
        for (int k = 0; k < groupWordCount; ++k) {
          groupActualWidth += renderer.getTextAdvanceX(fontId, wordText(i + k), wordStyle(i + k), tracking);
        }
        const int rubyWidth = renderer.getTextAdvanceX(fontId, rubyTexts[i].c_str(), EpdFontFamily::SUP, tracking);
        const int leaderWordX = xposArr[i] + x;
        const auto baseDir =
            static_cast<BidiUtils::BidiBaseDir>(BidiUtils::detectParagraphLevel(wordText(i), blockStyle.isRtl ? 1 : 0));
        rubies[i] = {leaderWordX - (rubyWidth - groupActualWidth) / 2, baseDir};
        i += groupWordCount - 1;
      }
    }
  }

  struct DecorationLineTracker {
    EpdFontFamily::Style style;
    int yOffset;
    int startX = -1;
    int endX = -1;
    int yPos = 0;

    bool active() const { return startX != -1; }
    void reset() {
      startX = -1;
      endX = -1;
      yPos = 0;
    }
  };

  DecorationLineTracker decorationLines[] = {
      {EpdFontFamily::UNDERLINE, ascender + 2},
      {EpdFontFamily::STRIKETHROUGH, ascender * 4 / 5},
  };

  const auto flushDecoration = [&](DecorationLineTracker& line) {
    if (line.active()) {
      renderer.drawLine(line.startX, line.yPos, line.endX, line.yPos, 2, true);
      line.reset();
    }
  };
  const auto flushDecorations = [&]() {
    for (auto& line : decorationLines) {
      flushDecoration(line);
    }
  };

  // Loop-invariant: hoisted out of the word loop so rubyTexts is scanned once,
  // not once per word.
  const int rubyShift = getRubyShift(ascender);

  for (uint16_t i = 0; i < numWords; i++) {
    const char* word = wordText(i);
    const int wordX = xposArr[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyle(i);
    const auto baseDir =
        static_cast<BidiUtils::BidiBaseDir>(BidiUtils::detectParagraphLevel(word, blockStyle.isRtl ? 1 : 0));
    const uint8_t boundary = focusBoundary(i);

    // SUP/SUB shift the baseline passed to drawText; the glyph is also scaled 50% inside
    // drawText, so these offsets are chosen relative to the full-size ascender:
    //   SUP: raise by 40% of ascender — sits clearly above the cap-height
    //   SUB: lower by 25% of ascender — descends below baseline without clashing with ascenders below
    int wordY = y + rubyShift;
    if ((currentStyle & EpdFontFamily::SUP) != 0) {
      wordY -= ascender * 2 / 5;
    } else if ((currentStyle & EpdFontFamily::SUB) != 0) {
      wordY += ascender / 4;
    }

    const int drawX = wordX;

    if (boundary > 0) {
      // Focus split: draw bold prefix, then the regular suffix at a pre-computed x offset.
      // The bold prefix is bounded to 9 codepoints by the clamp on targetBoldChars in
      // ParsedText::addWord; 9 UTF-8 codepoints occupy at most 9 * 4 = 36 bytes, +1 for null = 37.
      // suffixX is computed at cache-creation time to avoid font metric lookups at render time.
      static constexpr size_t MAX_FOCUS_PREFIX_BYTES = 9 * 4 + 1;
      char boldBuf[40];
      static_assert(sizeof(boldBuf) >= MAX_FOCUS_PREFIX_BYTES,
                    "boldBuf too small for max focus prefix (9 codepoints * 4 UTF-8 bytes + null)");
      const auto boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      const size_t boldLen =
          std::min<size_t>({static_cast<size_t>(boundary), static_cast<size_t>(wordTextLen(i)), sizeof(boldBuf) - 1});
      memcpy(boldBuf, word, boldLen);
      boldBuf[boldLen] = '\0';
      renderer.drawText(fontId, drawX, wordY, boldBuf, true, boldStyle, baseDir, tracking);
      const int suffixX = drawX + focusSuffixXArr[i];
      renderer.drawText(fontId, suffixX, wordY, word + boldLen, true, currentStyle, baseDir, tracking);
    } else {
      renderer.drawText(fontId, drawX, wordY, word, true, currentStyle, baseDir, tracking);
    }

    // Horizontal ruby text rendering
    if (blockHasRuby && i < rubyTexts.size() && !rubyTexts[i].empty() &&
        (wordStyle(i) & EpdFontFamily::RUBY_CONTINUE) == 0) {
      const int rubyY = wordY - ascender;
      renderer.drawText(fontId, rubies[i].x, rubyY, rubyTexts[i].c_str(), true, EpdFontFamily::SUP, rubies[i].baseDir,
                        tracking);
    }

    if (scanning) {
      continue;
    }

    if (EpdFontFamily::hasTextDecoration(currentStyle)) {
      int lineStartX = drawX;
      int lineWidth = renderer.getTextAdvanceX(fontId, word, currentStyle, tracking, baseDir,
                                               GfxRenderer::TextMeasureMode::Rendered);

      // Do not decorate the synthetic em-space used for paragraph indentation.
      if (wordTextLen(i) >= 3 && static_cast<uint8_t>(word[0]) == 0xE2 && static_cast<uint8_t>(word[1]) == 0x80 &&
          static_cast<uint8_t>(word[2]) == 0x83) {
        const char* visibleText = word + 3;
        lineStartX += renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle, tracking, baseDir,
                                               GfxRenderer::TextMeasureMode::Rendered);
        lineWidth = renderer.getTextAdvanceX(fontId, visibleText, currentStyle, tracking, baseDir,
                                             GfxRenderer::TextMeasureMode::Rendered);
      }

      for (auto& line : decorationLines) {
        if ((currentStyle & line.style) == 0) {
          flushDecoration(line);
          continue;
        }

        const int lineY = wordY + line.yOffset;
        if (line.active() && line.yPos != lineY) {
          flushDecoration(line);
        }
        if (!line.active()) {
          line.startX = lineStartX;
          line.yPos = lineY;
        }
        line.endX = lineStartX + lineWidth;
      }
    } else {
      flushDecorations();
    }
  }
  flushDecorations();
}

bool TextBlock::serialize(HalFile& file) const {
  if (!isValid) {
    LOG_ERR("TXB", "Serialization failed: invalid block");
    return false;
  }

  // Word data: scalars, then the arena verbatim -- its in-memory layout is
  // exactly the on-disk layout (see TextBlock.h), so one write covers all
  // per-word arrays and the text blob.
  if (!serialization::writePodChecked(file, numWords) ||
      !serialization::writePodChecked(file, static_cast<uint8_t>(focusPresent ? 1 : 0)) ||
      !serialization::writePodChecked(file, textBytes)) {
    LOG_ERR("TXB", "Serialization failed: text header");
    return false;
  }
  if (numWords > 0) {
    const size_t size = arenaSize(numWords, focusPresent, textBytes);
    if (file.write(arena.get(), size) != size) {
      LOG_ERR("TXB", "Serialization failed: arena write (%u bytes)", static_cast<uint32_t>(size));
      return false;
    }
  }

  size_t rubyBytesRemaining = MAX_RUBY_BYTES;
  for (size_t i = 0; i < numWords; i++) {
    const std::string_view ruby = i < rubyTexts.size() ? std::string_view(rubyTexts[i]) : std::string_view();
    if (ruby.size() > rubyBytesRemaining || !serialization::writeStringChecked(file, ruby)) {
      LOG_ERR("TXB", "Serialization failed: ruby text");
      return false;
    }
    rubyBytesRemaining -= ruby.size();
  }

  // Style (alignment + margins/padding/indent)
  if (!serialization::writePodChecked(file, blockStyle.alignment) ||
      !serialization::writePodChecked(file, blockStyle.textAlignDefined) ||
      !serialization::writePodChecked(file, blockStyle.marginTop) ||
      !serialization::writePodChecked(file, blockStyle.marginBottom) ||
      !serialization::writePodChecked(file, blockStyle.marginLeft) ||
      !serialization::writePodChecked(file, blockStyle.marginRight) ||
      !serialization::writePodChecked(file, blockStyle.paddingTop) ||
      !serialization::writePodChecked(file, blockStyle.paddingBottom) ||
      !serialization::writePodChecked(file, blockStyle.paddingLeft) ||
      !serialization::writePodChecked(file, blockStyle.paddingRight) ||
      !serialization::writePodChecked(file, blockStyle.textIndent) ||
      !serialization::writePodChecked(file, blockStyle.textIndentDefined) ||
      !serialization::writePodChecked(file, blockStyle.isRtl) ||
      !serialization::writePodChecked(file, blockStyle.directionDefined) ||
      !serialization::writePodChecked(file, blockStyle.characterSpacing)) {
    LOG_ERR("TXB", "Serialization failed: block style");
    return false;
  }

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(HalFile& file) {
  uint8_t header[2 * sizeof(uint16_t) + sizeof(uint8_t)];
  uint16_t wc = 0;
  uint16_t textBytes = 0;
  if (!serialization::readBytesChecked(file, header, sizeof(header)) || header[sizeof(uint16_t)] > 1) {
    LOG_ERR("TXB", "Deserialization failed: text header");
    return nullptr;
  }
  memcpy(&wc, header, sizeof(wc));
  memcpy(&textBytes, header + sizeof(uint16_t) + sizeof(uint8_t), sizeof(textBytes));
  const bool hasFocus = header[sizeof(uint16_t)] != 0;

  // Sanity checks: cap the arena allocation and reject impossible geometry
  // (every word carries at least its NUL terminator).
  if (wc > 10000) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }
  if ((wc == 0 && textBytes != 0) || (wc > 0 && textBytes < wc)) {
    LOG_ERR("TXB", "Deserialization failed: bad text size %u for %u words", textBytes, wc);
    return nullptr;
  }

  std::unique_ptr<TextBlock> block(new (std::nothrow) TextBlock());
  if (!block) {
    LOG_ERR("TXB", "OOM: TextBlock");
    return nullptr;
  }
  block->numWords = wc;
  block->textBytes = textBytes;
  block->focusPresent = hasFocus != 0;

  if (wc > 0) {
    const size_t size = arenaSize(wc, block->focusPresent, textBytes);
    block->arena = makeUniqueNoThrow<uint8_t[]>(size);
    if (!block->arena) {
      LOG_ERR("TXB", "OOM: arena %u bytes", static_cast<uint32_t>(size));
      return nullptr;
    }
    if (!serialization::readBytesChecked(file, block->arena.get(), size)) {
      LOG_ERR("TXB", "Deserialization failed: arena read (%u bytes)", static_cast<uint32_t>(size));
      return nullptr;
    }
    block->bindArenaPointers();

    // Validate offsets before anything dereferences wordText(): offset 0 first,
    // strictly increasing, in bounds, and every word NUL-terminated (word i ends
    // at the byte before offset i+1; the last word at the last text byte).
    const uint16_t* textOff = block->textOffArr;
    const char* text = block->textArr;
    if (textOff[0] != 0 || text[textBytes - 1] != '\0') {
      LOG_ERR("TXB", "Deserialization failed: corrupt text layout");
      return nullptr;
    }
    for (uint16_t i = 1; i < wc; i++) {
      if (textOff[i] <= textOff[i - 1] || textOff[i] >= textBytes || text[textOff[i] - 1] != '\0') {
        LOG_ERR("TXB", "Deserialization failed: corrupt word offset %u", i);
        return nullptr;
      }
    }
  }

  // Empty annotations share a bounded read buffer and need no retained vector.
  if (!readRubyTexts(file, wc, block->rubyTexts)) {
    LOG_ERR("TXB", "Deserialization failed: ruby text");
    return nullptr;
  }

  // Style (alignment + margins/padding/indent)
  if (!readBlockStyle(file, block->blockStyle)) {
    LOG_ERR("TXB", "Deserialization failed: block style");
    return nullptr;
  }

  return block;
}
