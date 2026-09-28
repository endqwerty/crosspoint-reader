#include <gtest/gtest.h>

#include "SectionPersistenceFixture.h"

namespace {
using namespace epub_page_test;

class SectionPersistence : public ::testing::Test {
 protected:
  std::unique_ptr<Section> section;
  std::vector<uint8_t> previousCache;

  void SetUp() override {
    faults = {};
    io = {};
    for (auto& bytes : files) bytes.clear();
    fileExists = {false, true, false};
    beginBuild();
  }
  void TearDown() override {
    section.reset();
    faults = {};
    io = {};
    fileExists = {true, true, false};
  }
  void beginBuild() {
    section = std::make_unique<Section>();
    section->build_->lut.reserve(3);
    section->build_->parser->anchors.reserve(2);
    files[2].assign(SectionPageReader::HEADER_SIZE, 0);
    files[2][0] = section_test::incompleteVersion();
    files[2].reserve(2048);
    fileExists[2] = true;
    section->file.open(files[2]);
    ASSERT_TRUE(section->file.seek(SectionPageReader::HEADER_SIZE));
    faults = {};
    io = {};
  }
  static std::unique_ptr<Page> rulePage(uint16_t width = 100) {
    auto page = std::make_unique<Page>();
    page->elements.reserve(1);
    page->elements.push_back(std::make_unique<PageHorizontalRule>(width, 1, 17, 18));
    return page;
  }
  void appendRule(uint16_t width = 100) {
    const auto offset = section->onPageComplete(rulePage(width));
    ASSERT_NE(0u, offset);
    section->build_->lut.push_back({offset, 2, 3, 4093});
  }
  void installPreviousCache() {
    beginBuild();
    appendRule(41);
    ASSERT_TRUE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
    ASSERT_NE(nullptr, SectionPageReader::load("section", 0));
    previousCache = files[0];
    beginBuild();
    appendRule(101);
    io = {};
  }
  void expectPreviousCacheIntact() {
    EXPECT_TRUE(fileExists[0]);
    EXPECT_EQ(previousCache, files[0]);
  }
  template <typename T>
  static T readValue(const std::vector<uint8_t>& bytes, size_t offset) {
    T value{};
    EXPECT_LE(offset + sizeof(value), bytes.size());
    if (offset + sizeof(value) <= bytes.size()) std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
  }
};

TEST_F(SectionPersistence, CompleteCommitPublishesReadablePagesAndAllLookupTables) {
  appendRule();
  section->build_->parser->anchors.emplace_back("start", 0);
  ASSERT_TRUE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_FALSE(static_cast<bool>(section->file));
  EXPECT_FALSE(fileExists[2]);
  ASSERT_TRUE(fileExists[0]);
  EXPECT_EQ(section_test::completeVersion(), files[0][0]);
  const auto countOffset = SectionPageReader::HEADER_SIZE - 5 * sizeof(uint32_t) - sizeof(uint16_t);
  EXPECT_EQ(1u, readValue<uint16_t>(files[0], countOffset));
  auto page = SectionPageReader::load("section", 0);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(4093u, page->visibleTextOffset);
  ASSERT_EQ(1u, page->elements.size());
  EXPECT_EQ(TAG_PageHorizontalRule, page->elements[0]->getTag());
  const auto paragraphOffset = readValue<uint32_t>(files[0], SectionPageReader::HEADER_SIZE - 3 * sizeof(uint32_t));
  EXPECT_EQ(1u, readValue<uint16_t>(files[0], paragraphOffset));
  EXPECT_EQ(2u, readValue<uint16_t>(files[0], paragraphOffset + sizeof(uint16_t)));
  const auto listOffset = readValue<uint32_t>(files[0], SectionPageReader::HEADER_SIZE - 2 * sizeof(uint32_t));
  EXPECT_EQ(3u, readValue<uint16_t>(files[0], listOffset));
}

TEST_F(SectionPersistence, PartialCommitKeepsOnlyCompletedPageAnchorsAndWatermark) {
  appendRule();
  section->build_->parser->anchors.emplace_back("kept", 0);
  section->build_->parser->anchors.emplace_back("trailing", 1);
  ASSERT_TRUE(section->commitBuildFile(section_test::partialVersion(), 700, 1000));
  ASSERT_TRUE(fileExists[0]);
  EXPECT_EQ(section_test::partialVersion(), files[0][0]);
  const auto anchorOffset = readValue<uint32_t>(files[0], SectionPageReader::HEADER_SIZE - 4 * sizeof(uint32_t));
  EXPECT_EQ(1u, readValue<uint16_t>(files[0], anchorOffset));
  const auto visibleOffset = readValue<uint32_t>(files[0], SectionPageReader::HEADER_SIZE - sizeof(uint32_t));
  EXPECT_EQ(4093u, readValue<uint32_t>(files[0], visibleOffset));
  EXPECT_EQ(700u, readValue<uint32_t>(files[0], visibleOffset + sizeof(uint32_t)));
  EXPECT_EQ(1000u, readValue<uint32_t>(files[0], visibleOffset + 2 * sizeof(uint32_t)));
  EXPECT_NE(nullptr, SectionPageReader::load("section", 0));
}

TEST_F(SectionPersistence, InvalidPageOffsetFailsBeforeReplacingPreviousCache) {
  installPreviousCache();
  section->build_->lut[0].fileOffset = 0;
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  expectPreviousCacheIntact();
  EXPECT_FALSE(fileExists[2]);
}

TEST_F(SectionPersistence, CheckedPageFailureDoesNotAdvanceCountOrPublishCache) {
  installPreviousCache();
  const std::vector<std::string> words{"alpha"};
  const std::vector<int16_t> positions{0};
  const std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR};
  auto block = std::make_unique<TextBlock>(words, positions, styles, std::vector<uint8_t>{}, std::vector<uint16_t>{});
  auto page = std::make_unique<Page>();
  page->elements.reserve(1);
  page->elements.push_back(std::make_unique<PageLine>(std::move(block), 10, 20));
  const auto count = section->builtPageCount_;
  faults.writeBudget = 0;
  const auto offset = section->onPageComplete(std::move(page));
  EXPECT_EQ(0u, offset);
  EXPECT_GT(io.injectedFailures, 0u);
  EXPECT_EQ(count, section->builtPageCount_);
  EXPECT_EQ(count, section->pageCount);
  section->build_->lut.push_back({offset, 4, 5, 6000});
  faults = {};
  EXPECT_FALSE(section->commitBuildFile(section_test::partialVersion(), 700, 1000));
  expectPreviousCacheIntact();
}

