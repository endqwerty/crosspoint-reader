# Compile the production RenderLock implementation without ActivityManager's UI
# and task dependencies. Reconfigure whenever its source changes.
set(RENDER_LOCK_SOURCE ${REPO_ROOT}/src/activities/ActivityManager.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RENDER_LOCK_SOURCE})
file(READ ${RENDER_LOCK_SOURCE} ACTIVITY_MANAGER_SOURCE)
string(FIND "${ACTIVITY_MANAGER_SOURCE}" "// RenderLock\n" RENDER_LOCK_START REVERSE)
if(RENDER_LOCK_START LESS 0)
  message(FATAL_ERROR "RenderLock implementation marker moved; update the host test extraction")
endif()
string(SUBSTRING "${ACTIVITY_MANAGER_SOURCE}" ${RENDER_LOCK_START} -1 RENDER_LOCK_IMPLEMENTATION)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/RenderLockProduction.cpp
  "#include \"RenderLockFixture.h\"\n${RENDER_LOCK_IMPLEMENTATION}")

add_executable(RenderLockTest RenderLockTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/RenderLockProduction.cpp)
target_include_directories(RenderLockTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/src/activities)
target_link_libraries(RenderLockTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(RenderLockTest)

# Exercise the actual main-loop scheduling tail, including existing power/input delays.
set(MAIN_SOURCE ${REPO_ROOT}/src/main.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MAIN_SOURCE})
file(READ ${MAIN_SOURCE} main_source)
string(FIND "${main_source}" "  bool skipLoopDelay = false;" scheduling_start)
if(scheduling_start LESS 0)
  message(FATAL_ERROR "Main scheduling tail moved")
endif()
string(SUBSTRING "${main_source}" ${scheduling_start} -1 scheduling_tail)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/MainSchedulingProduction.cpp
  "#include \"RenderLockFixture.h\"\nvoid runMainLoopTail() {\n${scheduling_tail}")
target_sources(RenderLockTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/MainSchedulingProduction.cpp)
