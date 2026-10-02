#include <Epub/Page.h>
#include <Epub/SectionPageReader.h>
#include <Serialization.h>
#include <gtest/gtest.h>

#include <climits>
#include <cstring>
#include <limits>

#include "HostAllocations.h"
#include "PageTurnFixture.h"

namespace {
using namespace epub_page_test;

class PersistenceFault : public ::testing::Test {
 protected:
  void SetUp() override {
    faults = {};
    io = {};
    fileExists = {true, true, false};
  }
  void TearDown() override {
    faults = {};
    io = {};
    fileExists = {true, true, false};
    captureAllocations = false;
  }

  static std::unique_ptr<TextBlock> text(bool ruby = false) {
    const std::vector<std::string> words{"alpha", "beta"};
    const std::vector<int16_t> positions{0, 55};
    const std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR, EpdFontFamily::BOLD};
    std::vector<std::string> rubies;
    if (ruby) rubies = {"reading", ""};
    BlockStyle style;
    style.marginTop = 7;
    style.directionDefined = true;
    return std::make_unique<TextBlock>(words, positions, styles, std::vector<uint8_t>{}, std::vector<uint16_t>{}, style,
                                       std::move(rubies));
  }

  template <typename Serializable>
  static std::vector<uint8_t> encode(Serializable& value) {
    std::vector<uint8_t> bytes;
    bytes.reserve(1024);
    HalFile file;
    file.open(bytes);
    EXPECT_TRUE(value.serialize(file));
    return bytes;
  }

  template <typename T>
  static void patch(std::vector<uint8_t>& bytes, size_t position, T value) {
    ASSERT_LE(position + sizeof(value), bytes.size());
    std::memcpy(bytes.data() + position, &value, sizeof(value));
  }

  static std::unique_ptr<TextBlock> decodeText(std::vector<uint8_t>& bytes) {
    HalFile file;
    file.open(bytes);
    return TextBlock::deserialize(file);
  }

  static std::unique_ptr<Page> decodePage(std::vector<uint8_t>& bytes) {
    HalFile file;
    file.open(bytes);
    return Page::deserialize(file);
  }

  // The two-word fixture uses the documented arena wire layout from TextBlock.h.
  static constexpr size_t TEXT_HEADER = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint16_t);
  static constexpr size_t TEXT_BYTES = sizeof("alpha") + sizeof("beta");
  static constexpr size_t TEXT_ARENA = 2 * (sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t)) + TEXT_BYTES;
  static constexpr size_t RUBY_LENGTH = TEXT_HEADER + TEXT_ARENA;
  static constexpr size_t STYLE_OFFSET = RUBY_LENGTH + 2 * sizeof(uint32_t);
};

TEST_F(PersistenceFault, HalShortWriteChangesOnlyWrittenPrefixAndReportsFailure) {
  std::vector<uint8_t> bytes(4, 0xa5);
  const uint8_t input[]{1, 2, 3, 4};
  HalFile file;
  file.open(bytes);
  faults.writeBudget = 2;
  EXPECT_EQ(0u, file.write(input, sizeof(input)));
  EXPECT_EQ((std::vector<uint8_t>{1, 2, 0xa5, 0xa5}), bytes);
  EXPECT_EQ(2u, file.position());
  EXPECT_EQ(0u, file.write(input, 1));
  EXPECT_EQ(2u, file.position());
  EXPECT_EQ(2u, io.injectedFailures);
}

TEST_F(PersistenceFault, HalReadAndSeekFailuresLeaveCursorAndDestinationUntouched) {
  std::vector<uint8_t> bytes{1, 2, 3, 4};
  uint8_t output[]{0xa5, 0xa5};
  HalFile file;
  file.open(bytes);
  faults.failReadCall = 1;
  EXPECT_EQ(-1, file.read(output, sizeof(output)));
  EXPECT_EQ(0xa5, output[0]);
  EXPECT_EQ(0xa5, output[1]);
  EXPECT_EQ(0u, file.position());
  faults.failSeekCall = 1;
  EXPECT_FALSE(file.seek(2));
  EXPECT_EQ(0u, file.position());
  EXPECT_EQ(2u, io.injectedFailures);
}

