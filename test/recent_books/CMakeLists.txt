# Exercise the production store, JSON serializer and atomic writer together.
add_executable(RecentBooksStoreTest RecentBooksStoreTest.cpp
  ${REPO_ROOT}/src/RecentBooksStore.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/lib/Serialization/AtomicFile.cpp)
target_include_directories(RecentBooksStoreTest PRIVATE
  ${REPO_ROOT}/test/bookmark_persistence/stubs
  ${REPO_ROOT}/test/atomic_file/stubs
  ${REPO_ROOT}/test/library_builder/stubs
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/Serialization)
target_compile_definitions(RecentBooksStoreTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  ARDUINOJSON_ENABLE_ARDUINO_STREAM=0 ARDUINOJSON_ENABLE_ARDUINO_PRINT=0 ARDUINOJSON_ENABLE_PROGMEM=0)
target_link_libraries(RecentBooksStoreTest PRIVATE crosspoint_test_common ArduinoJson GTest::gtest_main)
gtest_discover_tests(RecentBooksStoreTest)
