add_executable(EpubSearchTest
  EpubSearchTest.cpp
  ${REPO_ROOT}/lib/EpubSearch/EpubSearch.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
)
target_include_directories(EpubSearchTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/EpubSearch
  ${REPO_ROOT}/lib/Epub
)
target_link_libraries(EpubSearchTest PRIVATE FirmwareExpat GTest::gtest_main)
gtest_discover_tests(EpubSearchTest)

set(INPUT_SOURCE ${REPO_ROOT}/src/MappedInputManager.cpp)
crosspoint_extract_between(input_update ${INPUT_SOURCE}
  "void MappedInputManager::update(" "bool MappedInputManager::isNavDirectionSwapped()")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SearchInputProduction.cpp
  "#include \"TestUi.h\"\n${input_update}\n")

add_executable(EpubSearchActivityTest
  EpubSearchActivityTest.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/SearchInputProduction.cpp
  ${REPO_ROOT}/src/activities/reader/EpubSearchActivity.cpp
  ${REPO_ROOT}/lib/EpubSearch/EpubSearch.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
)
target_include_directories(EpubSearchActivityTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/activity_stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/EpubSearch
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/src
)
target_link_libraries(EpubSearchActivityTest PRIVATE FirmwareExpat GTest::gtest_main)
gtest_discover_tests(EpubSearchActivityTest)
