set(BOOT_SOURCE ${REPO_ROOT}/src/activities/boot_sleep/BootActivity.cpp)
crosspoint_extract_between(boot_method ${BOOT_SOURCE} "void BootActivity::onEnter()" "EOF")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BootSplashProduction.cpp
  "#include \"BootSplashFixture.h\"\n${boot_method}\n")
add_executable(BootSplashTest BootSplashTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/BootSplashProduction.cpp)
target_include_directories(BootSplashTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/src)
target_link_libraries(BootSplashTest PRIVATE GfxRefreshHost GTest::gtest_main)
gtest_discover_tests(BootSplashTest)
