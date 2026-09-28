# Exercise complete production input loops and their value/result callbacks.
function(slider_methods path start_marker end_marker output)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REPO_ROOT}/${path})
  file(READ ${REPO_ROOT}/${path} source)
  string(FIND "${source}" "${start_marker}" start)
  if(start LESS 0)
    message(FATAL_ERROR "Slider input start marker moved: ${start_marker}")
  endif()
  string(SUBSTRING "${source}" ${start} -1 remainder)
  string(FIND "${remainder}" "${end_marker}" end)
  if(end LESS 0)
    message(FATAL_ERROR "Slider input end marker moved: ${end_marker}")
  endif()
  string(SUBSTRING "${remainder}" 0 ${end} methods)
  set(${output} "${methods}" PARENT_SCOPE)
endfunction()

slider_methods(src/activities/reader/EpubReaderPercentSelectionActivity.cpp
  "void EpubReaderPercentSelectionActivity::adjustPercent("
  "void EpubReaderPercentSelectionActivity::percentScreen(" percent_methods)
slider_methods(src/activities/util/IntervalSelectionActivity.cpp
  "int IntervalSelectionActivity::clampedValue(" "void IntervalSelectionActivity::onEnter(" clamp_method)
slider_methods(src/activities/util/IntervalSelectionActivity.cpp
  "void IntervalSelectionActivity::adjustValue(" "void IntervalSelectionActivity::formatValue(" interval_methods)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SliderInputProduction.cpp
  "#include \"SliderInputFixture.h\"\n${percent_methods}\n${clamp_method}\n${interval_methods}\n")
add_executable(SliderInputTest SliderInputTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/SliderInputProduction.cpp)
target_include_directories(SliderInputTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SliderInputTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SliderInputTest)
