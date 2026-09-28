#include <gtest/gtest.h>

#include "RenderLockFixture.h"

class RenderLockTest : public testing::Test {
 protected:
  void SetUp() override { semaphore = {}; }
};

TEST_F(RenderLockTest, ContendedTryNeverWaitsOrReleasesAnotherOwnersMutex) {
  semaphore.held = true;
  {
    RenderLock lock(RenderLock::Mode::Try);
    EXPECT_FALSE(lock.ownsLock());
    EXPECT_EQ(semaphore.lastWait, 0u);
    EXPECT_EQ(semaphore.takeCalls, 1u);
    lock.unlock();
    EXPECT_EQ(semaphore.giveCalls, 0u);
    EXPECT_TRUE(semaphore.held);
  }
  EXPECT_EQ(semaphore.giveCalls, 0u);
  EXPECT_TRUE(semaphore.held);
}

TEST_F(RenderLockTest, SuccessfulTryOwnsTheMutexUntilScopeExit) {
  {
    RenderLock lock(RenderLock::Mode::Try);
    EXPECT_TRUE(lock.ownsLock());
    EXPECT_TRUE(semaphore.held);
    EXPECT_EQ(semaphore.lastWait, 0u);
    EXPECT_EQ(semaphore.giveCalls, 0u);
  }
  EXPECT_FALSE(semaphore.held);
  EXPECT_EQ(semaphore.giveCalls, 1u);
}

TEST_F(RenderLockTest, ExplicitUnlockAndDestructionReleaseOnlyOnce) {
  {
    RenderLock lock(RenderLock::Mode::Try);
    lock.unlock();
    EXPECT_FALSE(lock.ownsLock());
    EXPECT_FALSE(semaphore.held);
    lock.unlock();
  }
  EXPECT_EQ(semaphore.giveCalls, 1u);
}

TEST_F(RenderLockTest, ExistingConstructorsKeepBlockingAcquisition) {
  {
    RenderLock lock;
    EXPECT_TRUE(lock.ownsLock());
    EXPECT_EQ(semaphore.lastWait, portMAX_DELAY);
  }
  Activity activity;
  {
    RenderLock lock(activity);
    EXPECT_TRUE(lock.ownsLock());
    EXPECT_EQ(semaphore.lastWait, portMAX_DELAY);
  }
  EXPECT_EQ(semaphore.takeCalls, 2u);
  EXPECT_EQ(semaphore.giveCalls, 2u);
}

TEST_F(RenderLockTest, RetryCanAcquireAfterTheOwnerReleases) {
  semaphore.held = true;
  {
    RenderLock contended(RenderLock::Mode::Try);
    ASSERT_FALSE(contended.ownsLock());
  }
  semaphore.held = false;
  {
    RenderLock retry(RenderLock::Mode::Try);
    EXPECT_TRUE(retry.ownsLock());
  }
  EXPECT_EQ(semaphore.takeCalls, 2u);
  EXPECT_EQ(semaphore.giveCalls, 1u);
}

class MainSchedulingTest : public RenderLockTest {
 protected:
  void SetUp() override {
    RenderLockTest::SetUp();
    activityManager = {};
    powerManager = {};
    gpio = {};
    schedulingNow = 100000;
    lastActivityTime = 0;
    delayCalls = delayedMs = yieldCalls = 0;
    yieldedWhileLocked = false;
  }
};

TEST_F(MainSchedulingTest, BusyRenderSkipsHintAndKeepsPowerModeWithoutSpinning) {
  semaphore.held = true;
  activityManager.wantsFastLoop = true;
  runMainLoopTail();
  EXPECT_EQ(activityManager.hintCalls, 0);
  EXPECT_EQ(semaphore.lastWait, 0u);
  EXPECT_EQ(semaphore.takeCalls, 1u);
  EXPECT_EQ(semaphore.giveCalls, 0u);
  EXPECT_TRUE(semaphore.held);
  EXPECT_EQ(powerManager.calls, 0);
  EXPECT_EQ(delayCalls, 1u);
  EXPECT_EQ(delayedMs, 10u);
  EXPECT_EQ(yieldCalls, 0u);
}

TEST_F(MainSchedulingTest, AvailableWorkQueriesUnderLockThenYieldsAfterUnlock) {
  activityManager.wantsFastLoop = true;
  runMainLoopTail();
  EXPECT_EQ(activityManager.hintCalls, 1);
  EXPECT_TRUE(activityManager.hintWasLocked);
  EXPECT_FALSE(semaphore.held);
  EXPECT_EQ(semaphore.giveCalls, 1u);
  EXPECT_EQ(powerManager.calls, 1);
  EXPECT_FALSE(powerManager.saving);
  EXPECT_FALSE(powerManager.changedWhileLocked);
  EXPECT_EQ(yieldCalls, 1u);
  EXPECT_FALSE(yieldedWhileLocked);
  EXPECT_EQ(delayCalls, 0u);
}

TEST_F(MainSchedulingTest, RecentIdleKeepsExistingTenMillisecondDelay) {
  lastActivityTime = schedulingNow;
  runMainLoopTail();
  EXPECT_TRUE(activityManager.hintWasLocked);
  EXPECT_EQ(delayedMs, 10u);
  EXPECT_EQ(delayCalls, 1u);
  EXPECT_EQ(yieldCalls, 0u);
  EXPECT_EQ(powerManager.calls, 0);
  EXPECT_FALSE(semaphore.held);
}

TEST_F(MainSchedulingTest, LongIdleKeepsPowerSavingAndShortInputSlices) {
  runMainLoopTail();
  EXPECT_TRUE(activityManager.hintWasLocked);
  EXPECT_TRUE(powerManager.saving);
  EXPECT_FALSE(powerManager.changedWhileLocked);
  EXPECT_EQ(delayedMs, 50u);
  EXPECT_EQ(delayCalls, 5u);
  EXPECT_EQ(yieldCalls, 0u);
}

TEST_F(MainSchedulingTest, IdleInputWakesAtFirstSlice) {
  gpio.active = true;
  runMainLoopTail();
  EXPECT_EQ(delayedMs, 10u);
  EXPECT_EQ(delayCalls, 1u);
  EXPECT_FALSE(semaphore.held);
}

TEST_F(MainSchedulingTest, RepeatedContentionNeverReadsSectionHintAndRetryResumesWork) {
  semaphore.held = true;
  activityManager.wantsFastLoop = true;
  for (int i = 0; i < 1000; ++i) runMainLoopTail();
  EXPECT_EQ(activityManager.hintCalls, 0);
  EXPECT_EQ(semaphore.giveCalls, 0u);
  EXPECT_EQ(delayedMs, 10000u);
  EXPECT_EQ(yieldCalls, 0u);
  EXPECT_EQ(powerManager.calls, 0);
  semaphore.held = false;
  runMainLoopTail();
  EXPECT_EQ(activityManager.hintCalls, 1);
  EXPECT_TRUE(activityManager.hintWasLocked);
  EXPECT_EQ(semaphore.giveCalls, 1u);
  EXPECT_EQ(yieldCalls, 1u);
}
