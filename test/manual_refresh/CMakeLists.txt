# Compile complete production panel methods; keep only hardware/UI plumbing in stubs.
set(PANEL_SOURCE ${REPO_ROOT}/src/activities/util/FrontlightPanelActivity.cpp)
crosspoint_extract_between(exit_methods ${PANEL_SOURCE}
  "void FrontlightPanelActivity::persistLightSettings()" "void FrontlightPanelActivity::onBrightnessEvent(")
crosspoint_extract_between(tile_methods ${PANEL_SOURCE}
  "void FrontlightPanelActivity::onTileEvent(" "bool FrontlightPanelActivity::handleHomeGesture()")
crosspoint_extract_between(loop_method ${PANEL_SOURCE}
  "void FrontlightPanelActivity::loop()" "int FrontlightPanelActivity::computePanelBottom()")
crosspoint_extract_between(render_method ${PANEL_SOURCE} "void FrontlightPanelActivity::render(" "EOF")

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ManualRefreshProduction.cpp
  "#include \"ManualRefreshFixture.h\"\n${exit_methods}\n${tile_methods}\n${loop_method}\n${render_method}\n")

add_executable(ManualRefreshTest ManualRefreshTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/ManualRefreshProduction.cpp)
target_include_directories(ManualRefreshTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ManualRefreshTest PRIVATE GfxRefreshHost GTest::gtest_main)
gtest_discover_tests(ManualRefreshTest)
