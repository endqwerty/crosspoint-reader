add_executable(TextTruncationTest TextTruncationTest.cpp
  ${REPO_ROOT}/test/epub_page_turn/HostAllocations.cpp)
target_include_directories(TextTruncationTest PRIVATE ${REPO_ROOT}/test/epub_page_turn)
target_link_libraries(TextTruncationTest PRIVATE GfxRefreshHost GTest::gtest_main)
gtest_discover_tests(TextTruncationTest NO_PRETTY_VALUES)