TEST_F(PersistenceFault, HalShortReadAdvancesOnlyByTheReportedPrefix) {
  std::vector<uint8_t> bytes{1, 2, 3, 4};
  uint8_t output[]{0xa5, 0xa5};
  HalFile file;
  file.open(bytes);
  faults.shortReadCall = 1;
  EXPECT_EQ(1, file.read(output, sizeof(output)));
  EXPECT_EQ(1, output[0]);
  EXPECT_EQ(0xa5, output[1]);
  EXPECT_EQ(1u, file.position());
  EXPECT_EQ(1u, io.bytes);
  EXPECT_EQ(1u, io.injectedFailures);
}

TEST_F(PersistenceFault, HalFailedRenamePreservesSourceAndDestinationState) {
  files[2] = {1, 2, 3};
  files[0] = {4, 5, 6};
  fileExists = {true, true, true};
  faults.failRename = true;
  EXPECT_FALSE(Storage.rename("section.part", "section"));
  EXPECT_EQ((std::vector<uint8_t>{1, 2, 3}), files[2]);
  EXPECT_EQ((std::vector<uint8_t>{4, 5, 6}), files[0]);
  EXPECT_TRUE(fileExists[0]);
  EXPECT_TRUE(fileExists[2]);
  EXPECT_EQ(1u, io.injectedFailures);
}

TEST_F(PersistenceFault, TextRoundTripKeepsWordsCoordinatesRubyAndStyle) {
  auto block = text(true);
  auto bytes = encode(*block);
  auto decoded = decodeText(bytes);
  ASSERT_NE(decoded, nullptr);
  EXPECT_STREQ("alpha", decoded->wordText(0));
  EXPECT_STREQ("beta", decoded->wordText(1));
  EXPECT_EQ(55, decoded->wordXpos(1));
  EXPECT_EQ(EpdFontFamily::BOLD, decoded->wordStyle(1));
  ASSERT_EQ(2u, decoded->getRubyTexts().size());
  EXPECT_EQ("reading", decoded->getRubyTexts()[0]);
  EXPECT_EQ(7, decoded->getBlockStyle().marginTop);
  EXPECT_TRUE(decoded->getBlockStyle().directionDefined);
  EXPECT_EQ(bytes, encode(*decoded));
}

TEST_F(PersistenceFault, TextStyleBulkReadMatchesFieldwiseWireForEveryFlagAndAlignment) {
  for (int alignment = 0; alignment <= static_cast<int>(CssTextAlign::None); ++alignment) {
    for (int flags = 0; flags < 16; ++flags) {
      SCOPED_TRACE(alignment);
      SCOPED_TRACE(flags);
      BlockStyle style;
      style.alignment = static_cast<CssTextAlign>(alignment);
      style.textAlignDefined = flags & 1;
      style.textIndentDefined = flags & 2;
      style.isRtl = flags & 4;
      style.directionDefined = flags & 8;
      style.marginTop = INT16_MIN;
      style.marginBottom = INT16_MAX;
      style.marginLeft = -300;
      style.marginRight = 400;
      style.paddingTop = -500;
      style.paddingBottom = 600;
      style.paddingLeft = -700;
      style.paddingRight = 800;
      style.textIndent = -900;
      style.characterSpacing = static_cast<int8_t>(alignment - 2);
      // The writer still emits each field individually, independently of the reader's batching.
      TextBlock block({"alpha"}, {0}, {EpdFontFamily::REGULAR}, {}, {}, style);
      auto bytes = encode(block);
      auto decoded = decodeText(bytes);
      ASSERT_NE(nullptr, decoded);
      const auto& result = decoded->getBlockStyle();
      EXPECT_EQ(style.alignment, result.alignment);
      EXPECT_EQ(style.textAlignDefined, result.textAlignDefined);
      EXPECT_EQ(style.textIndentDefined, result.textIndentDefined);
      EXPECT_EQ(style.isRtl, result.isRtl);
      EXPECT_EQ(style.directionDefined, result.directionDefined);
      EXPECT_EQ(style.marginTop, result.marginTop);
      EXPECT_EQ(style.marginBottom, result.marginBottom);
      EXPECT_EQ(style.marginLeft, result.marginLeft);
      EXPECT_EQ(style.marginRight, result.marginRight);
      EXPECT_EQ(style.paddingTop, result.paddingTop);
      EXPECT_EQ(style.paddingBottom, result.paddingBottom);
      EXPECT_EQ(style.paddingLeft, result.paddingLeft);
      EXPECT_EQ(style.paddingRight, result.paddingRight);
      EXPECT_EQ(style.textIndent, result.textIndent);
      EXPECT_EQ(style.characterSpacing, result.characterSpacing);
      EXPECT_EQ(bytes, encode(*decoded));
    }
  }
}

