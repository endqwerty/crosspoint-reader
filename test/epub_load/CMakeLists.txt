set(EPUB_SOURCE ${REPO_ROOT}/lib/Epub/Epub.cpp)
crosspoint_extract_between(load_method ${EPUB_SOURCE}
  "bool Epub::load(const bool buildIfMissing, const bool skipLoadingCss)"
  "bool Epub::loadMetadata(")
set(CSS_SOURCE ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp)
crosspoint_extract_between(stale_predicate ${CSS_SOURCE}
  "bool CssParser::sectionCacheIsStale("
  "CssParser::CacheStatus CssParser::inspectCache()")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/EpubLoadProduction.cpp
  "#include \"EpubLoadFixture.h\"\n${load_method}\n${stale_predicate}\n")
add_executable(EpubLoadTest EpubLoadTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/EpubLoadProduction.cpp
  ${REPO_ROOT}/test/chapter_html_slim_parser/ScopedAllocationFailure.cpp)
target_include_directories(EpubLoadTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/test/chapter_html_slim_parser)
target_link_libraries(EpubLoadTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(EpubLoadTest)
