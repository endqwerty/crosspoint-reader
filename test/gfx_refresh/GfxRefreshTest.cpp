#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>

TEST(GfxRefresh, OverlayBaseHonorsManualCleanupExactlyOnce) {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
}

TEST(GfxRefresh, ExplicitGrayModesHonorCleanupAndRetainPlaneEncoding) {
  for (const auto mode :
       {HalDisplay::GrayscaleMode::Overlay, HalDisplay::GrayscaleMode::Absolute, HalDisplay::GrayscaleMode::Direct}) {
    HalDisplay display;
    GfxRenderer renderer(display);
    renderer.setFadingFix(true);
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    ASSERT_TRUE(renderer.displayGrayscaleBase(mode, HalDisplay::FAST_REFRESH));
    EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
    EXPECT_EQ(display.lastGrayMode, mode);
    EXPECT_TRUE(display.lastTurnOff);
    EXPECT_EQ(renderer.grayPlanesAreAbsolute(), mode != HalDisplay::GrayscaleMode::Overlay);
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
    EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
    EXPECT_FALSE(renderer.grayPlanesAreAbsolute());
  }
}

TEST(GfxRefresh, UnsupportedGrayscaleRetainsCleanupForBwFallback) {
  HalDisplay display;
  GfxRenderer renderer(display);
  display.acceptsGrayscale = false;
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  EXPECT_FALSE(renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, HalDisplay::FAST_REFRESH));
  EXPECT_EQ(display.refreshCount, 0u);
  EXPECT_FALSE(renderer.grayPlanesAreAbsolute());
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FULL_REFRESH);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::FAST_REFRESH);
}

TEST(GfxRefresh, OrdinaryBaseKeepsRequestedModeAndShutdownPolicy) {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::HALF_REFRESH);
  EXPECT_FALSE(display.lastTurnOff);
  renderer.setFadingFix(true);
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  EXPECT_EQ(display.lastRefresh, HalDisplay::HALF_REFRESH);
  EXPECT_TRUE(display.lastTurnOff);
}

TEST(GfxRefresh, FullHeightTargetMatchesBandedRasterInEveryOrientation) {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  display.frame.fill(0xa5);
  std::array<uint8_t, HalDisplay::BUFFER_SIZE> full{};
  std::array<uint8_t, HalDisplay::BUFFER_SIZE> banded{};
  const auto drawPattern = [&renderer] {
    const int width = renderer.getScreenWidth();
    const int height = renderer.getScreenHeight();
    // Cross every 80-row boundary, include clipped edges, and overwrite pixels
    // in both polarities so a mismatched band origin cannot pass by coincidence.
    for (int y = -1; y <= height; ++y) {
      for (int x = -1; x <= width; ++x) {
        if ((x * 13 + y * 7) % 19 < 3) renderer.drawPixel(x, y, false);
        if ((x + y * 3) % 29 == 0) renderer.drawPixel(x, y, true);
      }
    }
  };
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise, GfxRenderer::PortraitInverted,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    renderer.setOrientation(orientation);
    for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      renderer.setRenderMode(mode);
      renderer.beginStripTarget(full.data(), 0, HalDisplay::DISPLAY_HEIGHT);
      renderer.clearScreen(0);
      drawPattern();
      renderer.endStripTarget();
      for (int y = 0; y < HalDisplay::DISPLAY_HEIGHT; y += 80) {
        renderer.beginStripTarget(banded.data() + y * HalDisplay::DISPLAY_WIDTH_BYTES, y, 80);
        renderer.clearScreen(0);
        drawPattern();
        renderer.endStripTarget();
      }
      EXPECT_EQ(full, banded);
      EXPECT_TRUE(std::all_of(display.frame.begin(), display.frame.end(), [](uint8_t byte) { return byte == 0xa5; }));
    }
  }
}