TEST_F(PersistenceFault, TextHeaderAndStyleUseTwoReadsWithoutChangingPayloadBytes) {
  auto bytes = encode(*text());
  io = {};
  auto decoded = decodeText(bytes);
  ASSERT_NE(nullptr, decoded);
  // Header, word arena, batched empty ruby lengths, and style.
  EXPECT_EQ(4u, io.reads);
  EXPECT_EQ(bytes.size(), io.bytes);
  EXPECT_EQ(bytes, encode(*decoded));
}

TEST_F(PersistenceFault, LinkSerializationDoesNotPersistBytesAfterTerminator) {
  static constexpr char HREF[] = "chapter.xhtml#target";
  Page page;
  page.links.reserve(1);
  ASSERT_TRUE(page.addLink(HREF, 12, 45, 95, 32));
  auto& link = page.links.back();
  // Define every unused byte in a live object. Canonical serialization must
  // exclude this stale tail even when constructor initialization was correct.
  std::memset(link.href + sizeof(HREF), 0xa5, sizeof(link.href) - sizeof(HREF));

  const auto bytes = encode(page);
  // Empty elements/footnotes, then one fixed-width link followed by geometry.
  const size_t hrefOffset = 3 * sizeof(uint16_t);
  ASSERT_EQ(hrefOffset + sizeof(PageLink::href) + 4 * sizeof(int16_t), bytes.size());
  EXPECT_EQ(0, std::memcmp(bytes.data() + hrefOffset, HREF, sizeof(HREF)));
  const auto tail = bytes.begin() + hrefOffset + sizeof(HREF);
  const auto tailEnd = bytes.begin() + hrefOffset + sizeof(PageLink::href);
  EXPECT_TRUE(std::all_of(tail, tailEnd, [](uint8_t byte) { return byte == 0; }));
}

TEST_F(PersistenceFault, PageSerializerRejectsEveryIncompleteRulePageWrite) {
  Page page;
  page.elements.reserve(1);
  page.elements.push_back(std::make_unique<PageHorizontalRule>(100, 1, 17, 18));
  const auto complete = encode(page);
  for (size_t budget = 0; budget < complete.size(); ++budget) {
    SCOPED_TRACE(budget);
    std::vector<uint8_t> bytes;
    bytes.reserve(complete.size());
    HalFile file;
    file.open(bytes);
    io = {};
    faults.writeBudget = budget;
    EXPECT_FALSE(page.serialize(file));
    EXPECT_GT(io.injectedFailures, 0u);
    EXPECT_EQ(budget, bytes.size());
  }
}

TEST_F(PersistenceFault, TextSerializerRejectsShortArenaWrite) {
  auto block = text();
  std::vector<uint8_t> bytes;
  HalFile file;
  file.open(bytes);
  faults.writeBudget = TEXT_HEADER + TEXT_ARENA - 1;
  EXPECT_FALSE(block->serialize(file));
  EXPECT_GT(io.injectedFailures, 0u);
}

TEST_F(PersistenceFault, TextSerializerRejectsEveryIncompleteRubyOrStyleWrite) {
  auto block = text(true);
  const auto complete = encode(*block);
  for (size_t budget = RUBY_LENGTH; budget < complete.size(); ++budget) {
    SCOPED_TRACE(budget);
    std::vector<uint8_t> bytes;
    bytes.reserve(complete.size());
    HalFile file;
    file.open(bytes);
    io = {};
    faults.writeBudget = budget;
    EXPECT_FALSE(block->serialize(file));
    EXPECT_GT(io.injectedFailures, 0u);
    EXPECT_EQ(budget, bytes.size());
  }
}

TEST_F(PersistenceFault, TextDeserializerRejectsEveryTruncatedStyleTail) {
  const auto complete = encode(*text());
  ASSERT_LT(STYLE_OFFSET, complete.size());
  for (size_t size = STYLE_OFFSET; size < complete.size(); ++size) {
    SCOPED_TRACE(size);
    auto bytes = complete;
    bytes.resize(size);
    // Isolate the style tail from the independently tested record header.
    EXPECT_EQ(nullptr, decodeText(bytes));
  }
}

TEST_F(PersistenceFault, TextDeserializerRejectsEveryIncompleteRecordWithoutLeaking) {
  const auto complete = encode(*text(true));
  for (size_t size = 0; size < complete.size(); ++size) {
    SCOPED_TRACE(size);
    auto bytes = complete;
    bytes.resize(size);
    NothrowAllocationScope allocations(0);
    EXPECT_EQ(nullptr, decodeText(bytes));
    EXPECT_EQ(0u, allocations.outstanding());
  }
}

