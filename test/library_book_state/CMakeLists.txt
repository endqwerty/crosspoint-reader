add_executable(LibraryBookStateTest LibraryBookStateTest.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryBookState.cpp)
target_include_directories(LibraryBookStateTest PRIVATE
  ${REPO_ROOT}/test/library_builder/stubs ${REPO_ROOT}/lib/LibraryIndex)
target_link_libraries(LibraryBookStateTest PRIVATE GTest::gtest_main)
gtest_discover_tests(LibraryBookStateTest)
