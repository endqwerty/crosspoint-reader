# Use the production macro contract; a function stub accepts expressions the
# firmware macro cannot compile.
set(i18n_header ${REPO_ROOT}/lib/I18n/I18n.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${i18n_header})
file(STRINGS ${i18n_header} translation_macro REGEX "^#define tr\\(id\\)")
if(NOT translation_macro)
  message(FATAL_ERROR "I18n translation macro moved")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/I18nTranslationMacro.h "#pragma once\n${translation_macro}\n")

crosspoint_extract_between(matching ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp "struct ProgressRange {" "std::string buildReadFolderDestination(")
crosspoint_extract_between(epsilon ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp "constexpr float bookmarkProgressEpsilon =" "int clampPercent(")
crosspoint_extract_between(capacity ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp "constexpr size_t initialBookmarkCacheCapacity =" "constexpr float bookmarkProgressEpsilon =")
crosspoint_extract_between(add ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp "void EpubReaderActivity::addBookmark()" "void EpubReaderActivity::updateBookmarkFlag()")
crosspoint_extract_between(load ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp "void EpubReaderActivity::loadCachedBookmarks()" "void EpubReaderActivity::addBookmark()")
crosspoint_extract_between(update_flag ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp "void EpubReaderActivity::updateBookmarkFlag()" "ScreenshotInfo EpubReaderActivity::getScreenshotInfo()")
crosspoint_extract_between(feedback ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp "  if (showBookmarkMessage) {\n    GUI.drawPopup(" "\n  if (showDictionaryMessage) {")
crosspoint_extract_between(rename ${REPO_ROOT}/src/activities/reader/EpubReaderBookmarksActivity.cpp "void EpubReaderBookmarksActivity::startRename()" "void EpubReaderBookmarksActivity::showBookmarkActions()")
crosspoint_extract_between(remove ${REPO_ROOT}/src/activities/reader/EpubReaderBookmarksActivity.cpp "void EpubReaderBookmarksActivity::deleteSelectedBookmark()" "void EpubReaderBookmarksActivity::buildScreen(")
crosspoint_extract_between(open ${REPO_ROOT}/src/activities/reader/EpubReaderBookmarksActivity.cpp "void EpubReaderBookmarksActivity::openSelectedBookmark()" "void EpubReaderBookmarksActivity::activateIndex(")
crosspoint_extract_between(enter ${REPO_ROOT}/src/activities/reader/EpubReaderBookmarksActivity.cpp "void EpubReaderBookmarksActivity::onEnter()" "// Derives bookmarkSubtitles")
crosspoint_extract_between(buttons ${REPO_ROOT}/src/activities/reader/EpubReaderBookmarksActivity.cpp "bool EpubReaderBookmarksActivity::handleButtons()" "void EpubReaderBookmarksActivity::startRename()")
crosspoint_extract_between(screen ${REPO_ROOT}/src/activities/reader/EpubReaderBookmarksActivity.cpp "void EpubReaderBookmarksActivity::buildScreen(" "void EpubReaderBookmarksActivity::render(")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BookmarkActionsProduction.cpp
  "#include \"BookmarkActionsFixture.h\"\nnamespace {\n${capacity}\n${epsilon}\n${matching}\n}\n${load}\n${add}\n${update_flag}\n${rename}\n${remove}\n${open}\n${enter}\n${buttons}\n${screen}\n"
  "void EpubReaderActivity::renderFeedback() {\nRenderLock lock;\n${feedback}\n}\n")

add_executable(BookmarkActionsTest BookmarkActionsTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/BookmarkActionsProduction.cpp)
target_include_directories(BookmarkActionsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src)
target_link_libraries(BookmarkActionsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookmarkActionsTest)
