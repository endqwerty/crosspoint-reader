#include "SliderInputFixture.h"

template <class Activity>
class SliderInputTest : public testing::Test {
 protected:
  Activity activity;
  void SetUp() override {
    RenderLock::held = RenderLock::acquisitions = 0;
    gpio = {};
  }
  void TearDown() override { EXPECT_EQ(RenderLock::held, 0); }
  void route(void (*handler)(const fui::ActionEvent&, void*), fui::ActionEvent event = {}) {
    activity.routedHandler = handler;
    activity.routedUser = &activity;
    activity.touch.event = event;
  }
};
using SliderActivities = testing::Types<EpubReaderPercentSelectionActivity, IntervalSelectionActivity>;
TYPED_TEST_SUITE(SliderInputTest, SliderActivities);

TYPED_TEST(SliderInputTest, DragRoutesAndChangesValueUnderOneRenderLock) {
  this->route(&TypeParam::onSliderEvent, {0, 730});
  this->activity.touch.snap.touchHeld = true;
  this->activity.loop();
  EXPECT_EQ(this->activity.selection(), 73);
  EXPECT_TRUE(this->activity.draggingSlider);
  EXPECT_GT(this->activity.updates, 0);
  EXPECT_EQ(this->activity.finishes, 0);
  EXPECT_EQ(RenderLock::acquisitions, 1);
  EXPECT_EQ(RenderLock::held, 0);
}

TYPED_TEST(SliderInputTest, TouchConfirmClearsFlashAndQueuesResultWithoutReacquiringLock) {
  this->route(&TypeParam::onOkEvent);
  this->activity.loop();
  EXPECT_EQ(this->activity.app.flashClears, 1);
  EXPECT_TRUE(this->activity.hasResult);
  EXPECT_FALSE(this->activity.cancelled);
  EXPECT_EQ(this->activity.resultValue, 50);
  EXPECT_EQ(this->activity.finishes, 1);
  EXPECT_EQ(RenderLock::acquisitions, 1);
  EXPECT_EQ(RenderLock::held, 0);
}

TYPED_TEST(SliderInputTest, OutsideTapCancelsUnderLockAndReleasesOnEarlyReturn) {
  this->activity.touch.routed = true;
  this->activity.touch.snap.touchReleased = true;
  this->activity.touch.snap.touchX = 20;
  this->activity.loop();
  EXPECT_TRUE(this->activity.hasResult);
  EXPECT_TRUE(this->activity.cancelled);
  EXPECT_EQ(this->activity.finishes, 1);
  EXPECT_EQ(RenderLock::acquisitions, 1);
  EXPECT_EQ(RenderLock::held, 0);
}

TYPED_TEST(SliderInputTest, DragReleaseCannotCancelOrChangeSelection) {
  this->route(&TypeParam::onSliderEvent, {0, 600});
  this->activity.touch.snap.touchHeld = true;
  this->activity.loop();
  this->activity.touch = {};
  this->activity.touch.routed = true;
  this->activity.touch.snap.touchReleased = true;
  this->activity.touch.snap.touchX = 20;
  this->activity.mappedInput.swipe = MappedInputManager::SwipeDir::Left;
  this->activity.loop();
  EXPECT_FALSE(this->activity.draggingSlider);
  EXPECT_EQ(this->activity.selection(), 60);
  EXPECT_EQ(this->activity.finishes, 0);
  EXPECT_EQ(RenderLock::acquisitions, 2);
  EXPECT_EQ(RenderLock::held, 0);
}

TYPED_TEST(SliderInputTest, StepCallbackUpdatesValueWithoutNestedLock) {
  this->route(&TypeParam::onStepEvent, {-1, -1});
  this->activity.loop();
  EXPECT_EQ(this->activity.selection(), 49);
  EXPECT_EQ(RenderLock::acquisitions, 1);
  EXPECT_EQ(RenderLock::held, 0);
}

TYPED_TEST(SliderInputTest, PhysicalConfirmAndBackKeepResultsInsideLock) {
  this->activity.mappedInput.released[static_cast<size_t>(MappedInputManager::Button::Confirm)] = true;
  this->activity.loop();
  EXPECT_EQ(this->activity.resultValue, 50);
  EXPECT_EQ(this->activity.finishes, 1);
  EXPECT_FALSE(this->activity.cancelled);
  this->activity.mappedInput.released = {};
  this->activity.mappedInput.released[static_cast<size_t>(MappedInputManager::Button::Back)] = true;
  this->activity.loop();
  EXPECT_TRUE(this->activity.cancelled);
  EXPECT_EQ(this->activity.finishes, 2);
  EXPECT_EQ(RenderLock::acquisitions, 2);
  EXPECT_EQ(RenderLock::held, 0);
}

TYPED_TEST(SliderInputTest, EdgeButtonAdjustmentKeepsExistingDirection) {
  this->activity.mappedInput.pressed[static_cast<size_t>(MappedInputManager::Button::Up)] = true;
  this->activity.loop();
  EXPECT_EQ(this->activity.selection(), 40);
  EXPECT_EQ(RenderLock::acquisitions, 1);
  EXPECT_EQ(RenderLock::held, 0);
}

TYPED_TEST(SliderInputTest, IdleTickReleasesLockWithoutRequestingPaint) {
  this->activity.loop();
  EXPECT_EQ(this->activity.updates, 0);
  EXPECT_EQ(this->activity.finishes, 0);
  EXPECT_EQ(RenderLock::acquisitions, 1);
  EXPECT_EQ(RenderLock::held, 0);
}