TEST_F(PersistenceFault, TextReadAndWriteStopAtEveryInjectedFieldFailure) {
  auto block = text(true);
  io = {};
  auto complete = encode(*block);
  const size_t writeCalls = io.writes;
  io = {};
  ASSERT_NE(nullptr, decodeText(complete));
  const size_t readCalls = io.reads;
  for (size_t failure = 1; failure <= writeCalls; ++failure) {
    SCOPED_TRACE(failure);
    std::vector<uint8_t> bytes;
    bytes.reserve(complete.size());
    HalFile file;
    file.open(bytes);
    io = {};
    faults = {};
    faults.failWriteCall = failure;
    EXPECT_FALSE(block->serialize(file));
    EXPECT_EQ(1u, io.injectedFailures);
    EXPECT_EQ(failure, io.writes);
  }
  for (size_t failure = 1; failure <= readCalls; ++failure) {
    SCOPED_TRACE(failure);
    io = {};
    faults = {};
    faults.failReadCall = failure;
    NothrowAllocationScope allocations(0);
    EXPECT_EQ(nullptr, decodeText(complete));
    EXPECT_EQ(1u, io.injectedFailures);
    EXPECT_EQ(failure, io.reads);
    EXPECT_EQ(0u, allocations.outstanding());
  }
}

TEST_F(PersistenceFault, TextRejectsNonBooleanFocusAndStyleFlags) {
  const auto complete = encode(*text());
  for (size_t offset :
       {sizeof(uint16_t), STYLE_OFFSET + sizeof(uint8_t), STYLE_OFFSET + 20, STYLE_OFFSET + 21, STYLE_OFFSET + 22}) {
    SCOPED_TRACE(offset);
    for (uint8_t value : {2, 127, 255}) {
      auto bytes = complete;
      bytes[offset] = value;
      EXPECT_EQ(nullptr, decodeText(bytes));
    }
  }
  auto bytes = complete;
  bytes[STYLE_OFFSET] = 0xff;
  EXPECT_EQ(nullptr, decodeText(bytes));
}

TEST_F(PersistenceFault, CheckedPodReadKeepsDestinationOnTruncationAndInvalidBoolean) {
  std::vector<uint8_t> bytes{0xa5};
  HalFile file;
  file.open(bytes);
  uint16_t value = 1234;
  EXPECT_FALSE(serialization::readPodChecked(file, value));
  EXPECT_EQ(1234, value);
  file.open(bytes);
  bool enabled = true;
  EXPECT_FALSE(serialization::readPodChecked(file, enabled));
  EXPECT_TRUE(enabled);
}

TEST_F(PersistenceFault, CheckedStringRejectsLengthsBeforeAllocating) {
  for (uint32_t length : {uint32_t{32}, std::numeric_limits<uint32_t>::max()}) {
    std::vector<uint8_t> bytes(sizeof(length) + 2, 'x');
    patch(bytes, 0, length);
    HalFile file;
    file.open(bytes);
    std::string output = "unchanged";
    allocations = {};
    captureAllocations = true;
    const bool loaded = serialization::readStringChecked(file, output, 64);
    captureAllocations = false;
    EXPECT_FALSE(loaded);
    EXPECT_EQ("unchanged", output);
    EXPECT_EQ(0u, allocations.calls);
  }
}

TEST_F(PersistenceFault, RubyByteLimitIsSharedAcrossAnnotationsOnReadAndWrite) {
  const std::vector<std::string> words{"alpha", "beta"};
  const std::vector<int16_t> positions{0, 55};
  const std::vector<EpdFontFamily::Style> styles(2, EpdFontFamily::REGULAR);
  const size_t firstLength = TextBlock::MAX_RUBY_BYTES / 2;
  std::vector<std::string> rubies{std::string(firstLength, 'a'),
                                  std::string(TextBlock::MAX_RUBY_BYTES - firstLength, 'b')};
  TextBlock block(words, positions, styles, {}, {}, BlockStyle{}, rubies);
  auto bytes = encode(block);
  auto decoded = decodeText(bytes);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(bytes, encode(*decoded));

  const size_t secondLengthOffset = RUBY_LENGTH + sizeof(uint32_t) + firstLength;
  const auto secondLength = static_cast<uint32_t>(rubies[1].size());
  patch(bytes, secondLengthOffset, secondLength + 1);
  bytes.insert(bytes.begin() + secondLengthOffset + sizeof(uint32_t) + secondLength, 'b');
  EXPECT_EQ(nullptr, decodeText(bytes));

  rubies[1].push_back('b');
  TextBlock tooLarge(words, positions, styles, {}, {}, BlockStyle{}, std::move(rubies));
  bytes.clear();
  HalFile output;
  output.open(bytes);
  EXPECT_FALSE(tooLarge.serialize(output));
}

