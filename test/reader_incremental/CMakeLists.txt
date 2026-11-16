# Compile the production build orchestration and render path through progress
# persistence; only the section parser, SD, renderer and platform are substituted.
set(READER_SOURCE ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp)
set(READER_HEADER ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${READER_HEADER})
file(READ ${READER_HEADER} header)
set(constants "")
foreach(name BUILD_PAGES_PER_CHUNK BACKGROUND_BUILD_PAGES_PER_TICK BACKGROUND_BUILD_MIN_FREE_HEAP
    RENDER_MIN_FREE_HEAP BACKGROUND_BUILD_MIN_MAX_ALLOC PARTIAL_REBUILD_START_MARGIN BUILD_POPUP_PAGE_THRESHOLD
    BUILD_POPUP_BYTE_THRESHOLD BUILD_POPUP_DEADLINE_MS)
  string(REGEX MATCH "static constexpr [^;\n]+ ${name} = [^;]+;" declaration "${header}")
  if(NOT declaration)
    message(FATAL_ERROR "Incremental reader constant moved: ${name}")
  endif()
  string(APPEND constants "${declaration}\n")
endforeach()
string(REGEX MATCH "bool hasPendingSectionJump\\(\\) const \\{[^}]+\\}" pending_helper "${header}")
if(NOT pending_helper)
  message(FATAL_ERROR "Incremental reader pending helper moved")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderIncrementalConstants.h "${constants}\n${pending_helper}\n")
crosspoint_extract_functions(build_methods ${READER_SOURCE}
  "bool EpubReaderActivity::buildTickHeapGate()"
  "bool EpubReaderActivity::resolvePendingSectionJump("
  "bool EpubReaderActivity::backgroundBuildWanted("
  "void EpubReaderActivity::advanceSectionBuild("
  "void EpubReaderActivity::showBuildPopup("
  "bool EpubReaderActivity::anyButtonPressed("
  "bool EpubReaderActivity::backgroundBuildTick("
  "void EpubReaderActivity::idlePrewarmTick(")
crosspoint_extract_between(render_path ${READER_SOURCE} "bool EpubReaderActivity::skipLoopDelay()" "\n  showPendingSyncSaveError();")
crosspoint_extract_functions(deferred_methods ${READER_SOURCE}
  "bool EpubReaderActivity::applyDeferredReposition()"
  "void EpubReaderActivity::clearDeferredReposition("
  "void EpubReaderActivity::clearPendingNavigation(")
crosspoint_extract_functions(page_navigation ${READER_SOURCE}
  "bool EpubReaderActivity::pageTurn("
  "bool EpubReaderActivity::skipPages(")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderIncrementalProduction.cpp
  "#include \"ReaderIncrementalFixture.h\"\n${build_methods}\n${deferred_methods}\n${page_navigation}\n${render_path}\n}\n")
add_executable(ReaderIncrementalTest ReaderIncrementalTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/ReaderIncrementalProduction.cpp)
target_include_directories(ReaderIncrementalTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src/activities/reader ${REPO_ROOT}/lib/Epub)
target_link_libraries(ReaderIncrementalTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ReaderIncrementalTest)
