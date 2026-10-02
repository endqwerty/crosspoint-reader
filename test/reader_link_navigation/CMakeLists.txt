set(LINK_SOURCE ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp)
crosspoint_extract_between(methods ${LINK_SOURCE}
  "void EpubReaderActivity::navigateToHref("
  "void EpubReaderActivity::loadCachedBookmarks()")
crosspoint_extract_between(footnote_select ${LINK_SOURCE} "void EpubReaderActivity::openDictionaryWordSelect(" "void EpubReaderActivity::loop()")
crosspoint_extract_between(percent_jump ${LINK_SOURCE} "void EpubReaderActivity::jumpToPercent(" "void EpubReaderActivity::onReaderMenuConfirm(")
crosspoint_extract_between(progress_load ${LINK_SOURCE} "  bool hasSavedProgress = false;\n  HalFile f;"
  "  loadLinkStack();")
crosspoint_extract_between(clear_pending ${LINK_SOURCE} "void EpubReaderActivity::clearPendingNavigation()" "bool EpubReaderActivity::saveProgress(")
crosspoint_extract_between(progress_result ${LINK_SOURCE} "  auto progressChangeResultHandler = [this](const ActivityResult& result) {"
  "\n  switch (action) {")
crosspoint_extract_between(chapter_result ${LINK_SOURCE} "          [this](const ActivityResult& result) {\n            if (result.isCancelled) {\n              openReaderMenu();\n              return;"
  "          });")
crosspoint_extract_between(toc_select ${LINK_SOURCE} "      const auto item = epub->getTocItem(panelIndex);"
  "    } else if (overlay == Overlay::More) {")
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
crosspoint_extract_between(chapter_activate ${CHAPTER_SOURCE} "void EpubReaderChapterSelectionActivity::activateIndex(" "bool EpubReaderChapterSelectionActivity::handleButtons()")
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/ReaderLinkNavigationProduction.cpp "\n${chapter_activate}\n")
