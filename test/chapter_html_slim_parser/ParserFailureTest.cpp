#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ScopedAllocationFailure.h"

namespace {

using ParseStatus = ChapterHtmlSlimParser::ParseStatus;
using Fault = parser_test::ScopedAllocationFailure;

class ParserFailureTest : public ::testing::Test {
 protected:
  std::string filepath;
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  std::unique_ptr<ChapterHtmlSlimParser> parser;
  std::vector<std::string> words;
  std::vector<uint32_t> pageOffsets;
  std::vector<std::pair<int, int>> imageDimensions;
  size_t nullPages = 0;
  size_t steps = 0;
  size_t pageCallbacks = 0;
  size_t rejectPageCallback = 0;

  void SetUp() override {
    ASSERT_EQ(parser_test::openFileCount, 0u);
    filepath = (std::filesystem::temp_directory_path() / "crosspoint-parser-failure-XXXXXX").string();
    const int descriptor = mkstemp(filepath.data());
    ASSERT_NE(descriptor, -1);
    close(descriptor);
    words.reserve(512);
    pageOffsets.reserve(128);
  }

  void TearDown() override {
    parser.reset();
    EXPECT_EQ(parser_test::openFileCount, 0u);
    std::error_code error;
    std::filesystem::remove(filepath, error);
  }

