# Compile the full activity and SDK renderer; extract the actual orientation-safe area helper.
set(theme_source ${REPO_ROOT}/src/components/UITheme.cpp)
crosspoint_extract_between(method ${theme_source}
 "Rect UITheme::getContentArea(" "std::string UITheme::getCoverThumbPath(")
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
