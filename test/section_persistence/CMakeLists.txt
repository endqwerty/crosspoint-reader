# Compile complete production persistence methods, not copied algorithms. The
# fixture replaces the parser/EPUB construction graph with supplied LUT/anchors.
set(SECTION_SOURCE ${REPO_ROOT}/lib/Epub/Epub/Section.cpp)
set(SECTION_HEADER ${REPO_ROOT}/lib/Epub/Epub/Section.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SECTION_SOURCE} ${SECTION_HEADER})
file(READ ${SECTION_SOURCE} SECTION_IMPLEMENTATION)
file(READ ${SECTION_HEADER} SECTION_DECLARATION)

function(section_extract source start_marker end_marker output)
  string(FIND "${source}" "${start_marker}" start)
  if(start LESS 0)
    message(FATAL_ERROR "Section persistence start marker moved: ${start_marker}")
  endif()
  string(SUBSTRING "${source}" ${start} -1 remainder)
  string(FIND "${remainder}" "${end_marker}" end)
  if(end LESS 0)
    message(FATAL_ERROR "Section persistence end marker moved: ${end_marker}")
  endif()
  string(SUBSTRING "${remainder}" 0 ${end} result)
  set(${output} "${result}" PARENT_SCOPE)
endfunction()

section_extract("${SECTION_IMPLEMENTATION}" "constexpr uint8_t SECTION_FILE_VERSION" "}  // namespace" constants)
section_extract("${SECTION_IMPLEMENTATION}" "uint32_t Section::onPageComplete(" "bool Section::writeSectionFileHeader(" page_complete)
section_extract("${SECTION_IMPLEMENTATION}" "bool Section::recoverBuildBackup(" "bool Section::finalizeBuild(" commit_build)
section_extract("${SECTION_IMPLEMENTATION}" "std::unique_ptr<Page> Section::loadPageDuringBuild(" "std::unique_ptr<Page> Section::loadPageAt(" load_build)
section_extract("${SECTION_DECLARATION}" "std::string binTmpPath() const" "  std::unique_ptr<Page> loadPageAt(" tmp_path)
string(FIND "${SECTION_IMPLEMENTATION}" "std::optional<uint16_t> Section::getCachedPageCount()" visible_lookup_start)
if(visible_lookup_start LESS 0)
  message(FATAL_ERROR "Visible offset lookup extraction boundary moved")
endif()
string(SUBSTRING "${SECTION_IMPLEMENTATION}" ${visible_lookup_start} -1 visible_lookup)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SectionPersistenceProduction.cpp
  "#include \"SectionPersistenceFixture.h\"\nnamespace {\n${constants}\n}\n"
  "${page_complete}\n${commit_build}\n${load_build}\n${visible_lookup}\n"
  "namespace section_test {\nuint8_t completeVersion() { return SECTION_FILE_VERSION; }\n"
  "uint8_t incompleteVersion() { return SECTION_FILE_INCOMPLETE_VERSION; }\n"
  "uint8_t partialVersion() { return SECTION_FILE_PARTIAL_VERSION; }\n}\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SectionTmpPath.h "${tmp_path}\n")

add_executable(SectionPersistenceTest SectionPersistenceTest.cpp SectionLookupTest.cpp SectionNavigationTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/SectionPersistenceProduction.cpp)
target_include_directories(SectionPersistenceTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(SectionPersistenceTest PRIVATE EpubPageTurnHost GTest::gtest_main)
gtest_discover_tests(SectionPersistenceTest)
