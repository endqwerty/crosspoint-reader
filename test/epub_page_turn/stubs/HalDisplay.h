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
  enum class GrayscaleMode { Overlay, Absolute };
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
  bool lastTurnOff = false;
  bool acceptsGrayscale = true;
  bool inverted = false;
  bool committed = false;
  bool refreshPending = false;
  unsigned refreshCount = 0;
  unsigned grayRefreshCount = 0;
  unsigned cleanupCount = 0;

  uint8_t* getFrameBuffer() { return frame.data(); }
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }
  bool isInverted() const { return inverted; }
  void clearScreen(uint8_t color) { frame.fill(color); }
  void displayBuffer(RefreshMode mode, bool turnOff) {
    lastRefresh = mode;
    lastTurnOff = turnOff;
    ++refreshCount;
    committed = true;
  }
  void displayBufferAsync(RefreshMode mode) {
    displayBuffer(mode, false);
    committed = false;
    refreshPending = true;
  }
  void displayGrayscaleBase(RefreshMode mode, bool turnOff) { displayBuffer(mode, turnOff); }
  bool displayGrayscaleBase(GrayscaleMode, RefreshMode mode, bool turnOff) {
    if (!acceptsGrayscale) return false;
    displayBuffer(mode, turnOff);
    return true;
  }
  bool supportsAsyncRefresh() const { return true; }
  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode = GrayscaleMode::Overlay) const { return {}; }
  void beginDisplayWork() { committed = false; }
  bool displayCommitted() const { return committed; }
  void waitRefreshComplete() {
    if (refreshPending) committed = true;
    refreshPending = false;
  }
  void preconditionGrayscale() {}
  void preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void cleanupGrayscaleBuffers(const uint8_t*) { ++cleanupCount; }
  void displayGrayBuffer(bool) {
    ++grayRefreshCount;
    committed = true;
  }
  void writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}
  void drawImage(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t) {}
  uint8_t* lendFrameBufferStorage(uint32_t*) { return nullptr; }
  void returnFrameBufferStorage() {}
};
