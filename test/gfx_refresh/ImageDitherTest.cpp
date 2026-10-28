#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "Epub/converters/DirectPixelWriter.h"

// Black & White image pages (IMAGES_DISPLAY_BW) dither images in the B/W pass and
// skip the gray planes, so only text anti-aliasing can ask for a grayscale pass.
namespace {
constexpr int CELL_X = 40;  // even, so each 2x2 block is one Bayer cell
constexpr int CELL_Y = 20;

class ImageDither : public testing::Test {
 protected:
  HalDisplay display;
  GfxRenderer renderer{display};

  void SetUp() override {
    renderer.begin();
    renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);  // logical == physical
  }
  void TearDown() override { renderer.setDitherImagesInBw(false); }

  // Writes one 2x2 Bayer cell per gray level (0=black .. 3=white), left to right.
  void writeLevels(bool writeWhiteInBw = false) {
    DirectPixelWriter writer;
    writer.init(renderer);
    for (int row = 0; row < 2; ++row) {
      writer.beginRow(CELL_Y + row);
      for (uint8_t level = 0; level < 4; ++level) {
        for (int col = 0; col < 2; ++col) writer.writePixel(CELL_X + level * 2 + col, level, writeWhiteInBw);
      }
    }
  }
  bool black(int x, int y) const {
    return (display.frame[y * HalDisplay::DISPLAY_WIDTH_BYTES + x / 8] & (0x80 >> (x & 7))) == 0;
  }
  int blackInCell(uint8_t level) const {
    int count = 0;
    for (int row = 0; row < 2; ++row) {
      for (int col = 0; col < 2; ++col) count += black(CELL_X + level * 2 + col, CELL_Y + row);
    }
    return count;
  }
};

TEST_F(ImageDither, BwPassInksGrayLevelsAsAnOrderedDither) {
  renderer.clearScreen(0xff);
  renderer.setDitherImagesInBw(true);
  writeLevels();
  EXPECT_EQ(blackInCell(0), 4);
  EXPECT_EQ(blackInCell(1), 3);  // dark gray
  EXPECT_EQ(blackInCell(2), 1);  // light gray
  EXPECT_EQ(blackInCell(3), 0);

  // A cached image that rewrites its whole box also writes the dither's white pixels.
  renderer.clearScreen(0x00);
  writeLevels(/*writeWhiteInBw=*/true);
  EXPECT_EQ(blackInCell(1), 3);
  EXPECT_EQ(blackInCell(2), 1);
  EXPECT_EQ(blackInCell(3), 0);
}

TEST_F(ImageDither, GrayscaleImageModeKeepsEveryGrayPixelBlackInTheBwBase) {
  renderer.clearScreen(0xff);
  writeLevels();
  EXPECT_EQ(blackInCell(0), 4);
  EXPECT_EQ(blackInCell(1), 4);
  EXPECT_EQ(blackInCell(2), 4);
  EXPECT_EQ(blackInCell(3), 0);
}

TEST_F(ImageDither, TextAntiAliasingPassLeavesDitheredImagesOutOfTheGrayPlanes) {
  for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    SCOPED_TRACE(plane);
    renderer.setRenderMode(plane);
    renderer.clearScreen(0x00);
    renderer.setDitherImagesInBw(true);
    writeLevels();
    EXPECT_TRUE(std::all_of(display.frame.begin(), display.frame.end(), [](uint8_t value) { return value == 0; }));

    // Grayscale image mode marks the gray levels in this plane.
    renderer.setDitherImagesInBw(false);
    writeLevels();
    EXPECT_FALSE(std::all_of(display.frame.begin(), display.frame.end(), [](uint8_t value) { return value == 0; }));
  }
  renderer.setRenderMode(GfxRenderer::BW);
}

TEST_F(ImageDither, DitheredImagesDoNotMakeThePageNeedGrayscale) {
  renderer.clearScreen(0xff);
  renderer.setDitherImagesInBw(true);
  renderer.beginTextGrayscaleTracking();
  writeLevels();
  EXPECT_FALSE(renderer.endTextGrayscaleTracking());
}
}  // namespace
