set(FREEINK_FONT_ROOT ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont)
file(GLOB FREEINK_FREETYPE_SOURCES CONFIGURE_DEPENDS ${FREEINK_FONT_ROOT}/src/freetype/*.c)
add_library(TtfAdapterFontHost STATIC
  ${FREEINK_FONT_ROOT}/src/FtFont.cpp
  ${FREEINK_FONT_ROOT}/src/FontAlloc.c
  ${FREEINK_FONT_ROOT}/src/Gsub.cpp
  ${FREEINK_FONT_ROOT}/src/Gpos.cpp
  ${FREEINK_FREETYPE_SOURCES})
target_include_directories(TtfAdapterFontHost PUBLIC
  ${FREEINK_FONT_ROOT}/include
  ${FREEINK_FONT_ROOT}/third_party/freetype/include
  ${FREEINK_FONT_ROOT}/third_party/stb)
target_compile_definitions(TtfAdapterFontHost PUBLIC FREEINK_FONT_ENABLE_AUTOHINT=1)

add_executable(TtfEpdFontTest TtfEpdFontTest.cpp
  ${REPO_ROOT}/lib/EpdFont/TtfEpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(TtfEpdFontTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/gfx_refresh/stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8)
target_compile_definitions(TtfEpdFontTest PRIVATE BOARD_HAS_PSRAM=1
  TTF_FIXTURE_PATH="${REPO_ROOT}/freeink-sdk/libs/book/FreeInkBook/test/fixtures/fonts/DejaVuSans.ttf")
target_link_libraries(TtfEpdFontTest PRIVATE TtfAdapterFontHost crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(TtfEpdFontTest)

add_executable(VectorFontGateTest VectorFontGateTest.cpp)
target_include_directories(VectorFontGateTest PRIVATE ${REPO_ROOT}/lib/EpdFont)
target_link_libraries(VectorFontGateTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(VectorFontGateTest)
