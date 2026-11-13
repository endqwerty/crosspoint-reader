set(EPUB_SOURCE ${REPO_ROOT}/lib/Epub/Epub.cpp)
crosspoint_extract_between(resolve_method ${EPUB_SOURCE}
  "int Epub::resolveHrefToSpineIndex(const std::string& href) const {" "EOF")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/EpubHrefResolveProduction.cpp
  "#include \"EpubHrefResolveFixture.h\"\n${resolve_method}\n")
add_executable(EpubHrefResolveTest EpubHrefResolveTest.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/EpubHrefResolveProduction.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(EpubHrefResolveTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/test/fs_helpers/stubs ${REPO_ROOT}/lib/FsHelpers ${REPO_ROOT}/lib/Utf8)
target_link_libraries(EpubHrefResolveTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(EpubHrefResolveTest)