  void writeInput(const std::string& input) {
    std::ofstream output(filepath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(input.data(), static_cast<std::streamsize>(input.size()));
    ASSERT_TRUE(output.good());
  }

  void createParser(bool focus = false, uint16_t width = 480, uint16_t height = 800,
                    std::vector<std::string> tocAnchors = {}, std::shared_ptr<Epub> epub = nullptr) {
    parser.reset();
    words.clear();
    pageOffsets.clear();
    imageDimensions.clear();
    nullPages = 0;
    steps = 0;
    pageCallbacks = 0;
    rejectPageCallback = 0;
    parser = std::make_unique<ChapterHtmlSlimParser>(
        epub, filepath, renderer, 0, 1.0f, false, 0, width, height, false, focus,
        [this](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t offset) {
          ++pageCallbacks;
          if (!page) {
            ++nullPages;
            return;
          }
          if (pageCallbacks == rejectPageCallback) {
            parser->markFailed();
            return;
          }
          pageOffsets.push_back(offset);
          for (const auto& element : page->elements) {
            if (element->getTag() == TAG_PageImage) {
              const auto& image = static_cast<const PageImage&>(*element).getImageBlock();
              imageDimensions.emplace_back(image.getWidth(), image.getHeight());
            }
            if (element->getTag() != TAG_PageLine) continue;
            const auto* block = static_cast<const PageLine&>(*element).getBlock();
            for (uint16_t i = 0; i < block->wordCount(); ++i) words.emplace_back(block->wordText(i));
          }
        },
        true, "", "", 0, std::move(tocAnchors), nullptr, &cssParser);
  }

  bool parseIncrementally() {
    if (!parser->beginParse()) return false;
    while (++steps <= 10000) {
      const auto status = parser->parseStep();
      if (status == ParseStatus::Error) {
        parser->abortParse();
        return false;
      }
      if (status == ParseStatus::Done) return parser->finishParse();
    }
    parser->abortParse();
    return false;
  }

  std::string numberedChapter(size_t count) {
    std::string input = "<html><body>";
    for (size_t i = 0; i < count; ++i) input += "<p>word" + std::to_string(i) + "</p>";
    return input + "</body></html>";
  }
};

TEST_F(ParserFailureTest, OneShotAndIncrementalParsingPreserveEveryWordAndPageOffset) {
  writeInput(numberedChapter(180));
  createParser(false, 240, 48);
  ASSERT_TRUE(parser->parseAndBuildPages());
  ASSERT_EQ(words.size(), 180u);
  for (size_t i = 0; i < words.size(); ++i) EXPECT_EQ(words[i], "word" + std::to_string(i));
  EXPECT_EQ(nullPages, 0u);
  const auto expectedWords = words;
  const auto expectedOffsets = pageOffsets;
  EXPECT_EQ(parser_test::openFileCount, 0u);

  createParser(false, 240, 48);
  EXPECT_TRUE(parseIncrementally());
  EXPECT_GT(steps, 1u);
  EXPECT_EQ(words, expectedWords);
  EXPECT_EQ(pageOffsets, expectedOffsets);
  EXPECT_EQ(nullPages, 0u);
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

TEST_F(ParserFailureTest, MissingInputIsRejectedWithoutPublishingPagesOrRetainingAFile) {
  std::filesystem::remove(filepath);
  createParser();
  EXPECT_FALSE(parser->parseAndBuildPages());
  EXPECT_TRUE(pageOffsets.empty());
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

class CorruptChapterTest : public ParserFailureTest, public ::testing::WithParamInterface<const char*> {};

TEST_P(CorruptChapterTest, RejectsInputWithoutCompletingTheTrailingPage) {
  writeInput(GetParam());
  createParser();
  EXPECT_FALSE(parser->parseAndBuildPages());
  EXPECT_TRUE(pageOffsets.empty());
  EXPECT_EQ(nullPages, 0u);
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

INSTANTIATE_TEST_SUITE_P(CorruptXml, CorruptChapterTest,
                         ::testing::Values("<html><body><p>unfinished",
                                           "<html><body><p>wrong close</div></body></html>",
                                           "<html><body><p>invalid \xC3\x28</p></body></html>", ""));

TEST_F(ParserFailureTest, TrailingJunkAfterClosedHtmlKeepsCompleteChapter) {
  writeInput("<html><body><p>complete text</p></body></html>trailing converter junk");
  createParser();
  EXPECT_TRUE(parser->parseAndBuildPages());
  EXPECT_EQ(words, (std::vector<std::string>{"complete", "text"}));
  EXPECT_EQ(nullPages, 0u);
}

class Utf8ReadBoundaryTest : public ParserFailureTest, public ::testing::WithParamInterface<size_t> {};

TEST_P(Utf8ReadBoundaryTest, PreservesCodepointSplitAcrossParserReads) {
  const std::string prefix = "<html><body><p>";
  writeInput(prefix + std::string(1024 - prefix.size() - GetParam(), ' ') + "\xF0\x9F\x8C\x8A tail</p></body></html>");
  createParser();
  EXPECT_TRUE(parseIncrementally());
  EXPECT_GT(steps, 1u);
  EXPECT_EQ(words, (std::vector<std::string>{"\xF0\x9F\x8C\x8A", "tail"}));
  EXPECT_EQ(nullPages, 0u);
}

INSTANTIATE_TEST_SUITE_P(Utf8, Utf8ReadBoundaryTest, ::testing::Values(1u, 2u, 3u));

TEST_F(ParserFailureTest, RealHtmlEntitiesPreserveTextUnderFirmwareExpatConfiguration) {
  writeInput(
      "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" "
      "\"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">"
      "<html><body><p>alpha&nbsp;beta &amp; gamma&#x2014;delta</p></body></html>");
  createParser();
  EXPECT_TRUE(parser->parseAndBuildPages());
  EXPECT_EQ(words, (std::vector<std::string>{"alpha", " ", "beta", "&",
                                             "gamma\xE2\x80\x94"
                                             "delta"}));
}

class ParserReadFailureTest : public ParserFailureTest, public ::testing::WithParamInterface<int> {};

TEST_P(ParserReadFailureTest, InitialReadFailureIsRejectedAndClosesTheInput) {
  writeInput(numberedChapter(10));
  createParser();
  bool success;
  size_t failures;
  {
    parser_test::ScopedReadFailure fault(0, GetParam());
    success = parser->parseAndBuildPages();
    failures = fault.failures();
  }
  EXPECT_EQ(failures, 1u);
  EXPECT_FALSE(success);
  EXPECT_TRUE(pageOffsets.empty());
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

TEST_P(ParserReadFailureTest, MidChapterReadFailureDoesNotPublishTheTrailingPage) {
  writeInput(numberedChapter(180));
  createParser(false, 240, 48);
  ASSERT_TRUE(parser->beginParse());
  ASSERT_EQ(parser->parseStep(), ParseStatus::More);
  ASSERT_GT(pageOffsets.size(), 0u);
  const auto completedPages = pageOffsets.size();
  ParseStatus status;
  size_t failures;
  {
    parser_test::ScopedReadFailure fault(0, GetParam());
    status = parser->parseStep();
    failures = fault.failures();
  }
  EXPECT_EQ(failures, 1u);
  EXPECT_EQ(status, ParseStatus::Error);
  parser->abortParse();
  EXPECT_EQ(pageOffsets.size(), completedPages);
  EXPECT_EQ(parser->parseBytesConsumed(), 0u);
  EXPECT_EQ(parser->parseTotalBytes(), 0u);
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

INSTANTIATE_TEST_SUITE_P(SdReadResults, ParserReadFailureTest, ::testing::Values(-1, 0),
                         [](const auto& info) { return info.param < 0 ? "NegativeIoError" : "DefensiveZeroStall"; });

TEST_F(ParserFailureTest, DestroyingPausedParserClosesInputWithoutCompletingAnotherPage) {
  writeInput(numberedChapter(180));
  createParser(false, 240, 48);
  ASSERT_TRUE(parser->beginParse());
  ASSERT_EQ(parser->parseStep(), ParseStatus::More);
  const auto completedPages = pageOffsets.size();
  ASSERT_EQ(parser_test::openFileCount, 1u);
  parser.reset();
  EXPECT_EQ(pageOffsets.size(), completedPages);
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

TEST_F(ParserFailureTest, CheckedPageLineFailureDuringStepIsReportedBeforeDone) {
  writeInput("<html><body><p>lostword</p></body></html>");
  createParser();
  ASSERT_TRUE(parser->beginParse());
  ParseStatus status;
  size_t failures;
  {
    Fault fault(Fault::Kind::Object, sizeof(PageLine));
    status = parser->parseStep();
    failures = fault.failures();
  }
  ASSERT_EQ(failures, 1u);
  EXPECT_EQ(status, ParseStatus::Error) << "Incremental callers must not finalize an incomplete layout";
  parser->abortParse();
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

TEST_F(ParserFailureTest, FinalPageAllocationFailureDoesNotReportSuccessfulFinish) {
  writeInput("<html><body>lostword</body></html>");
  createParser();
  ASSERT_TRUE(parser->beginParse());
  ASSERT_EQ(parser->parseStep(), ParseStatus::Done);
  ASSERT_TRUE(pageOffsets.empty());
  bool success;
  size_t failures;
  {
    Fault fault(Fault::Kind::Object, sizeof(PageLine));
    success = parser->finishParse();
    failures = fault.failures();
  }
  ASSERT_EQ(failures, 1u);
  EXPECT_FALSE(success) << "Final page was reported complete after dropping its only word; emitted words: "
                        << ::testing::PrintToString(words);
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

TEST_F(ParserFailureTest, EveryCheckedAllocationFailureRejectsMixedChapterWithoutNullPages) {
  writeInput(
      "<html><body><p>startword</p><p id=\"chapter-two\">nextword</p><hr/>"
      "<table><tr><td>left one</td><td>right two</td></tr></table><p>endword</p></body></html>");
  createParser(false, 240, 48, {"chapter-two"});
  size_t allocationCount;
  bool healthy;
  {
    Fault observe(Fault::Kind::Any, 0, 0);
    healthy = parser->parseAndBuildPages();
    allocationCount = observe.matchingCalls();
  }
  ASSERT_TRUE(healthy);
  ASSERT_GT(allocationCount, 15u);
  const auto expectedWords = words;
  const auto expectedOffsets = pageOffsets;
  for (size_t allocation = 1; allocation <= allocationCount; ++allocation) {
    SCOPED_TRACE(allocation);
    createParser(false, 240, 48, {"chapter-two"});
    size_t failures;
    bool success;
    {
      Fault fault(Fault::Kind::Any, 0, allocation, 2);
      success = parser->parseAndBuildPages();
      failures = fault.failures();
    }
    ASSERT_GE(failures, 1u);
    EXPECT_FALSE(success);
    EXPECT_TRUE(parser->hasError());
    EXPECT_EQ(nullPages, 0u);
    EXPECT_EQ(parser_test::openFileCount, 0u);
    const auto callbacksBeforeFinish = pageCallbacks;
    EXPECT_FALSE(parser->finishParse());
    EXPECT_EQ(pageCallbacks, callbacksBeforeFinish);
  }
  createParser(false, 240, 48, {"chapter-two"});
  EXPECT_TRUE(parser->parseAndBuildPages());
  EXPECT_EQ(words, expectedWords);
  EXPECT_EQ(pageOffsets, expectedOffsets);
}

TEST_F(ParserFailureTest, WordChunkReclaimRetryPreservesEveryWord) {
  writeInput("<html><body><p>first second third</p></body></html>");
  createParser();
  {
    Fault fault(Fault::Kind::Array, 2048);
    EXPECT_TRUE(parser->parseAndBuildPages());
    EXPECT_EQ(fault.failures(), 1u);
    EXPECT_GE(fault.matchingCalls(), 2u);
  }
  EXPECT_EQ(words, (std::vector<std::string>{"first", "second", "third"}));
  EXPECT_FALSE(parser->hasError());
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

TEST_F(ParserFailureTest, ExhaustedWordChunkRetryCannotDisappearAtEmptyBlockBoundaries) {
  for (const char* body : {"<p>lost</p><hr/><table><tr><td>survivor</td></tr></table>",
                           "<ul><li></li></ul><table><tr><td>survivor</td></tr></table>",
                           "<ol><li></li></ol><table><tr><td>survivor</td></tr></table>"}) {
    SCOPED_TRACE(body);
    writeInput(std::string("<html><body>") + body + "</body></html>");
    createParser();
    {
      Fault fault(Fault::Kind::Array, 2048, 1, 2);
      EXPECT_FALSE(parser->parseAndBuildPages());
      EXPECT_EQ(fault.failures(), 2u);
    }
    EXPECT_TRUE(parser->hasError());
    EXPECT_EQ(pageCallbacks, 0u);
    EXPECT_EQ(parser_test::openFileCount, 0u);
    EXPECT_FALSE(parser->finishParse());
  }
}

TEST_F(ParserFailureTest, ParsedTextReclaimRetryRecoversOrFailsWithoutPublishingPartialContent) {
  writeInput("<html><body><p>retained</p></body></html>");
  for (size_t failures : {1u, 2u}) {
    createParser();
    {
      Fault fault(Fault::Kind::Object, sizeof(ParsedText), 1, failures);
      EXPECT_EQ(parser->parseAndBuildPages(), failures == 1);
      EXPECT_EQ(fault.failures(), failures);
    }
    if (failures == 1)
      EXPECT_EQ(words, (std::vector<std::string>{"retained"}));
    else {
      EXPECT_TRUE(parser->hasError());
      EXPECT_EQ(pageCallbacks, 0u);
    }
    EXPECT_EQ(parser_test::openFileCount, 0u);
  }
}

TEST_F(ParserFailureTest, RejectingCompletedPageStopsFurtherCallbacksAndFinalization) {
  writeInput(numberedChapter(180));
  createParser(false, 240, 48);
  rejectPageCallback = 1;
  EXPECT_FALSE(parser->parseAndBuildPages());
  EXPECT_TRUE(parser->hasError());
  EXPECT_EQ(pageCallbacks, 1u);
  EXPECT_EQ(nullPages, 0u);
  EXPECT_FALSE(parser->finishParse());
  EXPECT_EQ(pageCallbacks, 1u);
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

TEST_F(ParserFailureTest, RejectingFinalPageCannotBecomeSuccessfulOnRepeatedFinish) {
  writeInput("<html><body>trailing text</body></html>");
  createParser();
  ASSERT_TRUE(parser->beginParse());
  ASSERT_EQ(parser->parseStep(), ParseStatus::Done);
  rejectPageCallback = 1;
  EXPECT_FALSE(parser->finishParse());
  EXPECT_TRUE(parser->hasError());
  EXPECT_EQ(pageCallbacks, 1u);
  EXPECT_FALSE(parser->finishParse());
  EXPECT_EQ(pageCallbacks, 1u);
  EXPECT_EQ(nullPages, 0u);
}

TEST_F(ParserFailureTest, SuccessfulFinalizationIsIdempotentAndEmptyChapterNeverDeliversNull) {
  writeInput("<html><body></body></html>");
  createParser();
  EXPECT_TRUE(parser->parseAndBuildPages());
  EXPECT_FALSE(parser->hasError());
  ASSERT_EQ(pageCallbacks, 1u);
  EXPECT_EQ(nullPages, 0u);
  EXPECT_TRUE(parser->finishParse());
  EXPECT_EQ(parser->parseStep(), ParseStatus::Done);
  EXPECT_EQ(pageCallbacks, 1u);
}

TEST_F(ParserFailureTest, PrematureFinishAndCancelledParseCannotPublishTrailingPages) {
  writeInput(numberedChapter(180));
  createParser(false, 240, 48);
  ASSERT_TRUE(parser->beginParse());
  ASSERT_EQ(parser->parseStep(), ParseStatus::More);
  const auto completed = pageCallbacks;
  EXPECT_FALSE(parser->finishParse());
  EXPECT_TRUE(parser->hasError());
  EXPECT_EQ(pageCallbacks, completed);
  EXPECT_EQ(parser_test::openFileCount, 0u);

  createParser(false, 240, 48);
  ASSERT_TRUE(parser->beginParse());
  ASSERT_EQ(parser->parseStep(), ParseStatus::More);
  const auto completedBeforeAbort = pageCallbacks;
  parser->abortParse();
  EXPECT_FALSE(parser->finishParse());
  EXPECT_EQ(pageCallbacks, completedBeforeAbort);
  EXPECT_EQ(parser->parseStep(), ParseStatus::Error);
  EXPECT_EQ(parser_test::openFileCount, 0u);
}

TEST_F(ParserFailureTest, ReusingAParserIsRejectedAndFreshRetryStartsAtOriginalPosition) {
  writeInput(numberedChapter(10));
  createParser();
  ASSERT_TRUE(parser->parseAndBuildPages());
  const auto expectedWords = words;
  const auto callbacks = pageCallbacks;
  EXPECT_FALSE(parser->beginParse());
  EXPECT_TRUE(parser->hasError());
  EXPECT_EQ(pageCallbacks, callbacks);
  EXPECT_EQ(parser_test::openFileCount, 0u);
  createParser();
  EXPECT_TRUE(parser->parseAndBuildPages());
  EXPECT_EQ(words, expectedWords);
  ASSERT_FALSE(pageOffsets.empty());
  EXPECT_EQ(pageOffsets.front(), 0u);
}

TEST_F(ParserFailureTest, LayoutStopsAtFirstConsumerFailure) {
  ParsedText text(false);
  for (int i = 0; i < 10; ++i) text.addWord("word" + std::to_string(i), EpdFontFamily::REGULAR);
  size_t lines = 0;
  EXPECT_FALSE(text.layoutAndExtractLines(renderer, 0, 100, [&](std::unique_ptr<TextBlock>, uint32_t) {
    ++lines;
    return false;
  }));
  EXPECT_EQ(lines, 1u);
}

TEST_F(ParserFailureTest, LongParagraphSoftFlushStopsOnAllocationFailureBeforeClosingTag) {
  std::string input = "<html><body><p>";
  for (size_t i = 0; i < 1000; ++i) input += "word" + std::to_string(i) + " ";
  input += "</p></body></html>";
  writeInput(input);
  createParser(false, 240, 48);
  ASSERT_TRUE(parser->beginParse());
  ParseStatus status = ParseStatus::More;
  size_t failures;
  {
    Fault fault(Fault::Kind::Object, sizeof(TextBlock));
    while (status == ParseStatus::More && ++steps <= 10000) status = parser->parseStep();
    failures = fault.failures();
  }
  ASSERT_EQ(failures, 1u);
  EXPECT_EQ(status, ParseStatus::Error);
  EXPECT_TRUE(parser->hasError());
  // Each step reads at most 1,024 bytes. This failure precedes the closing tag,
  // so the soft flush, rather than the final paragraph flush, must propagate it.
  EXPECT_LT(steps * 1024, input.size());
  EXPECT_FALSE(parser->finishParse());
  EXPECT_EQ(nullPages, 0u);
  EXPECT_EQ(parser_test::openFileCount, 0u);

  createParser(false, 240, 48);
  ASSERT_TRUE(parseIncrementally());
  ASSERT_EQ(words.size(), 1000u);
  for (size_t i = 0; i < words.size(); ++i) EXPECT_EQ(words[i], "word" + std::to_string(i));
}

TEST_F(ParserFailureTest, ExtremeImageAspectRatiosKeepBothRenderedDimensionsPositive) {
  writeInput("<html><body><img src=\"image.png\"/></body></html>");
  for (const bool wide : {false, true}) {
    auto epub = std::make_shared<Epub>();
    epub->imageHeader = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13,
                         'I',  'H',  'D',  'R',  0,    0,    0,    1,    0, 0, 0, 1};
    const size_t dimension = wide ? 18 : 22;
    epub->imageHeader[dimension] = 0x7f;
    epub->imageHeader[dimension + 1] = 0xff;
    createParser(false, 480, 800, {}, epub);
    ASSERT_TRUE(parser->parseAndBuildPages());
    ASSERT_EQ(imageDimensions.size(), 1u);
    EXPECT_EQ(imageDimensions.front(), std::make_pair(wide ? 480 : 1, wide ? 1 : 800));
    EXPECT_EQ(nullPages, 0u);
  }
}

struct AllocationCase {
  const char* name;
  Fault::Kind kind;
  size_t bytes;
  bool focus;
  bool incremental;
  bool table;
};

void PrintTo(const AllocationCase& parameter, std::ostream* output) { *output << parameter.name; }

// TextBlock stores word offset, X, style, and a NUL-terminated word in its arena.
constexpr size_t LOST_WORD_ARENA_BYTES =
    sizeof(TextBlock::SourceRange) + sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t) + sizeof("lostword");
constexpr size_t FOCUS_WORD_ARENA_BYTES = LOST_WORD_ARENA_BYTES + sizeof(uint16_t) + sizeof(uint8_t);

class ParserAllocationFailureTest : public ParserFailureTest, public ::testing::WithParamInterface<AllocationCase> {};

TEST_P(ParserAllocationFailureTest, RejectsChapterWhenCheckedAllocationDropsText) {
  const auto parameter = GetParam();
  writeInput(parameter.table ? "<html><body><table><tr><td>lostword</td><td>survivor</td></tr></table></body></html>"
                             : "<html><body><p>lostword</p><p>survivor</p></body></html>");
  createParser(parameter.focus);
  bool success;
  size_t failures;
  size_t matchingCalls;
  {
    Fault fault(parameter.kind, parameter.bytes, 1, parameter.kind == Fault::Kind::Array ? 2 : 1);
    success = parameter.incremental ? parseIncrementally() : parser->parseAndBuildPages();
    failures = fault.failures();
    matchingCalls = fault.matchingCalls();
  }
  ASSERT_EQ(failures, parameter.kind == Fault::Kind::Array ? 2u : 1u)
      << "The production allocation and any reclaim retry must actually fail";
  EXPECT_GE(matchingCalls, 1u);
  EXPECT_FALSE(success) << "Chapter was reported complete after allocation failure; emitted words: "
                        << ::testing::PrintToString(words);
  EXPECT_EQ(parser_test::openFileCount, 0u);

  // A fresh parse after the transient failure must still be able to read every word.
  createParser(parameter.focus);
  EXPECT_TRUE(parser->parseAndBuildPages());
  EXPECT_EQ(words, (std::vector<std::string>{"lostword", "survivor"}));
  EXPECT_EQ(nullPages, 0u);
}

INSTANTIATE_TEST_SUITE_P(
    CheckedOom, ParserAllocationFailureTest,
    ::testing::Values(AllocationCase{"TextBlock", Fault::Kind::Object, sizeof(TextBlock), false, false, false},
                      AllocationCase{"TextArena", Fault::Kind::Array, LOST_WORD_ARENA_BYTES, false, false, false},
                      AllocationCase{"PageLine", Fault::Kind::Object, sizeof(PageLine), false, false, false},
                      AllocationCase{"FocusTextBlock", Fault::Kind::Object, sizeof(TextBlock), true, false, false},
                      AllocationCase{"FocusTextArena", Fault::Kind::Array, FOCUS_WORD_ARENA_BYTES, true, false, false},
                      AllocationCase{"IncrementalPageLine", Fault::Kind::Object, sizeof(PageLine), false, true, false},
                      AllocationCase{"TableTextBlock", Fault::Kind::Object, sizeof(TextBlock), false, false, true}),
    [](const auto& info) { return info.param.name; });

}  // namespace
