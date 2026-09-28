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
function(cache_contract source_path start_marker end_marker output)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source_path})
  file(READ ${source_path} source)
  string(FIND "${source}" "${start_marker}" start)
  if(start LESS 0)
    message(FATAL_ERROR "Cache contract start marker moved: ${start_marker}")
  endif()
  string(SUBSTRING "${source}" ${start} -1 remainder)
  string(FIND "${remainder}" "${end_marker}" end)
  if(end LESS 0)
    message(FATAL_ERROR "Cache contract end marker moved: ${end_marker}")
  endif()
  string(SUBSTRING "${remainder}" 0 ${end} result)
  set(${output} "${result}" PARENT_SCOPE)
endfunction()
cache_contract(${REPO_ROOT}/lib/Epub/Epub.cpp
  "int Epub::getSpineItemsCount() const" "BookMetadataCache::SpineEntry Epub::getSpineItem" epub_contract)
cache_contract(${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
  "bool EpubReaderActivity::isAtEndOfBook() const" "void EpubReaderActivity::onReturnFromEndOfBook()" reader_contract)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderContract.cpp
  "#include \"ReaderContract.h\"\n${epub_contract}\n${reader_contract}\n")
target_sources(BookMetadataCacheTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ReaderContract.cpp)
target_include_directories(BookMetadataCacheTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
