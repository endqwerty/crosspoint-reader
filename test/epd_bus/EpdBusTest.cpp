#include <gtest/gtest.h>

#include "BusFixture.h"
#include "EpdBus.h"

namespace {
using bus_test::state;
using freeink::BusyPolarity;

class EpdBusTest : public testing::Test {
 protected:
  freeink::EpdBus bus;
  void SetUp() override { bus_test::reset(); }
  void begin(BusyPolarity polarity, int level, bool semaphore = true) {
    state.busyLevel = level;
    state.allowSemaphore = semaphore;
    bus.begin({1, 2, 3, 4, 5, 6, 7}, 40000000, polarity);
    state.nowUs = 0;  // Each scheduled waveform starts after power-up settling.
  }
};

TEST_F(EpdBusTest, UcIdleHighNoOpStillSettlesOneTick) {
  begin(BusyPolarity::UcIdleHigh, HIGH);
  bus.setBusyWaitHooks(bus_test::beginHook, bus_test::endHook);
  bus.waitBusy();
  EXPECT_EQ(state.nowUs, 1000u);
  EXPECT_EQ(state.beginHooks, 0u);
  EXPECT_EQ(state.endHooks, 0u);
}

TEST_F(EpdBusTest, UcBusyBeyondThirtySecondsWaitsForIdle) {
  begin(BusyPolarity::UcIdleHigh, LOW);
  state.edges = {{45000000, HIGH}};
  bus.setBusyWaitHooks(bus_test::beginHook, bus_test::endHook);
  bus.waitBusy();
  EXPECT_EQ(state.nowUs, 45000000u);
  EXPECT_FALSE(bus.isBusy());
  EXPECT_EQ(state.beginHooks, 1u);
  EXPECT_EQ(state.endHooks, 1u);
  EXPECT_EQ(state.transfers, 0u);
}

TEST_F(EpdBusTest, UcRefreshCompletionUsesTheSameLongWaitContract) {
  begin(BusyPolarity::UcIdleHigh, LOW);
  state.edges = {{45000000, HIGH}};
  bus.waitRefreshComplete();
  EXPECT_EQ(state.nowUs, 45000000u);
  EXPECT_EQ(state.attaches, 0u);
  EXPECT_FALSE(bus.isBusy());
}

TEST_F(EpdBusTest, UcSliceHookCanWaitWithoutBeginHook) {
  begin(BusyPolarity::UcIdleHigh, LOW);
  state.edges = {{60000, HIGH}};
  state.sliceWaits = true;
  bus.setBusyWaitSliceHook(bus_test::sliceHook);
  bus.waitBusy();
  EXPECT_EQ(state.nowUs, 60000u);
  EXPECT_GT(state.sliceHooks, 0u);
  EXPECT_EQ(state.slicePin, 6);
  EXPECT_EQ(state.sliceLevel, LOW);
}

TEST_F(EpdBusTest, DeclinedSliceHookFallsBackToDelayAndBalancesHooks) {
  begin(BusyPolarity::UcIdleHigh, LOW);
  state.edges = {{60000, HIGH}};
  bus.setBusyWaitHooks(bus_test::beginHook, bus_test::endHook);
  bus.setBusyWaitSliceHook(bus_test::sliceHook);
  bus.waitBusy();
  EXPECT_EQ(state.nowUs, 60000u);
  EXPECT_GT(state.sliceHooks, 0u);
  EXPECT_EQ(state.beginHooks, 1u);
  EXPECT_EQ(state.endHooks, 1u);
}

TEST_F(EpdBusTest, ActiveHighRefreshWaitsForDelayedAssertionAndCompletion) {
  begin(BusyPolarity::ActiveHigh, LOW);
  state.edges = {{5000, HIGH}, {60000, LOW}};
  bus.waitRefreshComplete();
  EXPECT_EQ(state.nowUs, 60000u);
  EXPECT_EQ(state.attaches, 1u);
  EXPECT_EQ(state.interruptMode, CHANGE);
  EXPECT_EQ(state.detaches, 1u);
  EXPECT_EQ(state.isr, nullptr);
}

TEST_F(EpdBusTest, HookedActiveHighRefreshAlsoWaitsForDelayedAssertion) {
  begin(BusyPolarity::ActiveHigh, LOW);
  state.edges = {{5000, HIGH}, {60000, LOW}};
  bus.setBusyWaitSliceHook(bus_test::sliceHook);
  bus.waitRefreshComplete();
  EXPECT_EQ(state.nowUs, 60000u);
  EXPECT_EQ(state.attaches, 0u);
}

TEST_F(EpdBusTest, StaleSemaphoreTokenDoesNotFakeCompletion) {
  begin(BusyPolarity::ActiveHigh, LOW);
  state.token = true;
  state.edges = {{5000, HIGH}, {60000, LOW}};
  bus.waitRefreshComplete();
  EXPECT_EQ(state.nowUs, 60000u);
  EXPECT_EQ(state.detaches, 1u);
}

TEST_F(EpdBusTest, NoAssertionDetachesInterruptAfterGrace) {
  begin(BusyPolarity::ActiveHigh, LOW);
  bus.waitRefreshComplete();
  EXPECT_EQ(state.nowUs, 20000u);
  EXPECT_EQ(state.detaches, 1u);
  EXPECT_EQ(state.isr, nullptr);
}

TEST_F(EpdBusTest, ActiveLowCommandWaitHandlesDelayedAssertion) {
  begin(BusyPolarity::ActiveLow, HIGH);
  state.edges = {{5000, LOW}, {65000, HIGH}};
  bus.waitBusy();
  EXPECT_EQ(state.nowUs, 65000u);
  EXPECT_FALSE(bus.isBusy());
}

TEST_F(EpdBusTest, SemaphoreAllocationFailureStillWaitsForDelayedRefresh) {
  begin(BusyPolarity::ActiveHigh, LOW, false);
  ASSERT_EQ(state.failedSemaphoreAllocations, 1u);
  state.edges = {{5000, HIGH}, {60000, LOW}};
  bus.waitRefreshComplete();
  EXPECT_GE(state.nowUs, 60000u) << "Fallback returned before the pending waveform finished";
}

TEST_F(EpdBusTest, ActiveHighCommandCannotReturnReadyWhileStuckBusy) {
  begin(BusyPolarity::ActiveHigh, HIGH);
  ASSERT_TRUE(bus.isBusy());
  EXPECT_FALSE(bus.waitBusy());
  EXPECT_GT(state.nowUs, 30000000u);
  EXPECT_TRUE(bus.isBusy());
  EXPECT_TRUE(bus.hasFailed());
  const unsigned transfers = state.transfers;
  bus.cmd(0x24);
  bus.data(0xFF);
  EXPECT_EQ(state.transfers, transfers);
}

TEST_F(EpdBusTest, ActiveHighRefreshCannotReturnReadyWhileStuckBusy) {
  begin(BusyPolarity::ActiveHigh, HIGH);
  ASSERT_TRUE(bus.isBusy());
  bus.setBusyWaitHooks(bus_test::beginHook, bus_test::endHook);
  EXPECT_FALSE(bus.waitRefreshComplete());
  EXPECT_GE(state.nowUs, 30000000u);
  EXPECT_EQ(state.beginHooks, state.endHooks);
  EXPECT_EQ(state.isr, nullptr);
  EXPECT_TRUE(bus.isBusy());
  EXPECT_TRUE(bus.hasFailed());
  const unsigned transfers = state.transfers;
  bus.cmd(0x20);
  EXPECT_EQ(state.transfers, transfers);
}
TEST_F(EpdBusTest, FailedWaitBlocksEverySpiEntryUntilResetReachesIdle) {
  begin(BusyPolarity::ActiveHigh, HIGH);
  ASSERT_FALSE(bus.waitBusy());
  const uint8_t bytes[] = {1, 2};
  const unsigned transfers = state.transfers;
  bus.cmd(0x24);
  bus.data(0xFF);
  bus.data(bytes, sizeof(bytes));
  bus.cmdData(0x24, bytes, sizeof(bytes));
  bus.cmdData2(0x24, 1, 2);
  bus.beginTxn();
  bus.rawCmd(0x24);
  bus.rawData(1);
  bus.rawWriteBytes(bytes, sizeof(bytes));
  bus.endTxn();
  bus.sendPlaneFlipped(0x24, bytes, 1, sizeof(bytes));
  bus.sendPlaneFlippedInverted(0x24, bytes, 1, sizeof(bytes));
  bus.fillPlane(0x24, 0xFF, 1, sizeof(bytes));
  EXPECT_EQ(state.transfers, transfers);
  EXPECT_FALSE(state.transaction);

  bus.reset();
  EXPECT_TRUE(bus.hasFailed());
  bus.cmd(0x12);
  EXPECT_EQ(state.transfers, transfers);
  state.busyLevel = LOW;
  bus.reset();
  EXPECT_FALSE(bus.hasFailed());
  EXPECT_TRUE(bus.waitBusy());
  bus.cmd(0x12);
  EXPECT_EQ(state.transfers, transfers + 1);
}

TEST_F(EpdBusTest, TimedOutWaitDoesNotRestartHooksOrWaitUntilReset) {
  begin(BusyPolarity::ActiveHigh, HIGH);
  bus.setBusyWaitHooks(bus_test::beginHook, bus_test::endHook);
  ASSERT_FALSE(bus.waitBusy());
  const auto elapsed = state.nowUs;
  EXPECT_EQ(state.beginHooks, 1u);
  EXPECT_EQ(state.endHooks, 1u);
  EXPECT_FALSE(bus.waitBusy());
  EXPECT_FALSE(bus.waitRefreshComplete());
  EXPECT_EQ(state.nowUs, elapsed);
  EXPECT_EQ(state.beginHooks, 1u);
  EXPECT_EQ(state.endHooks, 1u);
}

}  // namespace
