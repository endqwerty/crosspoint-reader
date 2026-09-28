#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "ScopedAllocationFailure.h"
#include "SectionIntegrationFixture.h"

namespace {
using Fault = parser_test::ScopedAllocationFailure;

class SectionParserIntegration : public ::testing::Test {
 protected:
  std::string directory;
  std::string html;
  GfxRenderer renderer;
  ReaderRenderSpec spec;
  std::unique_ptr<Section> section;
  std::vector<uint8_t> previous;

  void SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "crosspoint-section-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    html = directory + "/chapter.html";
    spec.viewportWidth = 480;
    spec.viewportHeight = 800;
    spec.embeddedStyle = false;
  }
  void TearDown() override {
    section.reset();
    EXPECT_EQ(0u, parser_test::openFileCount);
    std::filesystem::remove_all(directory);
  }
  std::vector<uint8_t> readCache() const {
    std::ifstream input(directory + "/section", std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
  void prepare(const char* text) {
    section.reset();
    {
      std::ofstream output(html);
      output << text;
    }
    section = std::make_unique<Section>();
    section->filePath = directory + "/section";
    section->build_ = std::make_unique<Section::BuildContext>();
    section->build_->parsePath = html;
    section->build_->lut.reserve(16);
    ASSERT_TRUE(section->file.open(section->binTmpPath().c_str(), "wb+"));
    ASSERT_TRUE(section->writeSectionFileHeader(spec));
    Section* target = section.get();
    auto& context = *section->build_;
    context.parser = std::make_unique<ChapterHtmlSlimParser>(
        nullptr, context.parsePath, renderer, spec.fontId, spec.lineCompression, spec.extraParagraphSpacing,
        spec.paragraphAlignment, spec.viewportWidth, spec.viewportHeight, spec.hyphenationEnabled,
        spec.focusReadingEnabled,
        [target](std::unique_ptr<Page> page, uint16_t paragraph, uint16_t item, uint32_t offset) {
          target->appendPage(std::move(page), paragraph, item, offset);
        },
        false, context.contentBase, context.imageBasePath);
    context.parser->setTextSpacing(spec.characterSpacing, spec.wordSpacingPercent);
    ASSERT_TRUE(context.parser->beginParse());
    context.totalBytes = context.parser->parseTotalBytes();
  }
  void commitPrevious() {
    prepare("<html><body><p>previous words</p></body></html>");
    ASSERT_TRUE(section->buildSomeMore(0));
    previous = readCache();
    ASSERT_FALSE(previous.empty());
    ASSERT_NE(nullptr, SectionPageReader::load(section->filePath, 0));
  }
  void expectFailedBuild() {
    EXPECT_FALSE(section->buildComplete_);
    EXPECT_EQ(nullptr, section->build_);
    EXPECT_FALSE(Storage.exists(section->binTmpPath().c_str()));
    EXPECT_EQ(previous, readCache());
    EXPECT_NE(nullptr, SectionPageReader::load(section->filePath, 0));
  }
};

TEST_F(SectionParserIntegration, SuccessfulBuildRoundTripsEveryVisibleWord) {
  prepare("<html><body><p>first words</p><p>last words</p></body></html>");
  ASSERT_TRUE(section->buildSomeMore(0));
  ASSERT_TRUE(section->buildComplete_);
  ASSERT_EQ(nullptr, section->build_);
  ASSERT_TRUE(section->loadSectionFile(spec));
  auto page = SectionPageReader::load(section->filePath, 0);
  ASSERT_NE(nullptr, page);
  std::vector<std::string> words;
  words.reserve(4);
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* block = static_cast<const PageLine&>(*element).getBlock();
    for (uint16_t i = 0; i < block->wordCount(); ++i) words.emplace_back(block->wordText(i));
  }
  EXPECT_EQ((std::vector<std::string>{"first", "words", "last", "words"}), words);
}

TEST_F(SectionParserIntegration, SpacingSurvivesParagraphTableAndSoftFlushCacheRoundTrips) {
  spec.characterSpacing = -1;
  spec.wordSpacingPercent = 150;
  spec.viewportHeight = 160;
  std::string chapter = "<html><body><p>ab cd</p><table><tr><td>ab cd</td></tr></table><p>";
  chapter.reserve(3200);
  for (int pair = 0; pair < 500; ++pair) chapter += "ab cd ";
  chapter += "</p></body></html>";
  prepare(chapter.c_str());
  ASSERT_TRUE(section->buildSomeMore(0));
  ASSERT_TRUE(section->loadSectionFile(spec));
  ASSERT_GT(section->pageCount, 1u);
  const auto bytes = readCache();
  ASSERT_GE(bytes.size(), SectionPageReader::HEADER_SIZE);
  EXPECT_EQ(bytes[0], 49);
  EXPECT_EQ(SectionPageReader::HEADER_SIZE, 43u);
  size_t words = 0;
  for (uint16_t index = 0; index < section->pageCount; ++index) {
    SCOPED_TRACE(index);
    const auto page = SectionPageReader::load(section->filePath, index);
    ASSERT_NE(page, nullptr);
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto* block = static_cast<const PageLine&>(*element).getBlock();
      EXPECT_EQ(block->getBlockStyle().characterSpacing, -1);
      words += block->wordCount();
    }
  }
  EXPECT_EQ(words, 1004u);
}

