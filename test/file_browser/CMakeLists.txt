find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(browser_source ${REPO_ROOT}/src/activities/home/FileBrowserActivity.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${browser_source} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py
  ${browser_source} ${CMAKE_CURRENT_BINARY_DIR} RESULT_VARIABLE extract_result)
if(NOT extract_result EQUAL 0)
  message(FATAL_ERROR "File Browser production extraction failed")
endif()
add_executable(FileBrowserTest FileBrowserTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/FileBrowserProduction.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryBookState.cpp ${REPO_ROOT}/lib/LibraryIndex/LibraryText.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/src/util/BookmarkUtil.cpp ${REPO_ROOT}/src/util/BookStateMove.cpp
  ${REPO_ROOT}/src/ClippingStore.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(FileBrowserTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/lib/FsHelpers ${REPO_ROOT}/test/fs_helpers/stubs ${REPO_ROOT}/test/library_builder/stubs
  ${REPO_ROOT}/lib/LibraryIndex ${REPO_ROOT}/lib/FolderSearch ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/src
  ${REPO_ROOT}/test/clipping_store/stubs ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_link_libraries(FileBrowserTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FileBrowserTest)
