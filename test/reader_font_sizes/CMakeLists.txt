add_executable(ReaderFontSizesTest ReaderFontSizesTest.cpp ${REPO_ROOT}/src/ReaderFontSizes.cpp)
target_include_directories(ReaderFontSizesTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/lib/EpdFont)
target_link_libraries(ReaderFontSizesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderFontSizesTest)
