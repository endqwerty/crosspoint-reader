#include <Bitmap.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

// Decoder seam: the production bitmap compositor receives one known row with
// black, dark-gray, light-gray, and white. Image decoding is outside this test.
Bitmap::~Bitmap() = default;
BmpReaderError Bitmap::parseHeaders() {
  width = 4;
  height = 1;
  topDown = true;
  bpp = 2;
  rowBytes = 4;
  return BmpReaderError::Ok;
}
BmpReaderError Bitmap::readNextRow(uint8_t* data, uint8_t*) const {
  data[0] = 0x1b;
  return BmpReaderError::Ok;
}

TEST(GfxRefresh, DirectCoverWhiteOverlayKeepsExistingPixelsInEveryOrientation) {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  HalFile file;
  Bitmap bitmap(file);
  ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise, GfxRenderer::PortraitInverted,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    renderer.setOrientation(orientation);
    for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      ASSERT_TRUE(renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Direct));
      renderer.setRenderMode(plane);
      renderer.clearScreen(0);
      ASSERT_TRUE(renderer.drawBitmap(bitmap, 20, 30, 4, 1, 0, 0, true));
      const auto transparent = display.frame;
      renderer.drawPixel(23, 30, false);
      const auto whiteFilled = display.frame;
      EXPECT_NE(transparent, whiteFilled);
      renderer.clearScreen(0);
      ASSERT_TRUE(renderer.drawBitmap(bitmap, 20, 30, 4, 1, 0, 0, false));
      EXPECT_EQ(display.frame, whiteFilled);
      renderer.setRenderMode(GfxRenderer::BW);
    }
  }
}
