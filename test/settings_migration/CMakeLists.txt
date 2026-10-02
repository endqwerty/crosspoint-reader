# Compile the production legacy gesture migration with real ArduinoJson. The
# generic settings loader and SD persistence are outside this focused fixture.
crosspoint_extract_between(gesture_enums ${REPO_ROOT}/src/CrossPointSettings.h
  "  enum TOUCH_READER_CONTROLS" "  // How the reader menu opens")
crosspoint_extract_between(migration ${REPO_ROOT}/src/CrossPointSettings.cpp
  "  // Older files stored one combined touch mode" "  if (doc[\"sleepTimeoutMinutes\"].isNull()")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/GestureMigration.h
  "#pragma once\n#include <ArduinoJson.h>\n#include <cstdint>\nstruct GestureMigration {\n"
  "${gesture_enums}\n"
  "uint8_t touchReaderControls = TOUCH_READER_ON;\n"
  "uint8_t pageTurnGesture = SWIPE_ONLY;\nuint8_t previousPageGesture = SWIPE_ONLY;\n"
  "bool apply(const JsonDocument& doc) {\nbool needsResave = false;\n${migration}\nreturn needsResave;\n}\n};\n")
add_executable(SettingsMigrationTest SettingsMigrationTest.cpp)
target_include_directories(SettingsMigrationTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(SettingsMigrationTest PRIVATE crosspoint_test_common ArduinoJson GTest::gtest_main)
gtest_discover_tests(SettingsMigrationTest)
