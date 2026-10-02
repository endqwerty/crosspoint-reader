# Link the extracted persistence methods to the actual chapter parser with one
# consistent filesystem HAL.
set(SECTION_SOURCE ${REPO_ROOT}/lib/Epub/Epub/Section.cpp)
set(SECTION_HEADER ${REPO_ROOT}/lib/Epub/Epub/Section.h)
crosspoint_extract_between(constants ${SECTION_SOURCE}
  "constexpr uint8_t SECTION_FILE_VERSION"
  "struct SectionLookupTables")
crosspoint_extract_between(header_and_page ${SECTION_SOURCE}
  "Section::~Section()"
  "bool Section::createSectionFile(")
crosspoint_extract_between(advance ${SECTION_SOURCE}
  "bool Section::buildSomeMore("
  "bool Section::hasHtmlCache(")
crosspoint_extract_between(commit_and_finish ${SECTION_SOURCE}
  "bool Section::recoverBuildBackup("
  "std::unique_ptr<Page> Section::loadPageDuringBuild(")
crosspoint_extract_between(tmp_path ${SECTION_HEADER}
  "std::string binTmpPath() const"
  "  std::unique_ptr<Page> loadPageAt(")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SectionTmpPath.h "${tmp_path}\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SectionIntegrationProduction.cpp
  "#include \"SectionIntegrationFixture.h\"\nnamespace {\n${constants}\n}\n"
  "${header_and_page}\n${advance}\n${commit_and_finish}\n")
add_executable(SectionParserIntegrationTest SectionParserIntegrationTest.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/SectionIntegrationProduction.cpp
  ${REPO_ROOT}/lib/Epub/Epub/SectionPageReader.cpp)
target_include_directories(SectionParserIntegrationTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(SectionParserIntegrationTest PRIVATE ChapterHtmlParserHost GTest::gtest_main)
gtest_discover_tests(SectionParserIntegrationTest)
