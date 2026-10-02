# Compile the production RenderLock implementation without ActivityManager's UI
# and task dependencies. Reconfigure whenever its source changes.
set(RENDER_LOCK_SOURCE ${REPO_ROOT}/src/activities/ActivityManager.cpp)
crosspoint_extract_between(RENDER_LOCK_IMPLEMENTATION ${RENDER_LOCK_SOURCE} "// RenderLock\n" "EOF")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/RenderLockProduction.cpp
  "#include \"RenderLockFixture.h\"\n${RENDER_LOCK_IMPLEMENTATION}")

add_executable(RenderLockTest RenderLockTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/RenderLockProduction.cpp)
target_include_directories(RenderLockTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/src/activities)
target_link_libraries(RenderLockTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(RenderLockTest)

# Exercise the actual main-loop scheduling tail, including existing power/input delays.
set(MAIN_SOURCE ${REPO_ROOT}/src/main.cpp)
crosspoint_extract_between(scheduling_tail ${MAIN_SOURCE} "  bool skipLoopDelay = false;" "EOF")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/MainSchedulingProduction.cpp
  "#include \"RenderLockFixture.h\"\nvoid runMainLoopTail() {\n${scheduling_tail}")
target_sources(RenderLockTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/MainSchedulingProduction.cpp)
