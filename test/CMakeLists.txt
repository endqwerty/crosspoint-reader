cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(CROSSPOINT_TEST_SANITIZERS "Instrument host tests with address and undefined-behavior checks" OFF)
if(CROSSPOINT_TEST_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "CROSSPOINT_TEST_SANITIZERS requires GCC or Clang")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

enable_testing()
include(GoogleTest)
include(cmake/FirmwareExpat.cmake)
include(cmake/ProductionExtract.cmake)
add_subdirectory(expat_streaming)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(html_to_plain_text)
add_subdirectory(library_text)
add_subdirectory(library_format)
add_subdirectory(library_index_file)
add_subdirectory(library_builder)
add_subdirectory(library_follow_ons)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(credential_integrity)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(koreader_xpath_resolver)
add_subdirectory(progress_comparison)
add_subdirectory(font_cache_manager)
add_subdirectory(css_parser)
add_subdirectory(chapter_html_slim_parser)
add_subdirectory(korean_line_breaking)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)
add_subdirectory(content_opf_parser)
add_subdirectory(chapter_position)
add_subdirectory(reference_pages)
add_subdirectory(home_button)
add_subdirectory(button_press_navigation)

add_subdirectory(inflate_stream)
add_subdirectory(fs_helpers)
add_subdirectory(txt_to_html)

add_subdirectory(absolute_grayscale)


add_subdirectory(folder_search)
add_subdirectory(sd_card_font)
add_subdirectory(gfx_refresh)
add_subdirectory(glyph_bitmap)

add_subdirectory(reader_session)
add_subdirectory(plugin_manifest_classification)
add_subdirectory(protected_paths)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME TrustedTimeTimezone COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_trusted_time.py)

add_subdirectory(boot_splash)
add_subdirectory(manual_refresh)
add_subdirectory(epd_bus)
add_subdirectory(reader_grayscale_plan)
add_subdirectory(refresh_sequences)
add_subdirectory(reader_progress_state)
add_subdirectory(render_lock)
add_subdirectory(epub_page_turn)
add_subdirectory(section_persistence)
add_subdirectory(section_parser_integration)
add_subdirectory(reader_navigation)
add_subdirectory(reader_incremental)
add_subdirectory(reader_page_recovery)
add_subdirectory(reader_overlay)

add_subdirectory(reader_link_navigation)

add_subdirectory(epub_search)

add_subdirectory(library_book_state)

add_subdirectory(library_ui)

add_subdirectory(reader_search_handoff)

add_subdirectory(atomic_file)
add_subdirectory(bookmark_actions)

add_subdirectory(bookmark_persistence)
add_subdirectory(recent_books)
add_subdirectory(calibre_sync)
add_subdirectory(settings_migration)
add_subdirectory(text_truncation)
add_subdirectory(sleep_grayscale)
add_subdirectory(hal_storage)
add_subdirectory(file_browser)
add_subdirectory(ttf_epd_font)
add_subdirectory(slider_input)
add_subdirectory(sleep_quick_resume)
add_subdirectory(book_metadata_cache)

add_subdirectory(library_details)

add_subdirectory(button_navigator)

add_subdirectory(reader_lifecycle)
add_subdirectory(reader_font_sizes)

add_subdirectory(chunked_vector)
add_subdirectory(huge_book_index)

add_subdirectory(zip_file)

add_subdirectory(sdfat_cache)
add_subdirectory(epub_load)

add_subdirectory(epub_indexing)
