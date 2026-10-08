add_executable(ReferencePagesTest
  ReferencePagesTest.cpp
)

target_link_libraries(ReferencePagesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReferencePagesTest)
