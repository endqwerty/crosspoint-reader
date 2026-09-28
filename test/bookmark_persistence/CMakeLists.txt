if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
# Match the firmware's platformio.ini dependency, and verify the source archive.
FetchContent_Declare(crosspoint_arduinojson
  URL https://codeload.github.com/bblanchon/ArduinoJson/tar.gz/refs/tags/v7.4.2
  URL_HASH SHA256=681f703dd237f5b7f1dc1d7009a9cf246e88676b349572e73eae9154e8994a55
)
FetchContent_MakeAvailable(crosspoint_arduinojson)
add_executable(BookmarkPersistenceTest BookmarkPersistenceTest.cpp
  ${REPO_ROOT}/src/util/BookmarkFile.cpp ${REPO_ROOT}/src/util/BookmarkUtil.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp ${REPO_ROOT}/lib/Serialization/AtomicFile.cpp)
target_include_directories(BookmarkPersistenceTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/atomic_file/stubs ${REPO_ROOT}/test/library_builder/stubs
  ${REPO_ROOT}/src ${REPO_ROOT}/src/util ${REPO_ROOT}/lib/Serialization)
target_compile_definitions(BookmarkPersistenceTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  ARDUINOJSON_ENABLE_ARDUINO_STREAM=0 ARDUINOJSON_ENABLE_ARDUINO_PRINT=0 ARDUINOJSON_ENABLE_PROGMEM=0)
target_link_libraries(BookmarkPersistenceTest PRIVATE crosspoint_test_common ArduinoJson GTest::gtest_main)
gtest_discover_tests(BookmarkPersistenceTest)
