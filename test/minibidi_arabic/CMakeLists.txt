# Instrument only calls from BidiUtils, leaving the shaping implementation intact.
add_library(MiniBidiProbeReference OBJECT ${REPO_ROOT}/lib/MiniBidi/minibidi.c)
target_compile_definitions(MiniBidiProbeReference PRIVATE bidi_class=reference_bidi_class)
target_link_libraries(MiniBidiProbeReference PRIVATE crosspoint_test_common)

add_executable(MiniBidiArabicTest
  MiniBidiArabicTest.cpp
  DirectionProbeTest.cpp
  $<TARGET_OBJECTS:MiniBidiProbeReference>
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(MiniBidiArabicTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs  # no-op Logging.h (real one needs Arduino)
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(MiniBidiArabicTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MiniBidiArabicTest)
