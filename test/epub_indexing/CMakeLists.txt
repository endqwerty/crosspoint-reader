set(EPUB_SOURCE ${REPO_ROOT}/lib/Epub/Epub.cpp)
crosspoint_extract_between(parse_methods ${EPUB_SOURCE}
  "bool Epub::parseContentOpf("
  "void Epub::discoverCssFilesFromZip()")
crosspoint_extract_between(load_method ${EPUB_SOURCE}
  "bool Epub::load(const bool buildIfMissing, const bool skipLoadingCss)"
  "bool Epub::loadMetadata(")
crosspoint_extract_between(sync_method ${EPUB_SOURCE}
  "bool Epub::loadSyncMetadata("
  "bool Epub::loadMetadata(LibraryMetadata")
set(methods "#include \"EpubIndexingFixture.h\"\n${parse_methods}\n${load_method}\n${sync_method}\n")
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
