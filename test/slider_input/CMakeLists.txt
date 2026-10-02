# Exercise complete production input loops and their value/result callbacks.
crosspoint_extract_between(percent_methods ${REPO_ROOT}/src/activities/reader/EpubReaderPercentSelectionActivity.cpp
  "void EpubReaderPercentSelectionActivity::adjustPercent("
  "void EpubReaderPercentSelectionActivity::percentScreen(")
set(INTERVAL_SOURCE ${REPO_ROOT}/src/activities/util/IntervalSelectionActivity.cpp)
crosspoint_extract_between(clamp_method ${INTERVAL_SOURCE}
  "int IntervalSelectionActivity::clampedValue(" "void IntervalSelectionActivity::onEnter(")
crosspoint_extract_between(interval_methods ${INTERVAL_SOURCE}
  "void IntervalSelectionActivity::adjustValue(" "void IntervalSelectionActivity::formatValue(")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SliderInputProduction.cpp
  "#include \"SliderInputFixture.h\"\n${percent_methods}\n${clamp_method}\n${interval_methods}\n")
add_executable(SliderInputTest SliderInputTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/SliderInputProduction.cpp)
target_include_directories(SliderInputTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SliderInputTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SliderInputTest)
