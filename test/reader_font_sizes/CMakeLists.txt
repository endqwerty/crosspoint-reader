add_executable(ReaderFontSizesTest ReaderFontSizesTest.cpp ${REPO_ROOT}/src/ReaderFontSizes.cpp)
target_include_directories(ReaderFontSizesTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/lib/EpdFont)
target_link_libraries(ReaderFontSizesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderFontSizesTest)

set(SETTINGS_SOURCE ${REPO_ROOT}/src/CrossPointSettings.cpp)
crosspoint_extract_between(font_policy ${SETTINGS_SOURCE}
  "bool CrossPointSettings::enforceReaderFont()" "void CrossPointSettings::clearSdFontFamily()")
crosspoint_extract_between(font_id ${SETTINGS_SOURCE}
  "int CrossPointSettings::getReaderFontId() const" "EOF")
# This render-loop getter must remain independent of SD resolution and allocation.
if(font_id MATCHES "resolveFont|getFontRegistry|readerFontPointSizes|std::vector|new |malloc\\(|makeUnique")
  message(FATAL_ERROR "Reader font getter must use only built-in IDs and allocation-free snapping")
endif()
crosspoint_extract_between(line_compression ${SETTINGS_SOURCE}
  "float CrossPointSettings::getReaderLineCompression() const" "unsigned long CrossPointSettings::getSleepTimeoutMs() const")
crosspoint_extract_between(json_policy ${SETTINGS_SOURCE}
  "  // Ignore saved reader families" "  // Dictionary folder name")
crosspoint_extract_between(font_enum ${REPO_ROOT}/src/CrossPointSettings.h
  "  enum FONT_FAMILY {" "  static constexpr uint8_t BUILTIN_FONT_COUNT")
crosspoint_extract_between(line_enum ${REPO_ROOT}/src/CrossPointSettings.h
  "  enum LINE_COMPRESSION {" "  enum PARAGRAPH_ALIGNMENT {")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderFontPolicyProduction.inc
  "${font_policy}\n${font_id}\n${line_compression}\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderFontPolicyEnums.inc "${font_enum}\n${line_enum}\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderFontJsonPolicy.inc "${json_policy}\n")
add_executable(ReaderFontPolicyTest ReaderFontPolicyTest.cpp ${REPO_ROOT}/src/ReaderFontSizes.cpp)
target_include_directories(ReaderFontPolicyTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src ${REPO_ROOT}/lib/EpdFont)
target_link_libraries(ReaderFontPolicyTest PRIVATE crosspoint_test_common ArduinoJson GTest::gtest_main)
gtest_discover_tests(ReaderFontPolicyTest)
