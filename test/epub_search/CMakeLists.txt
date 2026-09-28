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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT_SOURCE})
file(READ ${INPUT_SOURCE} INPUT_IMPLEMENTATION)
string(FIND "${INPUT_IMPLEMENTATION}" "void MappedInputManager::update(" input_start)
string(FIND "${INPUT_IMPLEMENTATION}" "bool MappedInputManager::isNavDirectionSwapped()" input_end)
if(input_start LESS 0 OR input_end LESS_EQUAL input_start)
  message(FATAL_ERROR "Search input-pump extraction boundaries moved")
endif()
math(EXPR input_length "${input_end} - ${input_start}")
string(SUBSTRING "${INPUT_IMPLEMENTATION}" ${input_start} ${input_length} input_update)
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
