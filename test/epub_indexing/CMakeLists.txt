set(EPUB_SOURCE ${REPO_ROOT}/lib/Epub/Epub.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${EPUB_SOURCE})
file(READ ${EPUB_SOURCE} source)
set(methods "#include \"EpubIndexingFixture.h\"\n")
foreach(section parse load)
  if(section STREQUAL "parse")
    set(first "bool Epub::parseContentOpf(")
    set(last "void Epub::discoverCssFilesFromZip()")
  else()
    set(first "bool Epub::load(const bool buildIfMissing, const bool skipLoadingCss)")
    set(last "bool Epub::loadMetadata(")
  endif()
  string(FIND "${source}" "${first}" start)
  string(FIND "${source}" "${last}" end)
  if(start LESS 0 OR end LESS start)
    message(FATAL_ERROR "EPUB ${section} method boundaries moved")
  endif()
  math(EXPR length "${end} - ${start}")
  string(SUBSTRING "${source}" ${start} ${length} part)
  string(APPEND methods "${part}\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/EpubIndexingProduction.cpp "${methods}")
add_executable(EpubIndexingTest EpubIndexingTest.cpp XmlAllocationFaults.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/EpubIndexingProduction.cpp
  ${REPO_ROOT}/test/huge_book_index/HeapCap.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TocNavParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TocNcxParser.cpp)
target_compile_definitions(EpubIndexingTest PRIVATE XML_ParserCreate=testXmlParserCreate)
target_include_directories(EpubIndexingTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/test/huge_book_index/stubs ${REPO_ROOT}/test/huge_book_index
  ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Epub/Epub ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/XmlParserUtils)
target_link_libraries(EpubIndexingTest PRIVATE crosspoint_test_common FirmwareExpat GTest::gtest_main)
gtest_discover_tests(EpubIndexingTest)
