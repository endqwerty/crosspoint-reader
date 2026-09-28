#include <gtest/gtest.h>

#include "GestureMigration.h"

TEST(SettingsMigration, LegacyAlwaysNextUsesTrunkDirectionalControls) {
  JsonDocument doc;
  doc["touchReaderControls"] = 4;
  GestureMigration settings;
  EXPECT_TRUE(settings.apply(doc));
  EXPECT_EQ(settings.touchReaderControls, settings.TOUCH_READER_ON);
  EXPECT_EQ(settings.pageTurnGesture, settings.TAP_ONLY);
  EXPECT_EQ(settings.previousPageGesture, settings.SWIPE_ONLY);
}

TEST(SettingsMigration, ExistingLegacyModesKeepTheirDirections) {
  for (uint8_t mode = 1; mode <= 3; ++mode) {
    JsonDocument doc;
    doc["touchReaderControls"] = mode;
    GestureMigration settings;
    EXPECT_TRUE(settings.apply(doc));
    EXPECT_EQ(settings.touchReaderControls, settings.TOUCH_READER_ON);
    EXPECT_EQ(settings.pageTurnGesture, mode);
    EXPECT_EQ(settings.previousPageGesture, mode);
  }
}

TEST(SettingsMigration, ExistingDirectionalChoicesPreventLegacyOverride) {
  for (const char* key : {"pageTurnGesture", "previousPageGesture"}) {
    JsonDocument doc;
    doc["touchReaderControls"] = 4;
    doc[key] = GestureMigration::PAGE_TURN_GESTURE_DISABLED;
    GestureMigration settings;
    settings.pageTurnGesture = settings.PAGE_TURN_GESTURE_DISABLED;
    settings.previousPageGesture = settings.TAP_AND_SWIPE;
    EXPECT_FALSE(settings.apply(doc));
    EXPECT_EQ(settings.pageTurnGesture, settings.PAGE_TURN_GESTURE_DISABLED);
    EXPECT_EQ(settings.previousPageGesture, settings.TAP_AND_SWIPE);
  }
}

TEST(SettingsMigration, MissingOffAndInvalidLegacyValuesDoNotResave) {
  for (const char* json :
       {"{}", "{\"touchReaderControls\":0}", "{\"touchReaderControls\":5}", "{\"touchReaderControls\":255}",
        "{\"touchReaderControls\":256}", "{\"touchReaderControls\":-1}", "{\"touchReaderControls\":\"4\"}",
        "{\"touchReaderControls\":true}", "{\"touchReaderControls\":null}"}) {
    SCOPED_TRACE(json);
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, json));
    GestureMigration settings;
    settings.touchReaderControls = settings.TOUCH_READER_OFF;
    EXPECT_FALSE(settings.apply(doc));
    EXPECT_EQ(settings.touchReaderControls, settings.TOUCH_READER_OFF);
    EXPECT_EQ(settings.pageTurnGesture, settings.SWIPE_ONLY);
    EXPECT_EQ(settings.previousPageGesture, settings.SWIPE_ONLY);
  }
}

TEST(SettingsMigration, PersistedSplitSettingsDoNotMigrateAgain) {
  JsonDocument doc;
  doc["touchReaderControls"] = 4;
  GestureMigration settings;
  ASSERT_TRUE(settings.apply(doc));
  doc["touchReaderControls"] = settings.touchReaderControls;
  doc["pageTurnGesture"] = settings.pageTurnGesture;
  doc["previousPageGesture"] = settings.previousPageGesture;
  EXPECT_FALSE(settings.apply(doc));
  EXPECT_EQ(settings.pageTurnGesture, settings.TAP_ONLY);
  EXPECT_EQ(settings.previousPageGesture, settings.SWIPE_ONLY);
}