TEST_F(SectionParserIntegration, EitherSpacingChangeInvalidatesCachedLayout) {
  spec.characterSpacing = -2;
  spec.wordSpacingPercent = 150;
  for (bool changeTracking : {false, true}) {
    SCOPED_TRACE(changeTracking);
    commitPrevious();
    ASSERT_TRUE(section->loadSectionFile(spec));
    ReaderRenderSpec changed = spec;
    if (changeTracking) {
      ++changed.characterSpacing;
    } else {
      ++changed.wordSpacingPercent;
    }
    EXPECT_FALSE(section->loadSectionFile(changed));
    EXPECT_FALSE(Storage.exists(section->filePath.c_str()));
  }
}

TEST_F(SectionParserIntegration, BothPreviousVersion47HeaderLayoutsAreRejectedBeforeFieldReads) {
  commitPrevious();
  constexpr size_t SPACING_OFFSET = 19;
  for (bool oldLocalHeader : {false, true}) {
    for (uint8_t version : {47, 235}) {
      SCOPED_TRACE(oldLocalHeader);
      SCOPED_TRACE(version);
      auto bytes = previous;
      bytes[0] = version;
      if (oldLocalHeader) bytes.erase(bytes.begin() + SPACING_OFFSET, bytes.begin() + SPACING_OFFSET + 2);
      {
        std::ofstream out(section->filePath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      }
      parser_test::ScopedReadFailure fault(1);
      EXPECT_FALSE(section->loadSectionFile(spec));
      EXPECT_EQ(fault.failures(), 0u);
      EXPECT_FALSE(Storage.exists(section->filePath.c_str()));
    }
  }
}

TEST_F(SectionParserIntegration, PreviousVersion48AndPartialAreRejectedBeforeFieldReads) {
  commitPrevious();
  for (uint8_t version : {48, 234}) {
    SCOPED_TRACE(version);
    auto bytes = previous;
    bytes[0] = version;
    {
      std::ofstream out(section->filePath, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    parser_test::ScopedReadFailure fault(1);
    EXPECT_FALSE(section->loadSectionFile(spec));
    EXPECT_EQ(fault.failures(), 0u);
    EXPECT_FALSE(Storage.exists(section->filePath.c_str()));
  }
}

TEST_F(SectionParserIntegration, TwoPageTicksMeasureNewPagesBehindPartialWatermark) {
  std::string chapter = "<html><body>";
  chapter.reserve(64000);
  for (int paragraph = 0; paragraph < 1200; ++paragraph) {
    chapter += "<p>one two three four five six seven</p>";
  }
  chapter += "</body></html>";
  for (bool partial : {false, true}) {
    SCOPED_TRACE(partial);
    prepare(chapter.c_str());
    if (partial) {
      section->partial_ = true;
      section->partialPageCount_ = section->pageCount = 16;
    }
    ASSERT_TRUE(section->buildSomeMore(2));
    ASSERT_NE(nullptr, section->build_);
    EXPECT_EQ(section->builtPageCount_, 2);
    EXPECT_EQ(section->pageCount, partial ? 16 : 2);
    EXPECT_LT(section->build_->parser->parseBytesConsumed(), section->build_->parser->parseTotalBytes());
    int ticks = 1;
    while (section->build_ && ticks++ < 100) {
      const auto before = section->builtPageCount_;
      ASSERT_TRUE(section->buildSomeMore(2));
      EXPECT_LE(section->builtPageCount_ - before, 2);
    }
    ASSERT_LT(ticks, 100);
    EXPECT_TRUE(section->buildComplete_);
    EXPECT_GT(section->pageCount, 16);
    EXPECT_NE(nullptr, SectionPageReader::load(section->filePath, section->pageCount - 1));
  }
}

TEST_F(SectionParserIntegration, LongParagraphRemainsDecodableAcrossBudgetedTicks) {
  std::string chapter = "<html><body><p>";
  chapter.reserve(64000);
  for (int word = 0; word < 10000; ++word) chapter += "word ";
  chapter += "</p></body></html>";
  for (uint16_t height : {800, 160}) {
    SCOPED_TRACE(height);
    spec.viewportHeight = height;
    prepare(chapter.c_str());
    ASSERT_TRUE(section->buildSomeMore(2));
    int largestTick = section->builtPageCount_;
    // Soft-flush layout can emit multiple pages before Section rechecks its budget.
    // Record the observed batch without requiring that overshoot to persist.
    int ticks = 1;
    while (section->build_ && ticks++ < 100) {
      const int before = section->builtPageCount_;
      ASSERT_TRUE(section->buildSomeMore(2));
      largestTick = std::max(largestTick, section->builtPageCount_ - before);
    }
    ASSERT_LT(ticks, 100);
    RecordProperty("largest_tick_pages_at_" + std::to_string(height) + "px", largestTick);
    EXPECT_TRUE(section->buildComplete_);
    EXPECT_GT(section->pageCount, 2);
    EXPECT_NE(nullptr, SectionPageReader::load(section->filePath, section->pageCount - 1));
  }
}

TEST_F(SectionParserIntegration, LayoutFailureCannotReplaceCommittedCache) {
  commitPrevious();
  prepare("<html><body><p>lostword</p><p>survivor</p></body></html>");
  bool succeeded;
  size_t failures;
  {
    Fault fault(Fault::Kind::Object, sizeof(TextBlock));
    succeeded = section->buildSomeMore(0);
    failures = fault.failures();
  }
  ASSERT_EQ(1u, failures);
  EXPECT_FALSE(succeeded);
  expectFailedBuild();
  prepare("<html><body><p>lostword survivor</p></body></html>");
  EXPECT_TRUE(section->buildSomeMore(0));
}

TEST_F(SectionParserIntegration, FailedFinalPageCannotPublishCompleteOrPartialCache) {
  commitPrevious();
  prepare("<html><body>lostword</body></html>");
  ASSERT_EQ(ChapterHtmlSlimParser::ParseStatus::Done, section->build_->parser->parseStep());
  bool succeeded;
  size_t failures;
  {
    Fault fault(Fault::Kind::Object, sizeof(PageLine));
    succeeded = section->finalizeBuild();
    failures = fault.failures();
  }
  ASSERT_EQ(1u, failures);
  EXPECT_FALSE(succeeded);
  expectFailedBuild();
  section.reset();
  EXPECT_EQ(previous, readCache());
}

TEST_F(SectionParserIntegration, FailedPageWriteStopsParserAndPreservesCommittedCache) {
  commitPrevious();
  prepare("<html><body><p>first</p><p>last</p></body></html>");
  bool succeeded;
  size_t failures;
  {
    parser_test::ScopedWriteFailure fault;
    succeeded = section->buildSomeMore(0);
    failures = fault.failures();
  }
  ASSERT_EQ(1u, failures);
  EXPECT_FALSE(succeeded);
  expectFailedBuild();
}

TEST_F(SectionParserIntegration, MalformedChapterCannotReplaceCommittedCache) {
  commitPrevious();
  prepare("<html><body><p>first</p><p>unterminated</body></html>");
  EXPECT_FALSE(section->buildSomeMore(0));
  expectFailedBuild();
}

TEST_F(SectionParserIntegration, SuspendAfterLatchedErrorDoesNotPublishPartialCache) {
  commitPrevious();
  prepare("<html><body><p>first</p><p>last</p></body></html>");
  ASSERT_EQ(ChapterHtmlSlimParser::ParseStatus::Done, section->build_->parser->parseStep());
  section->build_->parser->markFailed();
  section->suspendBuild();
  expectFailedBuild();
}

TEST_F(SectionParserIntegration, EveryTruncatedHeaderIsRejectedWithoutUsingUninitializedFields) {
  commitPrevious();
  for (size_t bytes = 0; bytes < SectionPageReader::HEADER_SIZE; ++bytes) {
    SCOPED_TRACE(bytes);
    {
      std::ofstream out(section->filePath, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(previous.data()), static_cast<std::streamsize>(bytes));
    }
    EXPECT_FALSE(section->loadSectionFile(spec));
    EXPECT_EQ(0u, section->pageCount);
    EXPECT_FALSE(section->partial_);
  }
}

TEST_F(SectionParserIntegration, HeaderWriteFailureDoesNotAllowPageConstruction) {
  section = std::make_unique<Section>();
  section->filePath = directory + "/section";
  ASSERT_TRUE(section->file.open(section->binTmpPath().c_str(), "wb+"));
  bool succeeded;
  size_t failures;
  {
    parser_test::ScopedWriteFailure fault;
    succeeded = section->writeSectionFileHeader(spec);
    failures = fault.failures();
  }
  EXPECT_FALSE(succeeded);
  EXPECT_EQ(1u, failures);
  EXPECT_FALSE(Storage.exists(section->filePath.c_str()));
}

TEST_F(SectionParserIntegration, HeaderReadErrorsPreserveCacheForRetry) {
  commitPrevious();
  for (size_t successfulReads = 0; successfulReads < 14; ++successfulReads) {
    SCOPED_TRACE(successfulReads);
    bool succeeded;
    size_t failures;
    {
      parser_test::ScopedReadFailure fault(successfulReads);
      succeeded = section->loadSectionFile(spec);
      failures = fault.failures();
    }
    ASSERT_EQ(1u, failures);
    EXPECT_FALSE(succeeded);
    EXPECT_EQ(previous, readCache());
    EXPECT_TRUE(section->loadSectionFile(spec));
  }
}

TEST_F(SectionParserIntegration, InvalidHeaderBooleanIsRejected) {
  commitPrevious();
  previous[sizeof(uint8_t) + sizeof(int) + sizeof(float)] = 2;
  {
    std::ofstream out(section->filePath, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(previous.data()), static_cast<std::streamsize>(previous.size()));
  }
  EXPECT_FALSE(section->loadSectionFile(spec));
  EXPECT_EQ(0u, section->pageCount);
}

TEST_F(SectionParserIntegration, SuspendedBuildIsReadableAndOverflowingWatermarkIsRejected) {
  spec.viewportHeight = 18;
  prepare("<html><body><p>one</p><p>two</p><p>three</p><p>four</p></body></html>");
  ASSERT_EQ(ChapterHtmlSlimParser::ParseStatus::Done, section->build_->parser->parseStep());
  ASSERT_GT(section->builtPageCount_, 0u);
  section->suspendBuild();
  ASSERT_TRUE(section->partial_);
  ASSERT_TRUE(section->loadSectionFile(spec));
  EXPECT_TRUE(section->partial_);
  ASSERT_NE(nullptr, SectionPageReader::load(section->filePath, 0));
  auto bytes = readCache();
  ASSERT_FALSE(bytes.empty());
  EXPECT_EQ(bytes[0], 233);
  const uint32_t forgedOffset = UINT32_MAX - 3;
  std::memcpy(bytes.data() + SectionPageReader::HEADER_SIZE - sizeof(uint32_t), &forgedOffset, sizeof(forgedOffset));
  {
    std::ofstream out(section->filePath, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  EXPECT_FALSE(section->loadSectionFile(spec));
  EXPECT_EQ(0u, section->pageCount);
}
}  // namespace
