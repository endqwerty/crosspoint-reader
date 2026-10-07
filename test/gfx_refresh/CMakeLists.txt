set(GFX_HOST_SOURCES
  RealFontPage.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
function(configure_gfx_host target)
target_include_directories(${target} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Utf8
)
# Keep production font lookup, shaping, decompression and rasterization. Discard
# unused image/SD/hardware paths rather than providing fake implementations.
target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(${target} INTERFACE -Wl,-dead_strip)
else()
  target_link_options(${target} INTERFACE -Wl,--gc-sections)
endif()
target_link_libraries(${target} PUBLIC crosspoint_test_common)
endfunction()

add_library(GfxRefreshHost STATIC ${GFX_HOST_SOURCES} ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp)
configure_gfx_host(GfxRefreshHost)

# Freeze only the old raster implementation; font lookup, shaping, decompression,
# clipping and fixtures are the same production code in both host executables.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GLYPH_REFERENCE_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/GfxRendererReference.cpp)
add_custom_command(OUTPUT ${GLYPH_REFERENCE_SOURCE}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_glyph_reference.py
    ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp ${CMAKE_CURRENT_SOURCE_DIR}/R11GlyphRaster.inc ${GLYPH_REFERENCE_SOURCE}
  DEPENDS ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp R11GlyphRaster.inc make_glyph_reference.py
  VERBATIM)
add_library(GfxGlyphReferenceHost STATIC ${GFX_HOST_SOURCES} ${GLYPH_REFERENCE_SOURCE})
configure_gfx_host(GfxGlyphReferenceHost)
add_executable(GfxGlyphReferenceProbe GlyphRasterProbe.cpp)
target_link_libraries(GfxGlyphReferenceProbe PRIVATE GfxGlyphReferenceHost)
add_executable(GfxGlyphProbe GlyphRasterProbe.cpp)
target_link_libraries(GfxGlyphProbe PRIVATE GfxRefreshHost)
add_test(NAME GlyphRasterParity COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compare_glyph_rasters.py
  $<TARGET_FILE:GfxGlyphReferenceProbe> $<TARGET_FILE:GfxGlyphProbe>)

add_executable(GfxRefreshTest BitmapOverlayTest.cpp GfxRefreshTest.cpp RealFontPageTest.cpp TextGrayscaleTrackingTest.cpp
  RefreshCommitTest.cpp TextSpacingTest.cpp LibronCoverageTest.cpp)
target_link_libraries(GfxRefreshTest PRIVATE GfxRefreshHost GTest::gtest_main)
gtest_discover_tests(GfxRefreshTest)

option(CROSSPOINT_BUILD_PAGE_BENCHMARK "Build the optional host page-composition benchmark" OFF)
if(CROSSPOINT_BUILD_PAGE_BENCHMARK)
  add_executable(GfxPageBenchmark GfxPageBenchmark.cpp)
  target_link_libraries(GfxPageBenchmark PRIVATE GfxRefreshHost)
endif()
