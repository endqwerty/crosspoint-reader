# Reuse the extraction helper/paths from the persistence target, but link these
# methods to the actual chapter parser with one consistent filesystem HAL.
set(SECTION_SOURCE ${REPO_ROOT}/lib/Epub/Epub/Section.cpp)
set(SECTION_HEADER ${REPO_ROOT}/lib/Epub/Epub/Section.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SECTION_SOURCE} ${SECTION_HEADER})
file(READ ${SECTION_SOURCE} SECTION_IMPLEMENTATION)
file(READ ${SECTION_HEADER} SECTION_DECLARATION)
section_extract("${SECTION_IMPLEMENTATION}" "constexpr uint8_t SECTION_FILE_VERSION" "struct SectionLookupTables" constants)
section_extract("${SECTION_IMPLEMENTATION}" "Section::~Section()" "bool Section::createSectionFile(" header_and_page)
section_extract("${SECTION_IMPLEMENTATION}" "bool Section::buildSomeMore(" "bool Section::hasHtmlCache(" advance)
section_extract("${SECTION_IMPLEMENTATION}" "bool Section::recoverBuildBackup(" "std::unique_ptr<Page> Section::loadPageDuringBuild(" commit_and_finish)
section_extract("${SECTION_DECLARATION}" "std::string binTmpPath() const" "  std::unique_ptr<Page> loadPageAt(" tmp_path)
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
