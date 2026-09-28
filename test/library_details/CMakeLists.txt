# Compile the full activity and SDK renderer; extract the actual orientation-safe area helper.
set(theme_source ${REPO_ROOT}/src/components/UITheme.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${theme_source})
file(READ ${theme_source} theme)
string(FIND "${theme}" "Rect UITheme::getScreenSafeArea(" start)
string(FIND "${theme}" "std::string UITheme::getCoverThumbPath(" end)
if(start LESS 0 OR end LESS start)
 message(FATAL_ERROR "Theme safe-area helper moved")
endif()
math(EXPR length "${end} - ${start}")
string(SUBSTRING "${theme}" ${start} ${length} method)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ThemeProduction.cpp "#include \"components/UITheme.h\"\n${method}")
add_executable(LibraryDetailsTest LibraryDetailsTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/ThemeProduction.cpp
 ${REPO_ROOT}/src/activities/library/LibraryBookDetailsActivity.cpp
 ${REPO_ROOT}/test/chapter_html_slim_parser/ScopedAllocationFailure.cpp
 ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(LibraryDetailsTest PRIVATE stubs ${REPO_ROOT}/src/activities/library
 ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/test/chapter_html_slim_parser
 ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_link_libraries(LibraryDetailsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LibraryDetailsTest)
