#include <HalStorage.h>
#include <SdCardFont.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "FontFixture.h"
#include "HostAllocations.h"

namespace {
using namespace sd_font_fixture;

void measure(SdCardFont& font, const char* scenario, const char* phase, uint32_t offset, uint32_t count,
             uint8_t styles = 1, uint32_t stride = 1) {
  const auto text = page(FIRST + offset, count, stride);
  sdFontTestIo = {};
  sdFontTestAllocations = {};
  const auto start = std::chrono::steady_clock::now();
#ifdef SD_FONT_LEGACY_PREWARM
  const int missed = font.prewarm(text.c_str(), (1U << styles) - 1, false, false);
#else
  const int missed = font.prewarm(text.c_str(), (1U << styles) - 1, false, false, false);
#endif
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
  const auto io = sdFontTestIo;
  if (missed != 0 || !pageIntact(font, FIRST + offset, count, styles, stride)) {
    std::fprintf(stderr, "Invalid page in %s/%s\n", scenario, phase);
    std::exit(EXIT_FAILURE);
  }
  uint32_t residentGlyphs = 0;
  for (uint8_t style = 0; style < styles; ++style) residentGlyphs += residentCount(font, style);
  std::printf(
      "{\"scenario\":\"%s\",\"phase\":\"%s\",\"styles\":%u,\"requested_glyphs_per_style\":%u,"
      "\"resident_glyphs\":%u,\"opens\":%zu,\"seeks\":%zu,\"reads\":%zu,\"bytes\":%zu,"
      "\"nothrow_array_allocations\":%zu,\"array_bytes_requested\":%zu,\"host_prewarm_ns\":%lld,\"integrity\":true}\n",
      scenario, phase, styles, count + 1, residentGlyphs, io.opens, io.seeks, io.reads, io.bytes,
      sdFontTestAllocations.attempts, sdFontTestAllocations.requestedBytes, static_cast<long long>(elapsed.count()));
}

void scopedPage(SdCardFont& font, const char* scenario, uint32_t offset, uint32_t count, uint8_t styles = 1,
                uint32_t stride = 1) {
  font.clearCache();
  measure(font, scenario, "idle_prefetch", offset, count, styles, stride);
  font.clearCache();
  font.clearCache();
  measure(font, scenario, "page_turn", offset, count, styles, stride);
  font.clearCache();
}

void requireLoaded(SdCardFont& font) {
  if (!font.load("fixture")) std::exit(EXIT_FAILURE);
}
}  // namespace

int main() {
  {
    makeFont();
    SdCardFont font;
    requireLoaded(font);
    measure(font, "single_style", "cold_page", 0, 100);
    font.clearCache();
    measure(font, "single_style", "same_page", 0, 100);
    scopedPage(font, "next_page_1", 100, 100);
    scopedPage(font, "next_page_2", 200, 100);
    scopedPage(font, "next_page_3", 300, 100);
  }
  {
    makeFont(4);
    SdCardFont font;
    requireLoaded(font);
    measure(font, "four_styles", "cold_page", 0, 100, 4);
    font.clearCache();
    measure(font, "four_styles", "same_page", 0, 100, 4);
    scopedPage(font, "four_styles_next_page", 100, 100, 4);
  }
  {
    makeFont();
    SdCardFont font;
    requireLoaded(font);
    measure(font, "dense_sparse", "dense_page", 0, 400);
    scopedPage(font, "sparse_page_1", 412, 100);
    scopedPage(font, "sparse_page_2", 300, 100);
    scopedPage(font, "sparse_page_3", 200, 100);
    scopedPage(font, "sparse_page_4", 100, 100);
    scopedPage(font, "dense_again", 0, 400);
  }
  {
    makeFont();
    SdCardFont font;
    requireLoaded(font);
    measure(font, "scattered_glyphs", "cold_page", 0, 100, 1, 4);
    font.clearCache();
    measure(font, "scattered_glyphs", "same_page", 0, 100, 1, 4);
  }
}
