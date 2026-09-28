# Exercise the production navigation methods without the reader's rendering,
# storage, or UI dependencies. Fail closed if an extraction boundary moves.
function(navigation_source file start_marker end_marker output)
  set(source_path ${REPO_ROOT}/src/activities/reader/${file})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source_path})
  file(READ ${source_path} source)
  string(FIND "${source}" "${start_marker}" start)
  if(start LESS 0)
    message(FATAL_ERROR "Reader navigation start marker moved: ${file}: ${start_marker}")
  endif()
  string(SUBSTRING "${source}" ${start} -1 remainder)
  if(end_marker STREQUAL "EOF")
    set(${output} "${remainder}" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${remainder}" "${end_marker}" end)
  if(end LESS 0)
    message(FATAL_ERROR "Reader navigation end marker moved: ${file}: ${end_marker}")
  endif()
  string(SUBSTRING "${remainder}" 0 ${end} result)
  set(${output} "${result}" PARENT_SCOPE)
endfunction()

navigation_source(ReaderActivity.cpp "void ReaderActivity::loop()" "void ReaderActivity::render(" reader_loop)
navigation_source(ReaderActivity.cpp "bool ReaderActivity::handleForcedRefresh()" "EOF" forced_refresh)
navigation_source(XtcReaderActivity.cpp "bool XtcReaderActivity::pageTurn(" "void XtcReaderActivity::saveProgress()" xtc_navigation)
navigation_source(EpubReaderActivity.cpp "bool EpubReaderActivity::pageTurn(" "bool EpubReaderActivity::handleLoadFailure()" epub_navigation)
navigation_source(EpubReaderActivity.cpp "bool EpubReaderActivity::isAtEndOfBook() const" "bool EpubReaderActivity::skipLoopDelay(" epub_end_of_book)
navigation_source(EpubReaderActivity.cpp
  "    if (hasPendingSectionJump() || pendingBuildError) {"
  "    if (RenderLock::peek())" epub_automatic_wait)
navigation_source(EpubReaderActivity.cpp
  "    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {"
  "\n  }\n\n  // While the end-of-book" epub_automatic)
navigation_source(EpubReaderActivity.cpp
  "  constexpr unsigned long kMinManualTurnGapMs = 200;"
  "\n}\n\nvoid EpubReaderActivity::jumpToPercent(" epub_input)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderNavigationProduction.cpp
  "#include \"ReaderNavigationFixture.h\"\n"
  "${reader_loop}\n${forced_refresh}\n${xtc_navigation}\n${epub_navigation}\n${epub_end_of_book}\n"
  "void EpubReaderActivity::dispatchAutomatic() {\n${epub_automatic_wait}\n${epub_automatic}\n}\n"
  "void EpubReaderActivity::dispatchInput() {\n const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);\n${epub_input}\n}\n")

add_executable(ReaderNavigationTest ReaderNavigationTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/ReaderNavigationProduction.cpp)
target_include_directories(ReaderNavigationTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ReaderNavigationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderNavigationTest)
