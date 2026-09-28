add_executable(EpdBusTest EpdBusTest.cpp BusProduction.cpp BusFixture.cpp)
target_include_directories(EpdBusTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src/bus
)
target_link_libraries(EpdBusTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(EpdBusTest PROPERTIES TIMEOUT 10 LABELS "display;fault-injection")

foreach(mode IN ITEMS Single Dual)
  add_executable(SsdBusy${mode}Test
    SsdBusyIntegrationTest.cpp BusProduction.cpp BusFixture.cpp
    ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp
    ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp
  )
  target_include_directories(SsdBusy${mode}Test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/include
    ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src
  )
  target_compile_definitions(SsdBusy${mode}Test PRIVATE ARDUINO=1 FREEINK_DRIVER_SSD1677=1)
  if(mode STREQUAL "Single")
    target_compile_definitions(SsdBusy${mode}Test PRIVATE EINK_DISPLAY_SINGLE_BUFFER_MODE=1)
  endif()
  target_link_libraries(SsdBusy${mode}Test PRIVATE crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(SsdBusy${mode}Test TEST_PREFIX "${mode}." PROPERTIES TIMEOUT 10 LABELS "display;fault-injection")
endforeach()
