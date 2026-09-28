add_executable(FolderSearchTest
  FolderSearchTest.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryText.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)
target_include_directories(FolderSearchTest PRIVATE
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/lib/FolderSearch
  ${REPO_ROOT}/lib/LibraryIndex
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/FsHelpers
)
target_link_libraries(FolderSearchTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FolderSearchTest)
