#include <gtest/gtest.h>

struct HalDisplay {
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  bool inverted = false;
  int polarityChanges = 0;
  bool isInverted() const { return inverted; }
  void setInverted(bool value) {
    inverted = value;
    ++polarityChanges;
  }
};
struct CrossPointSettings {
  enum class SLEEP_SCREEN_MODE { COVER, QUICK_RESUME, TRANSPARENT_CUSTOM };
  enum class QUICK_RESUME_SLEEP_SCREEN { OFF, QUICK_RESUME_AFTER_TIMEOUT };
  SLEEP_SCREEN_MODE sleepScreen = SLEEP_SCREEN_MODE::COVER;
  QUICK_RESUME_SLEEP_SCREEN quickResumeSleepScreen = QUICK_RESUME_SLEEP_SCREEN::OFF;
};
CrossPointSettings SETTINGS;
struct Activity {
  void onEnter() {}
};
constexpr int MoonIcon = 1, MOONICON_HEIGHT = 8, MOONICON_WIDTH = 8;
struct Renderer {
  mutable int images = 0, bwSubmissions = 0, grayscaleSubmissions = 0;
  mutable HalDisplay::RefreshMode refresh = HalDisplay::FULL_REFRESH;
  int getScreenHeight() const { return 800; }
  void drawImage(int, int, int, int, int) const { ++images; }
  void displayBuffer(HalDisplay::RefreshMode value) const {
    ++bwSubmissions;
    refresh = value;
  }
  void displayGrayscaleBase(HalDisplay::RefreshMode value) const {
    ++grayscaleSubmissions;
    refresh = value;
  }
};
struct Gpio {
  bool x3 = false;
  bool deviceIsX3() const { return x3; }
};
struct SleepActivity : Activity {
  bool fromTimeout = false;
  HalDisplay display;
  Renderer renderer;
  Gpio gpio;
  void onEnter();
  void renderLastScreenSleepScreen() const;
};
#include "QuickResume.inc"

TEST(QuickResume, NightAndDayRetainPolarityWithOneFastSubmission) {
  for (bool night : {false, true}) {
    for (bool x3 : {false, true}) {
      for (bool timeout : {false, true}) {
        SETTINGS = {};
        if (timeout)
          SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
        else
          SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME;
        SleepActivity sleep;
        sleep.fromTimeout = timeout;
        sleep.display.inverted = night;
        sleep.gpio.x3 = x3;
        sleep.onEnter();
        EXPECT_EQ(sleep.display.inverted, night);
        EXPECT_EQ(sleep.display.polarityChanges, 0);
        EXPECT_EQ(sleep.renderer.images, 1);
        EXPECT_EQ(sleep.renderer.refresh, HalDisplay::FAST_REFRESH);
        EXPECT_EQ(sleep.renderer.bwSubmissions, x3 ? 0 : 1);
        EXPECT_EQ(sleep.renderer.grayscaleSubmissions, x3 ? 1 : 0);
      }
    }
  }
}
TEST(QuickResume, TimeoutPreferenceDoesNotChangeExplicitCoverSleep) {
  SETTINGS = {};
  SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  SleepActivity sleep;
  sleep.display.inverted = true;
  sleep.onEnter();
  EXPECT_FALSE(sleep.display.inverted);
  EXPECT_EQ(sleep.display.polarityChanges, 1);
  EXPECT_EQ(sleep.renderer.images, 0);
  EXPECT_EQ(sleep.renderer.bwSubmissions + sleep.renderer.grayscaleSubmissions, 0);
}
