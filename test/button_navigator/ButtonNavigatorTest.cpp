#include <gtest/gtest.h>

#include "AllocationProbe.h"
#include "ButtonNavigator.h"

class ButtonNavigatorTest : public testing::Test {
 protected:
  MappedInputManager input;
  ButtonNavigator navigator;
  using Button = MappedInputManager::Button;
  static size_t slot(Button b) { return static_cast<size_t>(b); }
  void SetUp() override {
    inputNow = 0;
    countButtonAllocations = false;
    buttonAllocations = buttonAllocationBytes = 0;
    ButtonNavigator::setMappedInputManager(input);
  }
};

TEST_F(ButtonNavigatorTest, IdlePollingMakesNoButtonListAllocations) {
  int callbacks = 0;
  const std::function<void()> callback = [&] { ++callbacks; };
  countButtonAllocations = true;
  for (int i = 0; i < 10000; ++i) {
    navigator.onNext(callback);
    navigator.onPrevious(callback);
  }
  countButtonAllocations = false;
  EXPECT_EQ(callbacks, 0);
  EXPECT_EQ(buttonAllocations, 0u);
  EXPECT_EQ(buttonAllocationBytes, 0u);
  RecordProperty("polls", 10000);
  RecordProperty("allocation_attempts", static_cast<int>(buttonAllocations));
  RecordProperty("requested_bytes", static_cast<int>(buttonAllocationBytes));
}

TEST_F(ButtonNavigatorTest, BorrowedDirectionListsRemainValidAcrossCalls) {
  const auto next = ButtonNavigator::getNextButtons();
  const auto previous = ButtonNavigator::getPreviousButtons();
  for (int i = 0; i < 100; ++i) {
    const auto later = ButtonNavigator::getPreviousButtons();
    EXPECT_EQ(*later.begin(), Button::NavPrevious);
  }
  ASSERT_EQ(next.size(), 1u);
  ASSERT_EQ(previous.size(), 1u);
  EXPECT_EQ(*next.begin(), Button::NavNext);
  EXPECT_EQ(*previous.begin(), Button::NavPrevious);
}

TEST_F(ButtonNavigatorTest, PressAndReleaseKeepLogicalDirectionsIndependent) {
  int next = 0, previous = 0;
  input.pressed[slot(Button::NavNext)] = true;
  navigator.onNext([&] { ++next; });
  navigator.onPrevious([&] { ++previous; });
  EXPECT_EQ(next, 1);
  EXPECT_EQ(previous, 0);
  input.pressed.fill(false);
  input.released[slot(Button::NavPrevious)] = true;
  navigator.onRelease(ButtonNavigator::getNextButtons(), [&] { ++next; });
  navigator.onPreviousRelease([&] { ++previous; });
  EXPECT_EQ(next, 1);
  EXPECT_EQ(previous, 1);
}

TEST_F(ButtonNavigatorTest, MultipleButtonsInvokeCallbackOnceAndEmptyListsDoNothing) {
  int calls = 0;
  input.pressed.fill(true);
  input.released.fill(true);
  input.held.fill(true);
  inputNow = input.heldTime = 1000;
  ButtonNavigator::onPress({Button::Left, Button::Right}, [&] { ++calls; });
  EXPECT_EQ(calls, 1);
  // A press or release frame already stepped; only a plain held frame repeats.
  navigator.onContinuous({Button::Up, Button::Down}, [&] { ++calls; });
  EXPECT_EQ(calls, 1);
  input.pressed.fill(false);
  input.released.fill(false);
  navigator.onContinuous({Button::Up, Button::Down}, [&] { ++calls; });
  EXPECT_EQ(calls, 2);
  navigator.onPressAndContinuous({}, [&] { ++calls; });
  navigator.onRelease({}, [&] { ++calls; });
  EXPECT_EQ(calls, 2);
}

TEST_F(ButtonNavigatorTest, RepeatHonorsBothThresholdsAndSuppressesReleaseAfterRepeat) {
  int calls = 0;
  input.held[slot(Button::NavNext)] = true;
  inputNow = input.heldTime = 500;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 0);
  ++inputNow;
  ++input.heldTime;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  inputNow += 500;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  ++inputNow;
  navigator.onNextContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 2);
  input.held.fill(false);
  input.released[slot(Button::NavNext)] = true;
  navigator.onRelease(ButtonNavigator::getNextButtons(), [&] { ++calls; });
  EXPECT_EQ(calls, 2);
  navigator.onRelease(ButtonNavigator::getNextButtons(), [&] { ++calls; });
  EXPECT_EQ(calls, 3);
}

TEST_F(ButtonNavigatorTest, RepeatIntervalSurvivesClockWrap) {
  int calls = 0;
  input.held[slot(Button::NavPrevious)] = true;
  input.heldTime = 1000;
  inputNow = UINT32_MAX - 100;
  navigator.onPreviousContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  inputNow = 300;
  navigator.onPreviousContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 1);
  inputNow = 401;
  navigator.onPreviousContinuous([&] { ++calls; });
  EXPECT_EQ(calls, 2);
}

TEST_F(ButtonNavigatorTest, TemporaryCallerListsAndCallbacksDoNotAllocate) {
  int calls = 0;
  input.pressed[slot(Button::Left)] = true;
  countButtonAllocations = true;
  for (int i = 0; i < 1000; ++i) navigator.onPressAndContinuous({Button::Left, Button::Right}, [&] { ++calls; });
  countButtonAllocations = false;
  EXPECT_EQ(calls, 1000);
  EXPECT_EQ(buttonAllocations, 0u);
}

TEST_F(ButtonNavigatorTest, AllocationProbeDetectsAnExplicitAllocation) {
  countButtonAllocations = true;
  void* allocation = allocateButtonProbe(16);
  freeButtonProbe(allocation);
  countButtonAllocations = false;
  EXPECT_EQ(buttonAllocations, 1u);
  EXPECT_EQ(buttonAllocationBytes, 16u);
}
