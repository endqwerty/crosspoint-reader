#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "BusFixture.h"
#define private public
#include "FreeInkDisplay.h"
#include "driver/Ssd1677Driver.h"
#undef private

namespace {
using namespace freeink;
using bus_test::state;

void waveform(uint8_t command) {
  if (command == state.stuckCommand) {
    state.busyLevel = HIGH;
    state.edges.clear();
    state.nextEdge = 0;
  } else if (command == 0x20) {
    ++state.activations;
    state.busyLevel = HIGH;
    state.edges.clear();
    state.nextEdge = 0;
    if (state.activations != state.stuckActivation) state.edges.push_back({state.nowUs + 5000, LOW});
  }
}

class SsdBusyTest : public ::testing::Test {
 protected:
  Ssd1677Config config = ssd1677DefaultConfig();
  Ssd1677Driver driver{config};
  FreeInkDisplay display{1, 2, 3, 4, 5, 6};
  std::vector<uint8_t> frame = std::vector<uint8_t>(48000, 0x96);
  std::vector<uint8_t> previous = std::vector<uint8_t>(48000, 0x69);

  void SetUp() override {
    bus_test::reset();
    config.fullSeqOverride = 0xF7;
    config.fastSeqOverride = 0xFC;
    config.halfSeqOverride = 0xD7;
    config.absoluteGrayscale = true;
    state.commandHook = waveform;
    display._driver = &driver;
    display.frameBuffer = frame.data();
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
    display.frameBufferActive = previous.data();
#endif
    display._bus.begin({1, 2, 3, 4, 5, 6, -1}, 40000000, BusyPolarity::ActiveHigh);
    driver.begin(display._bus);
    ASSERT_FALSE(display._bus.hasFailed());
    driver._needsInitialFull = false;
    state.writes.clear();
  }
  void TearDown() override { std::free(display._asyncShadow); }

  void expectFault() {
    EXPECT_TRUE(display._bus.hasFailed());
    EXPECT_TRUE(display._bus.isBusy());
    EXPECT_FALSE(display.displayCommitted());
    EXPECT_FALSE(display._refreshPending);
    EXPECT_FALSE(display._shadowValid);
    EXPECT_FALSE(display._redRamSynced);
    EXPECT_FALSE(driver._pendingPowerOff);
    EXPECT_TRUE(driver._needsGrayClear);
    EXPECT_FALSE(state.transaction);
    EXPECT_EQ(state.isr, nullptr);
  }

