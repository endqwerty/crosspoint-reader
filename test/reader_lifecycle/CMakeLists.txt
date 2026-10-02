# Compile the production opening, remember-once and end-screen paths against
# storage/display seams. The renderer-to-main-task publication helper is real.
set(READER_SOURCE ${REPO_ROOT}/src/activities/reader/ReaderActivity.cpp)
set(READER_HEADER ${REPO_ROOT}/src/activities/reader/ReaderActivity.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${READER_HEADER})
file(READ ${READER_HEADER} header)
crosspoint_extract_between(opening ${READER_SOURCE} "void ReaderActivity::onEnter()" "void ReaderActivity::onExit()")
crosspoint_extract_between(rendering ${READER_SOURCE} "void ReaderActivity::render(" "bool ReaderActivity::handleForcedRefresh()")
string(REGEX MATCH "void markPageRendered\\(\\) \\{[^}]+\\}" mark "${header}")
if(NOT mark)
  message(FATAL_ERROR "Reader publication helper moved")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderPublication.h "${mark}\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderLifecycleProduction.cpp
  "#include \"ReaderLifecycleFixture.h\"\n${opening}\n${rendering}\n")
add_executable(ReaderLifecycleTest ReaderLifecycleTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/ReaderLifecycleProduction.cpp)
target_include_directories(ReaderLifecycleTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ReaderLifecycleTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderLifecycleTest)
