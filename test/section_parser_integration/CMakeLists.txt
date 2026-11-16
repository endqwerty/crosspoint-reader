# Link the extracted persistence methods to the actual chapter parser with one
# consistent filesystem HAL.
set(SECTION_SOURCE ${REPO_ROOT}/lib/Epub/Epub/Section.cpp)
set(SECTION_HEADER ${REPO_ROOT}/lib/Epub/Epub/Section.h)
crosspoint_extract_between(constants ${SECTION_SOURCE}
  "constexpr uint8_t SECTION_FILE_VERSION"
  "struct SectionLookupTables")
crosspoint_extract_functions(header_and_page ${SECTION_SOURCE}
  "Section::~Section()"
  "uint32_t Section::onPageComplete("
  "void Section::appendPage("
  "bool Section::writeSectionFileHeader("
  "bool Section::loadSectionFile("
  "bool Section::clearCache(")
crosspoint_extract_functions(advance ${SECTION_SOURCE} "bool Section::buildSomeMore(")
crosspoint_extract_functions(commit_and_finish ${SECTION_SOURCE}
  "bool Section::recoverBuildBackup("
  "bool Section::commitBuildFile("
  "bool Section::finalizeBuild("
  "void Section::suspendBuild("
  "void Section::abandonBuild(")
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
