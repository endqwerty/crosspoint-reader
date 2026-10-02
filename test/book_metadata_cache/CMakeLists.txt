add_executable(BookMetadataCacheTest BookMetadataCacheTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/test/chapter_html_slim_parser/ScopedAllocationFailure.cpp)
target_include_directories(BookMetadataCacheTest PRIVATE stubs
  ${REPO_ROOT}/lib/Epub/Epub ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/test/chapter_html_slim_parser)
target_link_libraries(BookMetadataCacheTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookMetadataCacheTest)

# Exercise the reader's actual count/progress/EOF contract with the full cache.
crosspoint_extract_between(epub_contract ${REPO_ROOT}/lib/Epub/Epub.cpp
  "int Epub::getSpineItemsCount() const"
  "BookMetadataCache::SpineEntry Epub::getSpineItem")
crosspoint_extract_between(reader_contract ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
  "bool EpubReaderActivity::isAtEndOfBook() const"
  "void EpubReaderActivity::onReturnFromEndOfBook()")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderContract.cpp
  "#include \"ReaderContract.h\"\n${epub_contract}\n${reader_contract}\n")
target_sources(BookMetadataCacheTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ReaderContract.cpp)
target_include_directories(BookMetadataCacheTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