  void recover() {
    const unsigned failedTransfers = state.transfers;
    display.displayBuffer(FreeInkDisplay::FAST_REFRESH, false);
    EXPECT_TRUE(display._bus.hasFailed());
    EXPECT_EQ(state.transfers, failedTransfers);
    state.busyLevel = LOW;
    state.stuckActivation = 0;
    state.stuckCommand = -1;
    state.writes.clear();
    auto* const canvas = display.frameBuffer;
    const std::vector<uint8_t> intended(canvas, canvas + frame.size());
    display.displayBuffer(FreeInkDisplay::FAST_REFRESH, false);
    EXPECT_FALSE(display._bus.hasFailed());
    EXPECT_TRUE(display.displayCommitted());
    EXPECT_TRUE(display._redRamSynced);
    ASSERT_FALSE(state.writes.empty());
    EXPECT_EQ(state.writes.front().command, 0x12);  // controller initialization
    bool absoluteUpdate = false;
    for (const auto& write : state.writes) {
      if (write.command == 0x22 && !write.bytes.empty() && (write.bytes[0] == 0xD7 || write.bytes[0] == 0xF7))
        absoluteUpdate = true;
      if ((write.command == 0x24 || write.command == 0x26) && write.bytes.size() == intended.size())
        EXPECT_EQ(write.bytes, intended);
    }
    EXPECT_TRUE(absoluteUpdate);
  }
};

TEST_F(SsdBusyTest, BlockingTimeoutStopsPostRefreshRamAndPowerOff) {
  state.stuckActivation = 1;
  auto* const canvas = display.frameBuffer;
  display.displayBuffer(FreeInkDisplay::FAST_REFRESH, true);
  expectFault();
  EXPECT_EQ(display.frameBuffer, canvas);
  ASSERT_FALSE(state.writes.empty());
  EXPECT_EQ(state.writes.back().command, 0x20);
  EXPECT_EQ(state.activations, 1u);
  recover();
}

TEST_F(SsdBusyTest, DeferredFailureInvalidatesShadowAndDoesNotResumeDuringCleanup) {
  display.displayBuffer(FreeInkDisplay::HALF_REFRESH, false);
  ASSERT_TRUE(display.displayCommitted());
  state.stuckActivation = state.activations + 1;
  display.displayBufferAsync(FreeInkDisplay::FAST_REFRESH);
  ASSERT_TRUE(display._refreshPending);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  ASSERT_TRUE(display._shadowValid);
#endif
  display.waitRefreshComplete();
  expectFault();
  const unsigned failedTransfers = state.transfers;
  display.cleanupGrayscaleBuffers(frame.data());
  display.copyGrayscaleBuffers(frame.data(), previous.data());
  display.displayGrayBuffer(false);
  display.setCustomLUT(true, config.grayLut);
  display.deepSleep();
  display.syncRedRamFromFrameBuffer();
  EXPECT_FALSE(display._redRamSynced);
  EXPECT_EQ(state.transfers, failedTransfers);
  EXPECT_FALSE(display._shadowValid);
  EXPECT_FALSE(display.displayCommitted());
  recover();
}

TEST_F(SsdBusyTest, DeferredPowerOffTimeoutNeverCertifiesTheBaseline) {
  display.displayBuffer(FreeInkDisplay::HALF_REFRESH, false);
  state.stuckActivation = state.activations + 2;
  display.triggerDisplay(FreeInkDisplay::FAST_REFRESH, true);
  ASSERT_TRUE(display._refreshPending);
  display.waitRefreshComplete();
  expectFault();
  ASSERT_FALSE(state.writes.empty());
  EXPECT_EQ(state.writes.back().command, 0x20);
  recover();
}

TEST_F(SsdBusyTest, FactoryGrayFailureStopsLutCleanupAndCanRecover) {
  ASSERT_TRUE(display.displayGrayscaleBase(GrayscaleMode::Absolute, FreeInkDisplay::HALF_REFRESH, false));
  display.copyGrayscaleBuffers(frame.data(), previous.data());
  state.stuckActivation = state.activations + 1;
  display.displayGrayBuffer(true);
  expectFault();
  EXPECT_TRUE(display._grayPassFailed);
  ASSERT_FALSE(state.writes.empty());
  EXPECT_EQ(state.writes.back().command, 0x20);
  recover();
}

TEST_F(SsdBusyTest, InitializationFailureStopsAtTheFailedCommand) {
  for (const uint8_t command : {0x12, 0x46, 0x47}) {
    SCOPED_TRACE(command);
    state.stuckCommand = command;
    state.busyLevel = LOW;
    state.writes.clear();
    driver.begin(display._bus);
    ASSERT_TRUE(display._bus.hasFailed());
    ASSERT_FALSE(state.writes.empty());
    EXPECT_EQ(state.writes.back().command, command);
    EXPECT_FALSE(driver.displayCommitted());
    recover();
  }
}

TEST_F(SsdBusyTest, WindowFailureDoesNotWriteBaselineAfterWaveformTimeout) {
  state.stuckActivation = 1;
  display.displayWindow(0, 0, 8, 2, true);
  expectFault();
  ASSERT_FALSE(state.writes.empty());
  EXPECT_EQ(state.writes.back().command, 0x20);
  recover();
}
TEST_F(SsdBusyTest, CleanupDrainsPendingFailureBeforeAnyPlaneWrites) {
  state.stuckActivation = 1;
  display.triggerDisplay(FreeInkDisplay::FAST_REFRESH, false);
  ASSERT_TRUE(display._refreshPending);
  const unsigned submittedTransfers = state.transfers;
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  display.cleanupGrayscaleBuffers(frame.data());
#else
  display.cleanupGrayscaleWithPreviousBuffer();
#endif
  expectFault();
  EXPECT_EQ(state.transfers, submittedTransfers);
}

TEST_F(SsdBusyTest, GrayPowerOnAndSleepTimeoutsDoNotContinueTheirSequences) {
  config.grayPowerUpFirst = true;
  driver._isScreenOn = false;
  state.stuckActivation = 1;
  display.displayGrayBuffer(false);
  expectFault();
  ASSERT_FALSE(state.writes.empty());
  EXPECT_EQ(state.writes.back().command, 0x20);
  recover();

  driver._isScreenOn = true;
  state.stuckActivation = state.activations + 1;
  display.deepSleep();
  expectFault();
  EXPECT_EQ(state.writes.back().command, 0x20);  // no DEEP_SLEEP command after failed power-off
  recover();
}

TEST_F(SsdBusyTest, InvertedFailureRestoresHostBytesBeforeReturning) {
  display.setInverted(true);
  const auto originalFrame = frame;
  const auto originalPrevious = previous;
  state.stuckActivation = 1;
  display.displayBuffer(FreeInkDisplay::FAST_REFRESH, false);
  expectFault();
  EXPECT_EQ(frame, originalFrame);
  EXPECT_EQ(previous, originalPrevious);
  display.setInverted(false);
  recover();
}

TEST_F(SsdBusyTest, TypedOverlayRecoveryPreservesCanvasAndUsesIntendedBase) {
  auto* const canvas = display.frameBuffer;
  const auto intended = frame;
  const auto oldFrame = previous;
  state.stuckActivation = 1;
  display.displayBuffer(FreeInkDisplay::FAST_REFRESH, false);
  expectFault();

  state.busyLevel = LOW;
  state.stuckActivation = 0;
  state.writes.clear();
  ASSERT_TRUE(display.displayGrayscaleBase(GrayscaleMode::Overlay, FreeInkDisplay::FAST_REFRESH, false));
  EXPECT_FALSE(display._bus.hasFailed());
  EXPECT_FALSE(display._inversionDirty);
  EXPECT_TRUE(display.displayCommitted());
  EXPECT_EQ(display.frameBuffer, canvas);
  EXPECT_EQ(frame, intended);
  EXPECT_EQ(previous, oldFrame);
  unsigned baseWrites = 0;
  bool absoluteCleanup = false;
  for (const auto& write : state.writes) {
    if (write.command == 0x22 && !write.bytes.empty() && (write.bytes[0] == 0xD7 || write.bytes[0] == 0xF7))
      absoluteCleanup = true;
    if ((write.command == 0x24 || write.command == 0x26) && write.bytes.size() == intended.size()) {
      ++baseWrites;
      EXPECT_EQ(write.bytes, intended);
    }
  }
  EXPECT_TRUE(absoluteCleanup);
  EXPECT_GT(baseWrites, 0u);
}

TEST_F(SsdBusyTest, TypedOverlayRecoveryKeepsDirtyStateWhenCleanupTimesOut) {
  auto* const canvas = display.frameBuffer;
  const auto intended = frame;
  state.stuckActivation = 1;
  display.displayBuffer(FreeInkDisplay::FAST_REFRESH, false);
  expectFault();

  state.busyLevel = LOW;
  state.stuckActivation = state.activations + 1;
  state.writes.clear();
  EXPECT_FALSE(display.displayGrayscaleBase(GrayscaleMode::Overlay, FreeInkDisplay::FAST_REFRESH, false));
  expectFault();
  EXPECT_TRUE(display._inversionDirty);
  EXPECT_EQ(display.frameBuffer, canvas);
  EXPECT_EQ(frame, intended);
  ASSERT_FALSE(state.writes.empty());
  EXPECT_EQ(state.writes.back().command, 0x20);
}

}  // namespace