TEST_F(PersistenceFault, PageRejectsEveryIncompleteRecordIncludingAnnotations) {
  Page page;
  page.elements.reserve(2);
  page.elements.push_back(std::make_unique<PageLine>(text(true), 10, 20));
  page.elements.push_back(std::make_unique<PageHorizontalRule>(100, 1, 17, 18));
  page.footnotes.reserve(1);
  page.addFootnote("1", "notes.xhtml#one");
  page.links.reserve(1);
  ASSERT_TRUE(page.addLink("chapter.xhtml#target", 12, 45, 95, 32));
  const auto complete = encode(page);
  for (size_t size = 0; size < complete.size(); ++size) {
    SCOPED_TRACE(size);
    auto bytes = complete;
    bytes.resize(size);
    NothrowAllocationScope allocations(0);
    EXPECT_EQ(nullptr, decodePage(bytes));
    EXPECT_EQ(0u, allocations.outstanding());
  }
}

TEST_F(PersistenceFault, PageRejectsUnterminatedLinkAndFootnoteFields) {
  Page page;
  page.footnotes.reserve(1);
  page.addFootnote("1", "notes.xhtml#one");
  page.links.reserve(1);
  ASSERT_TRUE(page.addLink("chapter.xhtml#target", 12, 45, 95, 32));
  const auto complete = encode(page);
  for (size_t offset :
       {2 * sizeof(uint16_t) + FOOTNOTE_NUMBER_LEN, 3 * sizeof(uint16_t) + FOOTNOTE_NUMBER_LEN + FOOTNOTE_HREF_LEN}) {
    auto bytes = complete;
    std::memset(bytes.data() + offset, 'x', FOOTNOTE_HREF_LEN);
    EXPECT_EQ(nullptr, decodePage(bytes));
  }
  std::memset(page.links[0].href, 'x', sizeof(page.links[0].href));
  std::vector<uint8_t> bytes;
  HalFile file;
  file.open(bytes);
  EXPECT_FALSE(page.serialize(file));
}

TEST_F(PersistenceFault, LongestTerminatedLinkRoundTripsWithCanonicalPadding) {
  Page page;
  page.links.reserve(1);
  const std::string href(FOOTNOTE_HREF_LEN - 1, 'x');
  ASSERT_TRUE(page.addLink(href.c_str(), 12, 45, 95, 32));
  auto bytes = encode(page);
  auto decoded = decodePage(bytes);
  ASSERT_NE(decoded, nullptr);
  ASSERT_EQ(1u, decoded->links.size());
  EXPECT_STREQ(href.c_str(), decoded->links[0].href);
  EXPECT_EQ(bytes, encode(*decoded));
}

TEST_F(PersistenceFault, ImageMetadataRoundTripsAndRejectsEveryIncompleteReadOrWrite) {
  ImageBlock block("/.crosspoint/images/cover.png", "images/cover.png", 100, 200);
  const auto complete = encode(block);
  HalFile input;
  auto bytes = complete;
  input.open(bytes);
  auto decoded = ImageBlock::deserialize(input);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(block.getImagePath(), decoded->getImagePath());
  EXPECT_EQ(100, decoded->getWidth());
  EXPECT_EQ(200, decoded->getHeight());
  EXPECT_EQ(complete, encode(*decoded));
  for (size_t size = 0; size < complete.size(); ++size) {
    SCOPED_TRACE(size);
    faults = {};
    bytes = complete;
    bytes.resize(size);
    input.open(bytes);
    EXPECT_EQ(nullptr, ImageBlock::deserialize(input));
    bytes.clear();
    input.open(bytes);
    io = {};
    faults.writeBudget = size;
    EXPECT_FALSE(block.serialize(input));
    EXPECT_GT(io.injectedFailures, 0u);
  }
}

