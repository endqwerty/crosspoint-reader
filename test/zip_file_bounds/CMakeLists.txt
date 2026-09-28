add_executable(ZipFileBoundsTest
  ZipFileBoundsTest.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
)
target_include_directories(ZipFileBoundsTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/content_opf_parser/stubs
  ${REPO_ROOT}/test/inflate_stream/stubs
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/miniz/src
)
target_link_libraries(ZipFileBoundsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ZipFileBoundsTest)
