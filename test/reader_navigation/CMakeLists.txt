# Exercise the production navigation methods without the reader's rendering,
# storage, or UI dependencies. Fail closed if an extraction boundary moves.
crosspoint_extract_between(reader_loop ${REPO_ROOT}/src/activities/reader/ReaderActivity.cpp "void ReaderActivity::loop()" "void ReaderActivity::render(")
crosspoint_extract_between(forced_refresh ${REPO_ROOT}/src/activities/reader/ReaderActivity.cpp "bool ReaderActivity::handleForcedRefresh()" "EOF")
crosspoint_extract_between(xtc_navigation ${REPO_ROOT}/src/activities/reader/XtcReaderActivity.cpp "bool XtcReaderActivity::pageTurn(" "void XtcReaderActivity::saveProgress()")
crosspoint_extract_between(epub_navigation ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp "bool EpubReaderActivity::pageTurn(" "bool EpubReaderActivity::handleLoadFailure()")
crosspoint_extract_between(epub_end_of_book ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp "bool EpubReaderActivity::isAtEndOfBook() const" "bool EpubReaderActivity::skipLoopDelay(")
crosspoint_extract_between(epub_automatic_wait ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
  "    if (hasPendingSectionJump() || pendingBuildError) {"
  "    if (RenderLock::peek())")
crosspoint_extract_between(epub_automatic ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
  "    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {"
  "\n  }\n\n  // While the end-of-book")
crosspoint_extract_between(epub_input ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
  "  constexpr unsigned long kMinManualTurnGapMs = 200;"
  "\n}\n\nvoid EpubReaderActivity::jumpToPercent(")

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderNavigationProduction.cpp
  "#include \"ReaderNavigationFixture.h\"\n"
  "${reader_loop}\n${forced_refresh}\n${xtc_navigation}\n${epub_navigation}\n${epub_end_of_book}\n"
  "void EpubReaderActivity::dispatchAutomatic() {\n${epub_automatic_wait}\n${epub_automatic}\n}\n"
  "void EpubReaderActivity::dispatchInput() {\n const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);\n${epub_input}\n}\n")

add_executable(ReaderNavigationTest ReaderNavigationTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/ReaderNavigationProduction.cpp)
target_include_directories(ReaderNavigationTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ReaderNavigationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderNavigationTest)
