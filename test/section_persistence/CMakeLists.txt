# Compile complete production persistence methods, not copied algorithms. The
# fixture replaces the parser/EPUB construction graph with supplied LUT/anchors.
set(SECTION_SOURCE ${REPO_ROOT}/lib/Epub/Epub/Section.cpp)
set(SECTION_HEADER ${REPO_ROOT}/lib/Epub/Epub/Section.h)

crosspoint_extract_between(constants ${SECTION_SOURCE}
  "constexpr uint8_t SECTION_FILE_VERSION"
  "}  // namespace")
crosspoint_extract_between(page_complete ${SECTION_SOURCE}
  "uint32_t Section::onPageComplete("
  "bool Section::writeSectionFileHeader(")
crosspoint_extract_between(commit_build ${SECTION_SOURCE}
  "bool Section::recoverBuildBackup("
  "bool Section::finalizeBuild(")
crosspoint_extract_between(load_build ${SECTION_SOURCE}
  "std::unique_ptr<Page> Section::loadPageDuringBuild("
  "std::unique_ptr<Page> Section::loadPageAt(")
crosspoint_extract_between(tmp_path ${SECTION_HEADER}
  "std::string binTmpPath() const"
  "  std::unique_ptr<Page> loadPageAt(")
crosspoint_extract_between(visible_lookup ${SECTION_SOURCE}
  "std::optional<uint16_t> Section::getCachedPageCount()"
  EOF)

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
