#pragma once

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>

#include <array>
#include <cstdint>
#include <map>
#include <optional>

namespace page_test {

constexpr int FONT_ID = 1;
constexpr int LEGACY_STRIP_ROWS = 80;
constexpr int FULL_HEIGHT_ROWS = HalDisplay::DISPLAY_HEIGHT;
using Plane = std::array<uint8_t, HalDisplay::BUFFER_SIZE>;

enum class Scene { Prose, Multilingual, EdgeCases };

struct CompositionStats {
  uint32_t pageTraversals = 0;
  uint32_t textDrawCalls = 0;
  uint32_t bitmapCalls = 0;
  uint32_t cacheMisses = 0;
};

// Host-only fixture storage. None of these result buffers is linked into firmware.
class RealFontPage {
 public:
  HalDisplay display;
  GfxRenderer renderer;
  FontDecompressor decompressor;
  const std::map<int, SdCardFont*> noSdFonts;
  FontCacheManager cache;
  Plane lsb{};
  Plane msb{};
  Plane bwBeforeComposition{};

  RealFontPage();
  void prepare(Scene scene, bool prewarm);
  CompositionStats compose(Scene scene, int rows);
  bool hasFixtureGlyphs() const;
  uint32_t drawScene(Scene scene);

 private:
  std::optional<FontCacheManager::PrewarmScope> prewarmScope;
};

const char* sceneName(Scene scene);
const char* orientationName(GfxRenderer::Orientation orientation);

}  // namespace page_test
