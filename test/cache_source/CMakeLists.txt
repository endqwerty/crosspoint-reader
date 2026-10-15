add_executable(CacheSourceTest CacheSourceTest.cpp ${REPO_ROOT}/lib/Epub/Epub/CacheSource.cpp)
target_include_directories(CacheSourceTest PRIVATE ${REPO_ROOT}/test/library_builder/stubs ${REPO_ROOT}/lib/Epub/Epub)
target_link_libraries(CacheSourceTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CacheSourceTest)
