add_executable(LibraryFollowOnsTest
  LibraryFollowOnsTest.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryFollowOns.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryBookState.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryBuilder.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryIndexFile.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryFormat.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryText.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
# The builder's fake SD card and EPUB metadata double serve the real index build.
target_include_directories(LibraryFollowOnsTest PRIVATE
  ${REPO_ROOT}/test/library_builder/stubs
  ${REPO_ROOT}/lib/LibraryIndex
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(LibraryFollowOnsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LibraryFollowOnsTest)
