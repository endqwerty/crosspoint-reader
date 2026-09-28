set(LINK_SOURCE ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LINK_SOURCE})
file(READ ${LINK_SOURCE} reader_source)
string(FIND "${reader_source}" "void EpubReaderActivity::navigateToHref(" start)
string(FIND "${reader_source}" "void EpubReaderActivity::loadCachedBookmarks()" end)
if(start LESS 0 OR end LESS_EQUAL start)
  message(FATAL_ERROR "Reader link-navigation extraction markers moved")
endif()
math(EXPR length "${end} - ${start}")
string(SUBSTRING "${reader_source}" ${start} ${length} methods)
function(link_source start_marker end_marker output)
  string(FIND "${reader_source}" "${start_marker}" start)
  if(start LESS 0)
    message(FATAL_ERROR "Reader link extraction start marker moved: ${start_marker}")
  endif()
  string(SUBSTRING "${reader_source}" ${start} -1 remainder)
  string(FIND "${remainder}" "${end_marker}" end)
  if(end LESS 0)
    message(FATAL_ERROR "Reader link extraction end marker moved: ${end_marker}")
  endif()
  string(SUBSTRING "${remainder}" 0 ${end} body)
  set(${output} "${body}" PARENT_SCOPE)
endfunction()
link_source("void EpubReaderActivity::openDictionaryWordSelect(" "void EpubReaderActivity::loop()" footnote_select)
link_source("void EpubReaderActivity::jumpToPercent(" "void EpubReaderActivity::onReaderMenuConfirm(" percent_jump)
link_source("  bool hasSavedProgress = false;\n  HalFile f;"
  "  loadLinkStack();" progress_load)
link_source("void EpubReaderActivity::clearPendingNavigation()" "bool EpubReaderActivity::saveProgress(" clear_pending)
link_source("  auto progressChangeResultHandler = [this](const ActivityResult& result) {"
  "\n  switch (action) {" progress_result)
link_source("          [this](const ActivityResult& result) {\n            if (result.isCancelled) {"
  "          });" chapter_result)
link_source("      const auto item = epub->getTocItem(panelIndex);"
  "    } else if (overlay == Overlay::More) {" toc_select)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderLinkNavigationProduction.cpp
  "#include \"ReaderLinkNavigationFixture.h\"\n${methods}\n${footnote_select}\n${percent_jump}\n${clear_pending}\n"
  "void EpubReaderActivity::loadSavedProgress() {\n${progress_load}\n}\n"
  "void EpubReaderActivity::returnFromProgress(const ActivityResult& result) {\n${progress_result}\nprogressChangeResultHandler(result);\n}\n"
  "void EpubReaderActivity::returnFromChapter(const ActivityResult& result) {\nauto handler = ${chapter_result}\n};\nhandler(result);\n}\n"
  "void EpubReaderActivity::selectToc() {\n${toc_select}\n}\n")

add_executable(ReaderLinkNavigationTest ReaderLinkNavigationTest.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ReaderLinkNavigationProduction.cpp)
target_include_directories(ReaderLinkNavigationTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/src/activities/reader)
target_link_libraries(ReaderLinkNavigationTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ReaderLinkNavigationTest)

set(CHAPTER_SOURCE ${REPO_ROOT}/src/activities/reader/EpubReaderChapterSelectionActivity.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CHAPTER_SOURCE})
file(READ ${CHAPTER_SOURCE} reader_source)
link_source("void EpubReaderChapterSelectionActivity::activateIndex(" "bool EpubReaderChapterSelectionActivity::handleButtons()" chapter_activate)
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/ReaderLinkNavigationProduction.cpp "\n${chapter_activate}\n")
