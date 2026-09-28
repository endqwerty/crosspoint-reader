find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SLEEP_SOURCE ${REPO_ROOT}/src/activities/boot_sleep/SleepActivity.cpp)
set(EXTRACTED ${CMAKE_CURRENT_BINARY_DIR}/QuickResume.inc)
add_custom_command(OUTPUT ${EXTRACTED}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py ${SLEEP_SOURCE} ${EXTRACTED}
  DEPENDS ${SLEEP_SOURCE} extract.py
  VERBATIM)
add_executable(QuickResumeTest QuickResumeTest.cpp ${EXTRACTED})
target_include_directories(QuickResumeTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(QuickResumeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(QuickResumeTest)
