# Compile the production legacy gesture migration with real ArduinoJson. The
# generic settings loader and SD persistence are outside this focused fixture.
function(settings_fragment path start_marker end_marker output)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${path})
  file(READ ${path} source)
  string(FIND "${source}" "${start_marker}" start)
  if(start LESS 0)
    message(FATAL_ERROR "Settings migration start marker moved: ${start_marker}")
  endif()
  string(SUBSTRING "${source}" ${start} -1 remainder)
  string(FIND "${remainder}" "${end_marker}" end)
  if(end LESS 0)
    message(FATAL_ERROR "Settings migration end marker moved: ${end_marker}")
  endif()
  string(SUBSTRING "${remainder}" 0 ${end} result)
  set(${output} "${result}" PARENT_SCOPE)
endfunction()
settings_fragment(${REPO_ROOT}/src/CrossPointSettings.h
  "  enum TOUCH_READER_CONTROLS" "  // How the reader menu opens" gesture_enums)
settings_fragment(${REPO_ROOT}/src/CrossPointSettings.cpp
  "  // Older files stored one combined touch mode" "  if (doc[\"sleepTimeoutMinutes\"].isNull()" migration)
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
