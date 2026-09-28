set(OVERLAY_SOURCE ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp)
set(REFRESH_SOURCE ${REPO_ROOT}/src/activities/reader/ReaderUtils.h)
set(TRANSITION_SOURCE ${REPO_ROOT}/src/activities/ActivityManager.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${OVERLAY_SOURCE} ${REFRESH_SOURCE} ${TRANSITION_SOURCE})
file(READ ${OVERLAY_SOURCE} READER_SOURCE)
file(READ ${REFRESH_SOURCE} READER_UTILS)
file(READ ${TRANSITION_SOURCE} ACTIVITY_MANAGER_SOURCE)

function(extract_between source start_marker end_marker output)
  string(FIND "${source}" "${start_marker}" start)
  string(FIND "${source}" "${end_marker}" end)
  if(start LESS 0 OR end LESS_EQUAL start)
    message(FATAL_ERROR "Reader overlay test extraction markers moved: ${start_marker}")
  endif()
  math(EXPR length "${end} - ${start}")
  string(SUBSTRING "${source}" ${start} ${length} body)
  set(${output} "${body}" PARENT_SCOPE)
endfunction()

extract_between("${READER_SOURCE}"
  "void EpubReaderActivity::discardOverlayPage()"
  "void EpubReaderActivity::openOverlay(" DISCARD_BODY)
extract_between("${READER_SOURCE}"
  "void EpubReaderActivity::closeOverlayToPage()"
  "void EpubReaderActivity::renderOverlay()" CLOSE_BODY)
extract_between("${READER_UTILS}"
  "inline void displayWithRefreshCycle("
  "// Display the B/W base of a page" REFRESH_BODY)
extract_between("${READER_SOURCE}"
  "void EpubReaderActivity::renderBook()"
  "  const auto showPendingSyncSaveError =" RENDER_START)
string(REPLACE "renderBook()" "beginRenderForTest()" RENDER_START "${RENDER_START}")
extract_between("${READER_SOURCE}"
  "  const auto gotoSpine = [this](int target)"
  "  const auto toolOverlay =" CHAPTER_JUMP)
extract_between("${READER_SOURCE}"
  "void EpubReaderActivity::clearPendingNavigation()"
  "bool EpubReaderActivity::saveProgress(" CLEAR_PENDING)
extract_between("${READER_SOURCE}"
  "void EpubReaderActivity::onSuspend()"
  "bool EpubReaderActivity::loadBook()" SUSPEND_BODY)
extract_between("${ACTIVITY_MANAGER_SOURCE}"
  "      // Current activity has requested a new activity to be launched"
  "      // onEnter may request another pending action" TRANSITION_BODY)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderOverlayProduction.cpp
  "#include \"ReaderOverlayFixture.h\"\nnamespace ReaderUtils {\n${REFRESH_BODY}\n}\n${DISCARD_BODY}\n${CLOSE_BODY}\n${CLEAR_PENDING}\n${SUSPEND_BODY}\n${RENDER_START}\n}\nvoid EpubReaderActivity::changeChapterForTest(int target) {\n${CHAPTER_JUMP}\ngotoSpine(target);\n}\nvoid OverlayActivityManagerFixture::transitionForTest() {\n${TRANSITION_BODY}\n}\n")

add_executable(ReaderOverlayTest ReaderOverlayTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/ReaderOverlayProduction.cpp)
target_include_directories(ReaderOverlayTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/src/activities/reader ${REPO_ROOT}/src)
target_link_libraries(ReaderOverlayTest PRIVATE GfxRefreshHost GTest::gtest_main)
gtest_discover_tests(ReaderOverlayTest)
