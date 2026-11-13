add_executable(KOReaderDocumentIdTest KOReaderDocumentIdTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderDocumentId.cpp)
# The stubs directory comes first so its HalStorage.h, Logging.h and MD5Builder.h
# replace the firmware headers.
target_include_directories(KOReaderDocumentIdTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_include_directories(KOReaderDocumentIdTest PRIVATE ${REPO_ROOT}/lib/KOReaderSync)
target_link_libraries(KOReaderDocumentIdTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KOReaderDocumentIdTest)
