#include <Memory.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

void testLog(const char*, const char*, ...) {}
#define LOG_DBG(...) testLog(__VA_ARGS__)
#define LOG_ERR(...) testLog(__VA_ARGS__)

class HalDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  enum class GrayscaleMode { Overlay, Absolute, Direct };
};
enum class BmpReaderError { Ok };
class Bitmap {
 public:
  bool gray = true;
  int getWidth() const { return 8; }
  int getHeight() const { return 8; }
  bool hasGreyscale() const { return gray; }
  BmpReaderError rewindToData() const { return BmpReaderError::Ok; }
};

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  struct Capabilities {
    bool enabled;
    bool supported() const { return enabled; }
  };
  unsigned supportedModes = 0;
  bool inverted = false;
  mutable RenderMode mode = BW;
  mutable std::array<uint8_t, 8> frame{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  mutable std::array<uint8_t, 8> displayed{};
  mutable unsigned bwRefreshes = 0, grayBases = 0, lsbUploads = 0, msbUploads = 0, grayRefreshes = 0, decodePasses = 0;
  mutable HalDisplay::RefreshMode refresh = HalDisplay::FAST_REFRESH;
  Capabilities grayscaleCapabilities(HalDisplay::GrayscaleMode value) const {
    return {!inverted && (supportedModes & (1u << static_cast<unsigned>(value)))};
  }
  int getScreenWidth() const { return 8; }
  int getScreenHeight() const { return 8; }
  void clearScreen(uint8_t color = 0xff) const { frame.fill(color); }
  void invertScreen() const {
    for (auto& byte : frame) byte = static_cast<uint8_t>(~byte);
  }
  void setRenderMode(RenderMode value) const { mode = value; }
  bool paint() const {
    ++decodePasses;
    frame[2] = mode == BW ? 0xa5 : 0x18;
    return true;
  }
  bool drawBitmap(const Bitmap&, int, int, int, int, float, float, bool) const { return paint(); }
  void displayBuffer(HalDisplay::RefreshMode value) const {
    ++bwRefreshes;
    refresh = value;
    displayed = frame;
  }
  bool displayGrayscaleBase(HalDisplay::GrayscaleMode) const {
    ++grayBases;
    return true;
  }
  void displayGrayscaleBase(HalDisplay::RefreshMode) const { ++grayBases; }
  void copyGrayscaleLsbBuffers() const { ++lsbUploads; }
  void copyGrayscaleMsbBuffers() const { ++msbUploads; }
  void displayGrayBuffer() const { ++grayRefreshes; }
};

struct CrossPointSettings {
  enum class SLEEP_SCREEN_COVER_FILTER { NO_FILTER, INVERTED_BLACK_AND_WHITE };
  SLEEP_SCREEN_COVER_FILTER sleepScreenCoverFilter = SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
};
CrossPointSettings SETTINGS;
class HalFile {};
struct BitmapPlacement {
  int x = 0, y = 0;
  float cropX = 0, cropY = 0;
};
struct OverlayBmpInfo {
  int width = 8, height = 8;
  uint32_t rowBytes = 32;
};
enum class AlphaOverlayResult { NotAlphaOverlay, Error, Rendered };
enum class AlphaScanResult { Error, NotUseful, Useful };
enum class TransparentOverlayPass { BW, GrayscaleLsb, GrayscaleMsb };
BitmapPlacement calculateBitmapPlacement(int, int, const GfxRenderer&) { return {}; }
bool parseOverlayBmpHeader(HalFile&, OverlayBmpInfo&, bool) { return true; }
AlphaScanResult scanForUsefulAlpha(HalFile&, const OverlayBmpInfo&, uint8_t*) { return AlphaScanResult::Useful; }
bool renderTransparentOverlayPass(HalFile&, const OverlayBmpInfo&, const BitmapPlacement&, GfxRenderer& renderer,
                                  uint8_t*, TransparentOverlayPass) {
  return renderer.paint();
}
struct ImageDimensions {
  int width = 8, height = 8;
};
struct RenderConfig {
  int x = 0, y = 0, maxWidth = 0, maxHeight = 0;
  float sourceCropX = 0, sourceCropY = 0;
  bool useDithering = false, useExactDimensions = false, preserveAlpha = false;
};
class PngToFramebufferConverter {
 public:
  static bool getDimensionsStatic(const std::string&, ImageDimensions&) { return true; }
  bool decodeToFramebuffer(const std::string&, const GfxRenderer& renderer, const RenderConfig&) {
    return renderer.paint();
  }
};
class SleepActivity {
 public:
  GfxRenderer& renderer;
  explicit SleepActivity(GfxRenderer& value) : renderer(value) {}
  void renderBitmapSleepScreen(const Bitmap&, bool preserveBackground) const;
  bool renderTransparentOverlayPng(const std::string&) const;
};

#include "SleepFunctions.inc"

enum class Path { TransparentBmp, BitmapCover, TransparentPng };
class SleepGrayscale : public testing::TestWithParam<Path> {
 protected:
  GfxRenderer renderer;
  void render() {
    SleepActivity sleep(renderer);
    if (GetParam() == Path::TransparentBmp) {
      HalFile file;
      ASSERT_EQ(tryRenderTransparentOverlayBmp(file, renderer, "overlay.bmp"), AlphaOverlayResult::Rendered);
    } else if (GetParam() == Path::BitmapCover) {
      sleep.renderBitmapSleepScreen(Bitmap{}, true);
    } else {
      ASSERT_TRUE(sleep.renderTransparentOverlayPng("overlay.png"));
    }
  }
  void expectMonochrome() {
    render();
    EXPECT_EQ(renderer.decodePasses, 1u);
    EXPECT_EQ(renderer.bwRefreshes, 1u);
    EXPECT_EQ(renderer.refresh, HalDisplay::HALF_REFRESH);
    EXPECT_EQ(renderer.grayBases, 0u);
    EXPECT_EQ(renderer.lsbUploads, 0u);
    EXPECT_EQ(renderer.msbUploads, 0u);
    EXPECT_EQ(renderer.grayRefreshes, 0u);
    EXPECT_EQ(renderer.mode, GfxRenderer::BW);
    EXPECT_EQ(renderer.frame[2], 0xa5);
    EXPECT_EQ(renderer.displayed, renderer.frame);
    EXPECT_EQ(renderer.frame[0], 0xff);
  }
};
TEST_P(SleepGrayscale, UnsupportedControllerPreservesAndDisplaysBwImage) { expectMonochrome(); }
TEST_P(SleepGrayscale, InvertedPanelUsesBwEvenWhenControllerSupportsGray) {
  renderer.supportedModes = 7;
  renderer.inverted = true;
  expectMonochrome();
}
TEST_P(SleepGrayscale, SupportedModesKeepTheirTwoPlanePipeline) {
  for (unsigned mode = 0; mode < 3; ++mode) {
    renderer = GfxRenderer{};
    renderer.supportedModes = 1u << mode;
    render();
    EXPECT_EQ(renderer.decodePasses, 3u);
    EXPECT_EQ(renderer.bwRefreshes, 0u);
    EXPECT_EQ(renderer.grayBases, 1u);
    EXPECT_EQ(renderer.lsbUploads, 1u);
    EXPECT_EQ(renderer.msbUploads, 1u);
    EXPECT_EQ(renderer.grayRefreshes, 1u);
    EXPECT_EQ(renderer.mode, GfxRenderer::BW);
  }
}
INSTANTIATE_TEST_SUITE_P(ImagePaths, SleepGrayscale,
                         testing::Values(Path::TransparentBmp, Path::BitmapCover, Path::TransparentPng));
