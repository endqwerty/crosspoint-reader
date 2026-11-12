# Compile the production Sync On Sleep hand-off against a minimal reader stand-in.
crosspoint_extract_functions(handoff ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
  "void EpubReaderActivity::prepareForSleep()")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SleepSyncHandoffProduction.cpp
  "#include \"SleepSyncHandoffFixture.h\"\n${handoff}\n")
add_executable(SleepSyncHandoffTest SleepSyncHandoffTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/SleepSyncHandoffProduction.cpp)
target_include_directories(SleepSyncHandoffTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/lib/KOReaderSync)
target_link_libraries(SleepSyncHandoffTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SleepSyncHandoffTest)
