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

double median(std::vector<double>& samples) {
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

bool runCase(FILE* output, GfxRenderer::Orientation orientation, Scene scene, bool prewarm,
             HalDisplay::GrayscaleMode grayMode, bool clipped, int rows, int samples) {
  RealFontPage page;
  if (!page.hasFixtureGlyphs()) return false;
  page.renderer.setOrientation(orientation);
  if (clipped) {
    page.renderer.setClipRect(17, 19, page.renderer.getScreenWidth() - 43, page.renderer.getScreenHeight() - 47);
  }
  std::vector<double> bwTimes, grayTimes;
  bwTimes.reserve(samples);
  grayTimes.reserve(samples);
  for (int sample = -1; sample < samples; ++sample) {
    page.prepare(scene, prewarm);
    page.renderer.clearScreen();
    auto start = Clock::now();
    page.drawScene(scene);
    auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    if (sample >= 0) bwTimes.push_back(elapsed);
    if (page.display.frame != page.bwBeforeComposition) return false;

    // Fixture preparation resets bitmap caches when prewarm is disabled. Font
    // lookup and fixture setup have already run; this is not cold-start latency.
    page.prepare(scene, prewarm);
    if (!page.renderer.displayGrayscaleBase(grayMode, HalDisplay::FAST_REFRESH) ||
        page.renderer.grayPlanesAreAbsolute() != (grayMode == HalDisplay::GrayscaleMode::Absolute))
      return false;
    start = Clock::now();
    page.compose(scene, rows);
    elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    if (sample >= 0) grayTimes.push_back(elapsed);
    if (page.display.frame != page.bwBeforeComposition) return false;
  }
  for (const auto* plane : {&page.display.frame, &page.lsb, &page.msb}) {
    if (std::fwrite(plane->data(), 1, plane->size(), output) != plane->size()) return false;
  }
  std::printf("%s,%s,%s,%s,%s,%d,%d,%.3f,%.3f\n", sceneName(scene), orientationName(orientation),
              prewarm ? "prewarmed" : "no-prewarm",
              grayMode == HalDisplay::GrayscaleMode::Absolute ? "absolute" : "overlay", clipped ? "clipped" : "full",
              rows, samples, median(bwTimes), median(grayTimes));
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  int samples = 0;
  const char* end = argv[2] + std::strlen(argv[2]);
  const auto parsed = std::from_chars(argv[2], end, samples);
  if (parsed.ec != std::errc{} || parsed.ptr != end || samples < 1 || samples > 101) return 2;
  FILE* output = std::fopen(argv[1], "wb");
  if (!output) return 2;
  std::puts("scene,orientation,font_cache,gray_mode,clip,strip_rows,samples,bw_median_us,gray_median_us");
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    for (const auto scene : {Scene::Prose, Scene::Multilingual, Scene::EdgeCases}) {
      for (const bool prewarm : {false, true}) {
        for (const auto grayMode : {HalDisplay::GrayscaleMode::Overlay, HalDisplay::GrayscaleMode::Absolute}) {
          for (const bool clipped : {false, true}) {
            for (const int rows : {LEGACY_STRIP_ROWS, FULL_HEIGHT_ROWS}) {
              if (!runCase(output, orientation, scene, prewarm, grayMode, clipped, rows, samples)) {
                std::fclose(output);
                return 1;
              }
            }
          }
        }
      }
    }
  }
  return std::fclose(output) == 0 ? 0 : 1;
}
