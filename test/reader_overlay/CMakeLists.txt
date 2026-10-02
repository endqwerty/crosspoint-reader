set(OVERLAY_SOURCE ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp)
set(REFRESH_SOURCE ${REPO_ROOT}/src/activities/reader/ReaderUtils.h)
set(TRANSITION_SOURCE ${REPO_ROOT}/src/activities/ActivityManager.cpp)
crosspoint_extract_between(DISCARD_BODY ${OVERLAY_SOURCE}
  "void EpubReaderActivity::discardOverlayPage()"
  "void EpubReaderActivity::openOverlay(")
crosspoint_extract_between(CLOSE_BODY ${OVERLAY_SOURCE}
  "void EpubReaderActivity::closeOverlayToPage()"
  "void EpubReaderActivity::renderOverlay()")
crosspoint_extract_between(REFRESH_BODY ${REFRESH_SOURCE}
  "inline void displayWithRefreshCycle("
  "// Display the B/W base of a page")
crosspoint_extract_between(RENDER_START ${OVERLAY_SOURCE}
  "void EpubReaderActivity::renderBook()"
  "  const auto showPendingSyncSaveError =")
string(REPLACE "renderBook()" "beginRenderForTest()" RENDER_START "${RENDER_START}")
crosspoint_extract_between(CHAPTER_JUMP ${OVERLAY_SOURCE}
  "  const auto gotoSpine = [this](int target)"
  "  const auto toolOverlay =")
crosspoint_extract_between(CLEAR_PENDING ${OVERLAY_SOURCE}
  "void EpubReaderActivity::clearPendingNavigation()"
  "bool EpubReaderActivity::saveProgress(")
crosspoint_extract_between(SUSPEND_BODY ${OVERLAY_SOURCE}
  "void EpubReaderActivity::onSuspend()"
  "bool EpubReaderActivity::loadBook()")
crosspoint_extract_between(TRANSITION_BODY ${TRANSITION_SOURCE}
  "      // Current activity has requested a new activity to be launched"
  "      // onEnter may request another pending action")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderOverlayProduction.cpp
  "#include \"ReaderOverlayFixture.h\"\nnamespace ReaderUtils {\n${REFRESH_BODY}\n}\n${DISCARD_BODY}\n${CLOSE_BODY}\n${CLEAR_PENDING}\n${SUSPEND_BODY}\n${RENDER_START}\n}\nvoid EpubReaderActivity::changeChapterForTest(int target) {\n${CHAPTER_JUMP}\ngotoSpine(target);\n}\nvoid OverlayActivityManagerFixture::transitionForTest() {\n${TRANSITION_BODY}\n}\n")

add_executable(ReaderOverlayTest ReaderOverlayTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/ReaderOverlayProduction.cpp)
target_include_directories(ReaderOverlayTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/src/activities/reader ${REPO_ROOT}/src)
target_link_libraries(ReaderOverlayTest PRIVATE GfxRefreshHost GTest::gtest_main)
gtest_discover_tests(ReaderOverlayTest)