TEST_F(PersistenceFault, ImagePageRejectsCoordinateFailuresAndOversizedPaths) {
  Page page;
  page.elements.reserve(1);
  page.elements.push_back(
      std::make_unique<PageImage>(std::make_unique<ImageBlock>("cover.png", "images/cover.png", 100, 200), 12, 45));
  auto bytes = encode(page);
  auto decoded = decodePage(bytes);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(bytes, encode(*decoded));
  for (size_t call : {3u, 4u}) {
    io = {};
    faults = {};
    faults.failReadCall = call;
    EXPECT_EQ(nullptr, decodePage(bytes));
    EXPECT_EQ(1u, io.injectedFailures);
    EXPECT_EQ(call, io.reads);
  }
  faults = {};
  patch(bytes, sizeof(uint16_t) + sizeof(uint8_t) + 2 * sizeof(int16_t),
        static_cast<uint32_t>(ImageBlock::MAX_CACHED_PATH_BYTES + 1));
  EXPECT_EQ(nullptr, decodePage(bytes));
}

TEST_F(PersistenceFault, ImagePageReleasesEarlierObjectsOnEveryNothrowFailure) {
  Page page;
  page.elements.reserve(1);
  page.elements.push_back(
      std::make_unique<PageImage>(std::make_unique<ImageBlock>("cover.png", "images/cover.png", 100, 200), 12, 45));
  auto bytes = encode(page);
  size_t count = 0;
  {
    NothrowAllocationScope allocations(0);
    auto decoded = decodePage(bytes);
    ASSERT_NE(decoded, nullptr);
    count = allocations.attempts();
    decoded.reset();
    EXPECT_EQ(0u, allocations.outstanding());
  }
  ASSERT_GE(count, 3u);
  for (size_t failure = 1; failure <= count; ++failure) {
    SCOPED_TRACE(failure);
    NothrowAllocationScope allocations(failure);
    EXPECT_EQ(nullptr, decodePage(bytes));
    EXPECT_EQ(failure, allocations.attempts());
    EXPECT_EQ(0u, allocations.outstanding());
  }
}

TEST_F(PersistenceFault, ImagePathLimitRoundTripsAndRejectsOversizedWriterInput) {
  const std::string path(ImageBlock::MAX_CACHED_PATH_BYTES, 'x');
  ImageBlock block(path, path, 100, 200);
  auto bytes = encode(block);
  HalFile input;
  input.open(bytes);
  auto decoded = ImageBlock::deserialize(input);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(bytes, encode(*decoded));
  ImageBlock tooLarge(path + "x", path, 100, 200);
  bytes.clear();
  input.open(bytes);
  EXPECT_FALSE(tooLarge.serialize(input));
  EXPECT_TRUE(bytes.empty());
}

TEST_F(PersistenceFault, ImageMetadataRejectsNonpositiveDimensionsOnReadAndWrite) {
  ImageBlock valid("cover.png", "images/cover.png", 100, 200);
  const auto complete = encode(valid);
  for (int16_t invalid : {int16_t{0}, int16_t{-1}}) {
    for (bool invalidWidth : {false, true}) {
      auto bytes = complete;
      const auto offset = bytes.size() - sizeof(int16_t) * (invalidWidth ? 2 : 1);
      patch(bytes, offset, invalid);
      HalFile input;
      input.open(bytes);
      EXPECT_EQ(nullptr, ImageBlock::deserialize(input));
      ImageBlock bad("cover.png", "images/cover.png", invalidWidth ? invalid : 100, invalidWidth ? 200 : invalid);
      bytes.clear();
      input.open(bytes);
      EXPECT_FALSE(bad.serialize(input));
      EXPECT_TRUE(bytes.empty());
    }
  }
}

TEST_F(PersistenceFault, TextDeserializerRejectsRubyLengthLargerThanRemainingFile) {
  // One word keeps a second unchecked ruby-length read out of the repro, so
  // only the deliberately bounded length is read.
  const std::vector<std::string> words{"alpha"};
  const std::vector<int16_t> positions{0};
  const std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR};
  const TextBlock block(words, positions, styles, {}, {});
  auto bytes = encode(block);
  const size_t rubyLength = TEXT_HEADER + sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t) + sizeof("alpha");
  patch(bytes, rubyLength, uint32_t{64 * 1024});
  EXPECT_EQ(nullptr, decodeText(bytes));
}

TEST_F(PersistenceFault, TextDeserializerRejectsImpossibleCountsBeforeArenaAllocation) {
  const auto complete = encode(*text());
  for (const auto [words, size] : {std::pair<uint16_t, uint16_t>{10001, 10001}, {0, 1}, {2, 1}}) {
    SCOPED_TRACE(words);
    auto bytes = complete;
    patch(bytes, 0, words);
    patch(bytes, sizeof(uint16_t) + sizeof(uint8_t), size);
    NothrowAllocationScope allocations(0);
    EXPECT_EQ(nullptr, decodeText(bytes));
    EXPECT_EQ(0u, allocations.attempts());
  }
}

