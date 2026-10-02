find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(ui_source ${REPO_ROOT}/src/activities/library/LibraryListActivity.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ui_source} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py
  ${REPO_ROOT}/src/activities/UiTabListActivity.cpp ${REPO_ROOT}/src/util/ButtonNavigator.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryIndexFile.cpp)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py
  ${ui_source} ${CMAKE_CURRENT_BINARY_DIR} RESULT_VARIABLE extract_result)
if(NOT extract_result EQUAL 0)
  message(FATAL_ERROR "Library UI production extraction failed")
endif()
add_executable(LibraryUiTest LibraryUiTest.cpp ${REPO_ROOT}/test/huge_book_index/HeapCap.cpp ${CMAKE_CURRENT_BINARY_DIR}/LibraryUiProduction.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryText.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(LibraryUiTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/lib/LibraryIndex ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_link_libraries(LibraryUiTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LibraryUiTest)
set(menu_source ${REPO_ROOT}/src/activities/library/LibraryMenuActivity.cpp)
crosspoint_extract_between(menu_methods ${menu_source} "LibraryMenuActivity::LibraryMenuActivity(" "EOF")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/LibraryMenuProduction.cpp "#include \"MenuUiFixture.h\"\n${menu_methods}")
add_executable(LibraryMenuTest LibraryMenuTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/LibraryMenuProduction.cpp)
target_include_directories(LibraryMenuTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(LibraryMenuTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LibraryMenuTest)
