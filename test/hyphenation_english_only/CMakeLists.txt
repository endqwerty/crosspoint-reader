add_executable(HyphenationEnglishOnlyTest
  HyphenationEnglishOnlyTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(HyphenationEnglishOnlyTest PRIVATE ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Utf8)
target_compile_definitions(HyphenationEnglishOnlyTest PRIVATE CROSSPOINT_HYPHENATION_ENGLISH_ONLY)
target_link_libraries(HyphenationEnglishOnlyTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HyphenationEnglishOnlyTest)
