set(READER_SOURCE ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp)
crosspoint_extract_between(page_attempt ${READER_SOURCE} "  {\n    auto p = section->loadPage(section->currentPage);" "\n  showPendingSyncSaveError();")
crosspoint_extract_between(offset_selection ${READER_SOURCE} "    const bool explicitOffsetJump = pendingOffsetJump.has_value();" "    if (!cacheComplete) {")
crosspoint_extract_between(resume_position ${READER_SOURCE} "    if (pendingPageJump.has_value()) {" "\n  if (section->isPartial() && section->currentPage")
crosspoint_extract_between(deferred_position ${READER_SOURCE} "bool EpubReaderActivity::applyDeferredReposition()" "bool EpubReaderActivity::saveProgress(")
crosspoint_extract_between(position_info ${READER_SOURCE} "ChapterPosition EpubReaderActivity::chapterPosition() const" "int EpubReaderActivity::bookPercentFor(")
crosspoint_extract_between(pending_jump ${READER_SOURCE} "bool EpubReaderActivity::resolvePendingSectionJump()" "bool EpubReaderActivity::backgroundBuildWanted()")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ReaderRecoveryProduction.cpp
  "#include \"ReaderRecoveryFixture.h\"\n${deferred_position}\n${position_info}\n${pending_jump}\n"
  "void EpubReaderActivity::renderAttempt() {\n${page_attempt}\n}\n"
  "void EpubReaderActivity::resumeSection() {\n section = std::make_unique<RecoverySection>(storage);\n {\n${offset_selection}\n${resume_position}\napplyDeferredReposition();\n}\n")
add_executable(ReaderPageRecoveryTest ReaderPageRecoveryTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/ReaderRecoveryProduction.cpp)
target_include_directories(ReaderPageRecoveryTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/src/activities/reader)
target_link_libraries(ReaderPageRecoveryTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ReaderPageRecoveryTest)
