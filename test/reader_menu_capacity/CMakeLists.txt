# Compile the production reader menu list and check that every optional entry fits the row storage.
set(menu_header ${REPO_ROOT}/src/activities/reader/EpubReaderMenuActivity.h)
set(menu_source ${REPO_ROOT}/src/activities/reader/EpubReaderMenuActivity.cpp)
crosspoint_extract_between(menu_actions ${menu_header} "  enum class MenuAction {" "  struct MenuItem {")
crosspoint_extract_between(menu_capacity ${menu_header} "  static constexpr size_t MAX_MENU_ITEMS" "\n  freeink::ui::ListItem")
crosspoint_extract_between(menu_items ${menu_source} "void EpubReaderMenuActivity::buildMenuItems("
  "void EpubReaderMenuActivity::toggleBluetooth()")
string(REGEX MATCHALL "StrId::STR_[A-Z0-9_]+" str_ids "${menu_items}")
list(REMOVE_DUPLICATES str_ids)
list(TRANSFORM str_ids REPLACE "StrId::" "")
list(JOIN str_ids ", " str_list)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/MenuItemsProduction.h
  "#pragma once\n#include \"MenuFixture.h\"\nenum class StrId { ${str_list} };\n"
  "struct EpubReaderMenuActivity {\n${menu_actions}  struct MenuItem {\n    MenuAction action;\n    StrId labelId;\n  };\n"
  "${menu_capacity}\n  static void buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks,\n"
  "                             bool hasClippings);\n};\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/MenuItemsProduction.cpp "#include \"MenuItemsProduction.h\"\n${menu_items}")
add_executable(ReaderMenuCapacityTest ReaderMenuCapacityTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/MenuItemsProduction.cpp)
target_include_directories(ReaderMenuCapacityTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ReaderMenuCapacityTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ReaderMenuCapacityTest)
