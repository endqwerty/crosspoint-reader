add_executable(ButtonNavigatorTest ButtonNavigatorTest.cpp AllocationProbe.cpp ${REPO_ROOT}/src/util/ButtonNavigator.cpp)
target_include_directories(ButtonNavigatorTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/src/util)
target_link_libraries(ButtonNavigatorTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ButtonNavigatorTest)
# Keep replacement operator new observable in optimized host allocation checks.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(ButtonNavigatorTest PRIVATE -fno-assume-sane-operator-new -fno-builtin)
endif()