TEST_F(SectionPersistence, ShortFooterWritesMustNotPublishOrReplacePreviousCache) {
  installPreviousCache();
  faults.writeBudget = 0;
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_GT(io.injectedFailures, 0u);
  expectPreviousCacheIntact();
}

TEST_F(SectionPersistence, FailedPageOffsetWriteMustNotStampCommittedVersion) {
  installPreviousCache();
  faults.failWriteCall = 1;
  EXPECT_FALSE(section->commitBuildFile(section_test::partialVersion(), 700, 1000));
  EXPECT_GT(io.injectedFailures, 0u);
  expectPreviousCacheIntact();
}

TEST_F(SectionPersistence, FailedHeaderSeekMustNotPublishOrReplacePreviousCache) {
  installPreviousCache();
  faults.failSeekCall = 1;
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_GT(io.injectedFailures, 0u);
  expectPreviousCacheIntact();
}

TEST_F(SectionPersistence, FailedVersionSeekMustNotPublishOrReplacePreviousCache) {
  installPreviousCache();
  faults.failSeekCall = 2;
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_GT(io.injectedFailures, 0u);
  expectPreviousCacheIntact();
}

TEST_F(SectionPersistence, FailedRenamePreservesPreviouslyReadableCache) {
  installPreviousCache();
  faults.failRename = true;
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_GT(io.injectedFailures, 0u);
  expectPreviousCacheIntact();
}

TEST_F(SectionPersistence, FailedRemoveDoesNotOverwriteExistingDestination) {
  installPreviousCache();
  // Recovery must not discard either copy if it cannot remove the interrupted live name.
  files[3] = previousCache;
  fileExists[3] = true;
  faults.failRemove = true;
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_GT(io.injectedFailures, 0u);
  expectPreviousCacheIntact();
}

TEST_F(SectionPersistence, ReadingBuiltPageRestoresAppendCursor) {
  appendRule(100);
  appendRule(200);
  const auto position = section->file.position();
  auto page = section->loadPageDuringBuild(0);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(4093u, page->visibleTextOffset);
  EXPECT_EQ(position, section->file.position());
}

TEST_F(SectionPersistence, FailedCursorRestoreCannotLeaveBuildWritableAtEarlierPage) {
  appendRule(100);
  appendRule(200);
  const auto position = section->file.position();
  io = {};
  faults.failSeekCall = 2;
  EXPECT_EQ(nullptr, section->loadPageDuringBuild(0));
  EXPECT_EQ(1u, io.injectedFailures);
  EXPECT_TRUE(!section->file || section->file.position() == position);
}

TEST_F(SectionPersistence, FailedPageCallbackStopsParserAndDoesNotAddLookupEntry) {
  const auto count = section->build_->lut.size();
  faults.writeBudget = 0;
  section->appendPage(rulePage(), 1, 2, 3);
  EXPECT_TRUE(section->build_->ioFailed);
  EXPECT_TRUE(section->build_->parser->hasError());
  EXPECT_EQ(count, section->build_->lut.size());
  faults = {};
  EXPECT_EQ(0u, section->onPageComplete(rulePage()));
}

TEST_F(SectionPersistence, NullPageCannotAdvanceOrPublishBuild) {
  section->appendPage(nullptr, 1, 2, 3);
  EXPECT_EQ(0u, section->builtPageCount_);
  EXPECT_TRUE(section->build_->parser->hasError());
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_FALSE(fileExists[0]);
}

