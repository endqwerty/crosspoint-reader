# Compile the real renderer with this target's instrumented HalFile consistently
# in every translation unit; linking a renderer built with a different HalFile
# definition would violate the one-definition rule.
set(IMAGE_BLOCK_SOURCE ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${IMAGE_BLOCK_SOURCE})
file(READ ${IMAGE_BLOCK_SOURCE} IMAGE_BLOCK_IMPLEMENTATION)
string(FIND "${IMAGE_BLOCK_IMPLEMENTATION}" "ImageBlock::ImageBlock(" image_constructor_start)
string(FIND "${IMAGE_BLOCK_IMPLEMENTATION}" "void* ImageBlock::extractCtx" image_constructor_end)
string(FIND "${IMAGE_BLOCK_IMPLEMENTATION}" "bool ImageBlock::serialize(" image_serialization_start)
if(image_constructor_start LESS 0 OR image_constructor_end LESS image_constructor_start OR image_serialization_start LESS 0)
  message(FATAL_ERROR "Image persistence extraction boundaries moved")
endif()
math(EXPR image_constructor_size "${image_constructor_end} - ${image_constructor_start}")
string(SUBSTRING "${IMAGE_BLOCK_IMPLEMENTATION}" ${image_constructor_start} ${image_constructor_size} image_constructor)
string(SUBSTRING "${IMAGE_BLOCK_IMPLEMENTATION}" ${image_serialization_start} -1 image_serialization)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ImagePersistenceProduction.cpp
  "#include <Epub/blocks/ImageBlock.h>\n#include <Memory.h>\n#include <Logging.h>\n#include <Serialization.h>\n"
  "${image_constructor}\n${image_serialization}\n")

add_library(EpubPageTurnHost STATIC
  PageTurnFixture.cpp
  ImageBlockUnused.cpp
  HostAllocations.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c
  ${CMAKE_CURRENT_BINARY_DIR}/ImagePersistenceProduction.cpp
  ${REPO_ROOT}/test/gfx_refresh/RealFontPage.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/SectionPageReader.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
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
target_include_directories(EpubPageTurnHost PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/gfx_refresh
  ${REPO_ROOT}/test/gfx_refresh/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Utf8
)
target_compile_options(EpubPageTurnHost PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(EpubPageTurnHost INTERFACE -Wl,-dead_strip)
else()
  target_link_options(EpubPageTurnHost INTERFACE -Wl,--gc-sections)
endif()
target_link_libraries(EpubPageTurnHost PUBLIC crosspoint_test_common)

add_executable(EpubPageTurnTest EpubPageTurnTest.cpp CacheBudgetTest.cpp PersistenceFaultTest.cpp)
target_link_libraries(EpubPageTurnTest PRIVATE EpubPageTurnHost GTest::gtest_main)
gtest_discover_tests(EpubPageTurnTest)

option(CROSSPOINT_BUILD_EPUB_PAGE_BENCHMARK "Build the optional cached EPUB page-turn benchmark" OFF)
if(CROSSPOINT_BUILD_EPUB_PAGE_BENCHMARK)
  add_executable(EpubPageTurnBenchmark EpubPageTurnBenchmark.cpp)
  target_link_libraries(EpubPageTurnBenchmark PRIVATE EpubPageTurnHost)
endif()
