#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "RealFontPage.h"

namespace {
using namespace page_test;
using Clock = std::chrono::steady_clock;

struct Result {
  CompositionStats stats;
  double medianUs = 0;
};

bool measureCase(GfxRenderer::Orientation orientation, Scene scene, bool prewarm, int samples) {
  RealFontPage page;
  if (!page.hasFixtureGlyphs()) return false;
  page.renderer.setOrientation(orientation);
  page.renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Overlay, HalDisplay::FAST_REFRESH);
  page.prepare(scene, prewarm);
  page.compose(scene, LEGACY_STRIP_ROWS);
  const auto expectedLsb = page.lsb;
  const auto expectedMsb = page.msb;

  // Host benchmark buffers only: reserve once, never grow during the measured path.
  std::vector<double> timings[2];
  for (auto& times : timings) times.reserve(samples);
  Result results[2];
  for (int sample = -1; sample < samples; ++sample) {
    // Alternate order, with one discarded warm-up pair, to reduce ordering bias.
    for (int trial = 0; trial < 2; ++trial) {
      const int variant = (trial + sample + 1) % 2;
      page.prepare(scene, prewarm);
      const auto start = Clock::now();
      const auto stats = page.compose(scene, variant == 0 ? LEGACY_STRIP_ROWS : FULL_HEIGHT_ROWS);
      const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
      if (page.lsb != expectedLsb || page.msb != expectedMsb || page.display.frame != page.bwBeforeComposition) {
        std::fprintf(stderr, "Plane or BW mismatch: %s/%s\n", sceneName(scene), orientationName(orientation));
        return false;
      }
      if (sample >= 0) timings[variant].push_back(elapsed);
      results[variant].stats = stats;
    }
  }
  for (int variant = 0; variant < 2; ++variant) {
    auto& times = timings[variant];
    std::sort(times.begin(), times.end());
    const auto middle = times.size() / 2;
    results[variant].medianUs = times.size() % 2 ? times[middle] : (times[middle - 1] + times[middle]) / 2;
  }
  const auto& old = results[0];
  const auto& current = results[1];
  std::printf("%s,%s,%s,%d,%.1f,%.1f,%.2f,%u,%u,%u,%u,%u,%u,%u,%u\n", sceneName(scene), orientationName(orientation),
              prewarm ? "prewarmed" : "cold-font", samples, old.medianUs, current.medianUs,
              old.medianUs / current.medianUs, old.stats.pageTraversals, current.stats.pageTraversals,
              old.stats.textDrawCalls, current.stats.textDrawCalls, old.stats.bitmapCalls, current.stats.bitmapCalls,
              old.stats.cacheMisses, current.stats.cacheMisses);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  int samples = 11;
  if (argc == 3 && std::strcmp(argv[1], "--samples") == 0) {
    const char* end = argv[2] + std::strlen(argv[2]);
    const auto parsed = std::from_chars(argv[2], end, samples);
    if (parsed.ec != std::errc{} || parsed.ptr != end || samples < 3 || samples > 1001) return 2;
  } else if (argc != 1) {
    std::fprintf(stderr, "Usage: GfxPageBenchmark [--samples 3..1001]\n");
    return 2;
  }
  std::puts("# Host CPU composition only: no ESP32 timing, SPI, panel BUSY, SD I/O, optical ghosting or prewarm time.");
  std::puts("# Real compressed Noto Serif 14; two grayscale planes; identical bytes; unchanged BW; overlay mode.");
  std::puts(
      "scene,orientation,font_cache,samples,legacy_median_us,full_height_median_us,host_speedup,"
      "legacy_page_passes,full_page_passes,legacy_text_calls,full_text_calls,legacy_bitmap_calls,"
      "full_bitmap_calls,legacy_decompression_misses,full_decompression_misses");
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    for (const auto scene : {Scene::Prose, Scene::Multilingual, Scene::EdgeCases}) {
      if (!measureCase(orientation, scene, true, samples)) return 1;
    }
  }
  for (const auto scene : {Scene::Prose, Scene::Multilingual}) {
    if (!measureCase(GfxRenderer::Portrait, scene, false, samples)) return 1;
  }
  return 0;
}