TEST_F(SectionPersistence, EveryFooterAndHeaderWriteFailurePreservesPreviousCache) {
  for (bool partial : {false, true}) {
    installPreviousCache();
    section->build_->parser->anchors.emplace_back("start", 0);
    const auto version = partial ? section_test::partialVersion() : section_test::completeVersion();
    ASSERT_TRUE(section->commitBuildFile(version, 700, 1000));
    const auto writes = io.writes;
    for (size_t ordinal = 1; ordinal <= writes; ++ordinal) {
      SCOPED_TRACE(ordinal);
      installPreviousCache();
      section->build_->parser->anchors.emplace_back("start", 0);
      faults.failWriteCall = ordinal;
      EXPECT_FALSE(section->commitBuildFile(version, 700, 1000));
      EXPECT_EQ(1u, io.injectedFailures);
      expectPreviousCacheIntact();
      EXPECT_FALSE(fileExists[2]);
      faults = {};
    }
  }
}

TEST_F(SectionPersistence, FailedCloseDoesNotInstallUnflushedCache) {
  installPreviousCache();
  faults.failCloseCall = 1;
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_EQ(1u, io.injectedFailures);
  expectPreviousCacheIntact();
  EXPECT_FALSE(fileExists[2]);
}

TEST_F(SectionPersistence, FailedInstallRestoresPreviousCache) {
  installPreviousCache();
  faults.failRenameCall = 2;
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_EQ(1u, io.injectedFailures);
  expectPreviousCacheIntact();
  EXPECT_FALSE(fileExists[3]);
}

TEST_F(SectionPersistence, FailedInstallAndRollbackRetainRecoverablePreviousCache) {
  installPreviousCache();
  faults.failRenameCall = 2;
  faults.failRenameCall2 = 3;
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_EQ(2u, io.injectedFailures);
  ASSERT_TRUE(fileExists[3]);
  EXPECT_EQ(previousCache, files[3]);
  EXPECT_FALSE(fileExists[0]);
  EXPECT_FALSE(fileExists[2]);
  faults = {};
  ASSERT_TRUE(section->recoverBuildBackup());
  expectPreviousCacheIntact();
  EXPECT_NE(nullptr, SectionPageReader::load("section", 0));
  EXPECT_FALSE(fileExists[3]);
}

TEST_F(SectionPersistence, InterruptedBackupCleanupRollsBackToReadablePreviousCache) {
  installPreviousCache();
  faults.failRemoveCall = 1;
  ASSERT_TRUE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_EQ(1u, io.injectedFailures);
  ASSERT_TRUE(fileExists[3]);
  EXPECT_EQ(previousCache, files[3]);
  EXPECT_NE(previousCache, files[0]);
  faults = {};
  ASSERT_TRUE(section->recoverBuildBackup());
  expectPreviousCacheIntact();
}

TEST_F(SectionPersistence, FailedRecoveryRenameKeepsBackupForRetry) {
  installPreviousCache();
  files[3] = previousCache;
  fileExists[3] = true;
  faults.failRename = true;
  EXPECT_FALSE(section->recoverBuildBackup());
  EXPECT_EQ(previousCache, files[3]);
  EXPECT_TRUE(fileExists[3]);
  faults = {};
  ASSERT_TRUE(section->recoverBuildBackup());
  expectPreviousCacheIntact();
}

TEST_F(SectionPersistence, ReadSeekFailureClosesWriterAndRejectsLaterCommit) {
  appendRule();
  io = {};
  faults.failSeekCall = 1;
  EXPECT_EQ(nullptr, section->loadPageDuringBuild(0));
  EXPECT_EQ(1u, io.injectedFailures);
  EXPECT_FALSE(static_cast<bool>(section->file));
  faults = {};
  EXPECT_EQ(0u, section->onPageComplete(rulePage()));
  EXPECT_FALSE(section->commitBuildFile(section_test::completeVersion(), 0, 0));
  EXPECT_FALSE(fileExists[0]);
}
TEST_F(SectionPersistence, NavigationLookupsRoundTripCommittedAndPartialCaches) {
  for (bool partial : {false, true}) {
    beginBuild();
    appendRule();
    appendRule();
    section->build_->lut[1].paragraphIndex = 8;
    section->build_->lut[1].listItemIndex = 12;
    section->build_->parser->anchors = {{"first", 0}, {"second", 1}};
    if (partial) section->build_->parser->anchors.push_back({"future", 2});
    ASSERT_TRUE(section->commitBuildFile(partial ? section_test::partialVersion() : section_test::completeVersion(),
                                         partial ? 100 : 0, partial ? 200 : 0));
    EXPECT_EQ(partial ? std::nullopt : std::optional<uint16_t>(2), section->getCachedPageCount());
    EXPECT_EQ(0, section->getPageForAnchor("first"));
    EXPECT_EQ(1, section->getPageForAnchor("second"));
    EXPECT_EQ(std::nullopt, section->getPageForAnchor("future"));
    EXPECT_EQ(1, section->getPageForParagraphIndex(8));
    EXPECT_EQ(8, section->getParagraphIndexForPage(1));
    EXPECT_EQ(1, section->getPageForListItemIndex(12));
    EXPECT_NE(nullptr, SectionPageReader::load("section", 1));
  }
}
}  // namespace