TEST_F(PersistenceFault, TextDeserializerRejectsInvalidArenaOffsetsAndTerminators) {
  const auto complete = encode(*text());
  for (int corruption = 0; corruption < 5; ++corruption) {
    SCOPED_TRACE(corruption);
    auto bytes = complete;
    switch (corruption) {
      case 0:
        patch(bytes, TEXT_HEADER, uint16_t{1});
        break;
      case 1:
        patch(bytes, TEXT_HEADER + sizeof(uint16_t), uint16_t{0});
        break;
      case 2:
        patch(bytes, TEXT_HEADER + sizeof(uint16_t), static_cast<uint16_t>(TEXT_BYTES));
        break;
      case 3:
        bytes[TEXT_HEADER + TEXT_ARENA - 1] = 'x';
        break;
      case 4:
        bytes[TEXT_HEADER + TEXT_ARENA - TEXT_BYTES + sizeof("alpha") - 1] = 'x';
        break;
    }
    NothrowAllocationScope allocations(0);
    EXPECT_EQ(nullptr, decodeText(bytes));
    EXPECT_EQ(0u, allocations.outstanding());
  }
}

TEST_F(PersistenceFault, TextDeserializerRejectsShortArenaReadAndReleasesAllocations) {
  auto bytes = encode(*text());
  bytes.resize(TEXT_HEADER + TEXT_ARENA - 1);
  NothrowAllocationScope allocations(0);
  EXPECT_EQ(nullptr, decodeText(bytes));
  EXPECT_EQ(0u, allocations.outstanding());
}

TEST_F(PersistenceFault, PageDecodeRejectsEachNothrowFailureAndReleasesEarlierLines) {
  Page page;
  page.elements.reserve(2);
  page.elements.push_back(std::make_unique<PageLine>(text(), 10, 20));
  page.elements.push_back(std::make_unique<PageLine>(text(), 10, 40));
  auto bytes = encode(page);
  size_t allocationCount = 0;
  {
    NothrowAllocationScope allocations(0);
    auto decoded = decodePage(bytes);
    ASSERT_NE(decoded, nullptr);
    ASSERT_EQ(2u, decoded->elements.size());
    allocationCount = allocations.attempts();
    decoded.reset();
    ASSERT_EQ(0u, allocations.outstanding());
  }
  ASSERT_GT(allocationCount, 3u);
  for (size_t failure = 1; failure <= allocationCount; ++failure) {
    SCOPED_TRACE(failure);
    NothrowAllocationScope allocations(failure);
    EXPECT_EQ(nullptr, decodePage(bytes));
    EXPECT_EQ(failure, allocations.attempts());
    EXPECT_EQ(0u, allocations.outstanding());
  }
}

TEST_F(PersistenceFault, PageDeserializerRejectsOversizedFootnoteAndLinkCounts) {
  const Page empty;
  const auto complete = encode(empty);
  for (size_t offset : {sizeof(uint16_t), 2 * sizeof(uint16_t)}) {
    auto bytes = complete;
    patch(bytes, offset, std::numeric_limits<uint16_t>::max());
    EXPECT_EQ(nullptr, decodePage(bytes));
  }
}

TEST_F(PersistenceFault, RuleDeserializerRejectsFailedCoordinateRead) {
  PageHorizontalRule rule(100, 1, 17, 18);
  auto bytes = encode(rule);
  HalFile file;
  file.open(bytes);
  io = {};
  faults.failReadCall = 1;
  EXPECT_EQ(nullptr, PageHorizontalRule::deserialize(file));
  EXPECT_EQ(1u, io.injectedFailures);
}

TEST_F(PersistenceFault, RuleWriterRejectsZeroDimensionsLikeReader) {
  for (bool zeroWidth : {false, true}) {
    PageHorizontalRule rule(zeroWidth ? 0 : 100, zeroWidth ? 1 : 0, 17, 18);
    std::vector<uint8_t> bytes;
    HalFile file;
    file.open(bytes);
    EXPECT_FALSE(rule.serialize(file));
    EXPECT_TRUE(bytes.empty());
  }
}

