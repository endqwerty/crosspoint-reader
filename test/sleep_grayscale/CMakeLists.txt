find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SLEEP_SOURCE ${REPO_ROOT}/src/activities/boot_sleep/SleepActivity.cpp)
set(SLEEP_FUNCTIONS ${CMAKE_CURRENT_BINARY_DIR}/SleepFunctions.inc)
add_custom_command(OUTPUT ${SLEEP_FUNCTIONS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_functions.py ${SLEEP_SOURCE} ${SLEEP_FUNCTIONS}
  DEPENDS ${SLEEP_SOURCE} extract_functions.py
  VERBATIM)
add_executable(SleepGrayscaleTest SleepGrayscaleTest.cpp ${SLEEP_FUNCTIONS})
target_include_directories(SleepGrayscaleTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/lib/Memory)
target_link_libraries(SleepGrayscaleTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SleepGrayscaleTest NO_PRETTY_VALUES)
