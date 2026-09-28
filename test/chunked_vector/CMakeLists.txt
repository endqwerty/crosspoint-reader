add_executable(ChunkedVectorTest
  ChunkedVectorTest.cpp
  ${REPO_ROOT}/test/chapter_html_slim_parser/ScopedAllocationFailure.cpp
)

target_include_directories(ChunkedVectorTest PRIVATE
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/chapter_html_slim_parser
)

target_link_libraries(ChunkedVectorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ChunkedVectorTest)
