#pragma once

#include <Arduino.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

class HalDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  enum class GrayscaleMode { Overlay, Absolute, Direct };
  enum class GrayscaleBase { Separate, Combined };
  struct GrayscaleCapabilities {
    bool asyncBase = false;
    bool stripUploads = true;
    GrayscaleBase base = GrayscaleBase::Separate;
  };
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  std::array<uint8_t, BUFFER_SIZE> frame{};
  RefreshMode lastRefresh = FAST_REFRESH;
  GrayscaleMode lastGrayMode = GrayscaleMode::Overlay;
  bool lastTurnOff = false;
  bool acceptsGrayscale = true;
  bool inverted = false;
  bool lastInverted = false;
  bool asyncAvailable = true;
  bool displaySucceeds = true;
  bool waitSucceeds = true;
  bool graySucceeds = true;
  bool cleanupSucceeds = true;
  bool combinedGray = false;
  bool committed = false;
  bool refreshPending = false;
  bool combinedPending = false;
  unsigned refreshCount = 0;
  unsigned beginWorkCount = 0;
  unsigned cleanupCount = 0;
  unsigned waitCount = 0;
  uint8_t firstCleanupByte = 0;

  uint8_t* getFrameBuffer() { return frame.data(); }
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }
  bool isInverted() const { return inverted; }
  void setInverted(bool value) { inverted = value; }
  void clearScreen(uint8_t color) { frame.fill(color); }
  void displayBuffer(RefreshMode mode, bool turnOff) {
    lastRefresh = mode;
    lastInverted = inverted;
    lastTurnOff = turnOff;
    ++refreshCount;
    committed = displaySucceeds;
  }
  void displayBufferAsync(RefreshMode mode) {
    displayBuffer(mode, false);
    committed = false;
    refreshPending = true;
  }
  void displayGrayscaleBase(RefreshMode mode, bool turnOff) {
    displayBuffer(mode, turnOff);
    if (combinedGray) {
      committed = false;
      combinedPending = true;
    }
  }
  bool displayGrayscaleBase(GrayscaleMode grayMode, RefreshMode mode, bool turnOff) {
    if (!acceptsGrayscale) return false;
    lastGrayMode = grayMode;
    displayGrayscaleBase(mode, turnOff);
    return displaySucceeds;
  }
  bool supportsAsyncRefresh() const { return asyncAvailable; }
  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode = GrayscaleMode::Overlay) const {
    GrayscaleCapabilities caps;
    if (combinedGray) caps.base = GrayscaleBase::Combined;
    return caps;
  }
  void beginDisplayWork() {
    ++beginWorkCount;
    committed = false;
    combinedPending = false;
  }
  bool displayCommitted() const { return committed; }
  void waitRefreshComplete() {
    if (refreshPending) {
      ++waitCount;
      committed = displaySucceeds && waitSucceeds;
      refreshPending = false;
    }
  }
  void preconditionGrayscale() {}
  void preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void cleanupGrayscaleBuffers(const uint8_t* baseline) {
    ++cleanupCount;
    firstCleanupByte = baseline ? baseline[0] : 0;
    waitRefreshComplete();
    if (!cleanupSucceeds)
      committed = false;
    else if (combinedPending)
      committed = true;
    combinedPending = false;
  }
  void displayGrayBuffer(bool) {
    committed = graySucceeds;
    combinedPending = false;
  }
  void writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}
  void drawImage(const uint8_t* bitmap, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    const size_t stride = (width + 7) / 8;
    for (unsigned row = 0; row < height && y + row < DISPLAY_HEIGHT; ++row) {
      for (unsigned col = 0; col < width && x + col < DISPLAY_WIDTH; ++col) {
        const uint8_t mask = 0x80 >> ((x + col) & 7);
        auto& destination = frame[(y + row) * DISPLAY_WIDTH_BYTES + (x + col) / 8];
        if (bitmap[row * stride + col / 8] & (0x80 >> (col & 7)))
          destination |= mask;
        else
          destination &= static_cast<uint8_t>(~mask);
      }
    }
  }
  uint8_t* lendFrameBufferStorage(uint32_t*) { return nullptr; }
  void returnFrameBufferStorage() {}
};