TEST_F(PersistenceFault, SectionReaderRejectsFailedOpenAndRequiredOffsetReadsWithoutDecoding) {
  PageTurnFixture fixture(Scene::Prose);
  for (int failure = 0; failure < 5; ++failure) {
    SCOPED_TRACE(failure);
    io = {};
    faults = {};
    faults.failOpen = failure == 0;
    faults.failReadCall = failure;
    NothrowAllocationScope allocations(0);
    EXPECT_EQ(nullptr, SectionPageReader::load("section", 0));
    EXPECT_EQ(1u, io.injectedFailures);
    EXPECT_EQ(0u, allocations.attempts());
  }
}

TEST_F(PersistenceFault, SectionReaderRejectsFailedRequiredSeeksWithoutDecoding) {
  PageTurnFixture fixture(Scene::Prose);
  for (size_t seek : {1u, 2u, 3u, 4u, 5u}) {
    SCOPED_TRACE(seek);
    io = {};
    faults = {};
    faults.failSeekCall = seek;
    NothrowAllocationScope allocations(0);
    EXPECT_EQ(nullptr, SectionPageReader::load("section", 0));
    EXPECT_EQ(1u, io.injectedFailures);
    EXPECT_EQ(0u, allocations.attempts());
  }
}

TEST_F(PersistenceFault, SectionReaderRejectsShortVisibleOffsetReadsWithoutDecoding) {
  PageTurnFixture fixture(Scene::Prose);
  for (size_t read : {3u, 4u}) {
    SCOPED_TRACE(read);
    io = {};
    faults = {};
    faults.shortReadCall = read;
    {
      NothrowAllocationScope allocations(0);
      EXPECT_EQ(nullptr, SectionPageReader::load("section", 0));
      EXPECT_EQ(1u, io.injectedFailures);
      EXPECT_EQ(0u, allocations.attempts());
    }
    faults = {};
    auto retry = SectionPageReader::load("section", 0);
    ASSERT_NE(retry, nullptr);
    EXPECT_EQ(PageTurnFixture::VISIBLE_OFFSET, retry->visibleTextOffset);
  }
}

TEST_F(PersistenceFault, SectionReaderRejectsInvalidVisibleOffsetBoundsWithoutDecoding) {
  PageTurnFixture fixture(Scene::Prose);
  const auto complete = files[0];
  uint32_t lutOffset = 0;
  std::memcpy(&lutOffset, complete.data() + SectionPageReader::HEADER_SIZE - sizeof(uint32_t) * 5, sizeof(lutOffset));
  for (uint32_t offset : {0u, SectionPageReader::HEADER_SIZE - 1, SectionPageReader::HEADER_SIZE, lutOffset,
                          std::numeric_limits<uint32_t>::max(), static_cast<uint32_t>(complete.size() - 3)}) {
    SCOPED_TRACE(offset);
    files[0] = complete;
    patch(files[0], SectionPageReader::HEADER_SIZE - sizeof(uint32_t), offset);
    NothrowAllocationScope allocations(0);
    EXPECT_EQ(nullptr, SectionPageReader::load("section", 0));
    EXPECT_EQ(0u, allocations.attempts());
  }
}

TEST_F(PersistenceFault, SectionReaderAcceptsGenuineZeroVisibleOffset) {
  PageTurnFixture fixture(Scene::Prose);
  uint32_t visibleLutOffset = 0;
  std::memcpy(&visibleLutOffset, files[0].data() + SectionPageReader::HEADER_SIZE - sizeof(uint32_t),
              sizeof(visibleLutOffset));
  patch(files[0], visibleLutOffset, uint32_t{0});
  auto page = SectionPageReader::load("section", 0);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(0u, page->visibleTextOffset);
  EXPECT_FALSE(page->elements.empty());
}

TEST_F(PersistenceFault, SectionReaderRejectsOverflowAndOutOfFileLookupEntries) {
  PageTurnFixture fixture(Scene::Prose);
  const auto complete = files[0];
  EXPECT_EQ(nullptr, SectionPageReader::load("section", -1));
  EXPECT_EQ(nullptr, SectionPageReader::load("section", INT_MAX));
  for (uint32_t offset : {std::numeric_limits<uint32_t>::max(), static_cast<uint32_t>(complete.size() - 3)}) {
    files[0] = complete;
    patch(files[0], SectionPageReader::HEADER_SIZE - sizeof(uint32_t) * 5, offset);
    NothrowAllocationScope allocations(0);
    EXPECT_EQ(nullptr, SectionPageReader::load("section", 0));
    EXPECT_EQ(0u, allocations.attempts());
  }
}
}  // namespace
