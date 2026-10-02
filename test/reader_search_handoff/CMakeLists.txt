set(READER_SOURCE ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp)
crosspoint_extract_between(methods ${READER_SOURCE}
  "void EpubReaderActivity::prepareForBookSearch()"
  "bool EpubReaderActivity::launchKOReaderSync()")
crosspoint_extract_between(menu ${READER_SOURCE}
  "    case EpubReaderMenuActivity::MenuAction::FIND_IN_BOOK:"
  "    case EpubReaderMenuActivity::MenuAction::BOOKMARKS:")
crosspoint_extract_between(clear_pending ${READER_SOURCE}
  "void EpubReaderActivity::clearPendingNavigation()"
  "bool EpubReaderActivity::saveProgress(")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderSearchProduction.cpp
  "#include \"ReaderSearchFixture.h\"\n${methods}\n${clear_pending}\nvoid EpubReaderActivity::activateFind() {\n switch (EpubReaderMenuActivity::MenuAction::FIND_IN_BOOK) {\n${menu}\n}}\n")
add_executable(ReaderSearchHandoffTest ReaderSearchHandoffTest.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ReaderSearchProduction.cpp)
target_include_directories(ReaderSearchHandoffTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/src/activities/reader)
target_link_libraries(ReaderSearchHandoffTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ReaderSearchHandoffTest)
