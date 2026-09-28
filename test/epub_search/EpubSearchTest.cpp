#include <EpubSearch.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

using namespace epub_search;

namespace {
struct Outcome {
  Results results;
  Status status;
};

Outcome search(const std::string& body, const std::string_view query, size_t chunk = CHUNK_BYTES,
               const bool document = false) {
  auto out = std::make_unique<Outcome>();
  const std::string html = document ? body : "<html><body>" + body + "</body></html>";
  auto parser = std::make_unique<ChapterSearch>(out->results, 7);
  if (!parser->begin(query)) {
    out->status = parser->status();
    return *out;
  }
  for (size_t i = 0; i < html.size();) {
    const size_t count = std::min(chunk, html.size() - i);
    if (parser->write(reinterpret_cast<const uint8_t*>(html.data() + i), count) != count) break;
    i += count;
  }
  out->status = parser->finish();
  return *out;
}
}  // namespace

TEST(EpubSearch, QueryValidationAndBoundedOwnership) {
  EXPECT_FALSE(ChapterSearch::validQuery(""));
  EXPECT_FALSE(ChapterSearch::validQuery(" \n\t"));
  EXPECT_FALSE(ChapterSearch::validQuery(std::string(65, 'a')));
  EXPECT_FALSE(ChapterSearch::validQuery(std::string("a\0b", 3)));
  EXPECT_FALSE(ChapterSearch::validQuery("\xC0\xAF"));
  EXPECT_FALSE(ChapterSearch::validQuery("\xED\xA0\x80"));
  EXPECT_FALSE(ChapterSearch::validQuery("\xF4\x90\x80\x80"));
  EXPECT_TRUE(ChapterSearch::validQuery("你好 café"));
  EXPECT_LE(sizeof(Results), 5600u);
  EXPECT_LE(sizeof(ChapterSearch), 2600u);
}

TEST(EpubSearch, LinkedExpatMatchesFirmwareEntityConfiguration) {
  const auto version = XML_ExpatVersionInfo();
  EXPECT_EQ(version.major, XML_MAJOR_VERSION);
  EXPECT_EQ(version.minor, XML_MINOR_VERSION);
  EXPECT_EQ(version.micro, XML_MICRO_VERSION);
  bool generalEntities = false;
  long contextBytes = 0;
  for (const auto* feature = XML_GetFeatureList(); feature->feature != XML_FEATURE_END; feature++) {
    if (feature->feature == XML_FEATURE_GE) generalEntities = true;
    if (feature->feature == XML_FEATURE_CONTEXT_BYTES) contextBytes = feature->value;
  }
  EXPECT_FALSE(generalEntities);
  EXPECT_EQ(contextBytes, XML_CONTEXT_BYTES);
}

TEST(EpubSearch, AttributeEntitiesKeepTheReadersXmlAcceptanceRules) {
  for (const char* declaration : {"", "<!DOCTYPE html SYSTEM 'https://invalid.example/not-loaded'>"}) {
    const std::string html =
        std::string(declaration) + "<html><body><p title='&nbsp; &quot;quoted&quot; &amp;'>needle</p></body></html>";
    const auto parser = XML_ParserCreate(nullptr);
    ASSERT_NE(parser, nullptr);
    // The reader's fallback receives text entities; attributes still use Expat's validation.
    XML_SetDefaultHandlerExpand(parser, [](void*, const XML_Char*, int) {});
    const bool readerAccepts = XML_Parse(parser, html.data(), static_cast<int>(html.size()), XML_TRUE) == XML_STATUS_OK;
    XML_ParserFree(parser);
    const auto out = search(html, "needle", 1, true);
    EXPECT_EQ(out.status, readerAccepts ? Status::Complete : Status::InvalidInput);
    EXPECT_EQ(out.results.count, readerAccepts ? 1 : 0);
  }
}

TEST(EpubSearch, FindsCaseInsensitiveMatchesAndPreservesSource) {
  const auto out = search("<p>A Captain Nemo and captain Nemo.</p>", "CAPTAIN");
  ASSERT_EQ(out.status, Status::Complete);
  ASSERT_EQ(out.results.count, 2);
  EXPECT_EQ(out.results.items[0].spineIndex, 7);
  EXPECT_EQ(out.results.items[0].visibleTextOffset, 2u);
  EXPECT_EQ(out.results.items[1].visibleTextOffset, 19u);
  EXPECT_NE(std::strstr(out.results.items[0].snippet, "Captain"), nullptr);
  EXPECT_NE(std::strstr(out.results.items[1].snippet, "captain"), nullptr);
}

TEST(EpubSearch, FindsOverlappingAndPrefixFallbackMatches) {
  const auto overlap = search("aaaaa", "aaa", 1);
  ASSERT_EQ(overlap.results.count, 3);
  EXPECT_EQ(overlap.results.items[0].visibleTextOffset, 0u);
  EXPECT_EQ(overlap.results.items[1].visibleTextOffset, 1u);
  EXPECT_EQ(overlap.results.items[2].visibleTextOffset, 2u);
  const auto fallback = search("abababac", "ababac", 1);
  ASSERT_EQ(fallback.results.count, 1);
  EXPECT_EQ(fallback.results.items[0].visibleTextOffset, 2u);
}

TEST(EpubSearch, InlineTagsJoinButParagraphBoundariesSeparate) {
  ASSERT_EQ(search("<p>cap<em>tain</em></p>", "captain").results.count, 1);
  ASSERT_EQ(search("<p>cap</p><p>tain</p>", "captain").results.count, 0);
  const auto out = search("<p>cap</p><p>tain</p>", "cap tain");
  ASSERT_EQ(out.results.count, 1);
  EXPECT_EQ(out.results.items[0].visibleTextOffset, 0u);
}

TEST(EpubSearch, SkipsHeadScriptStyleTitleAndRpWithoutShiftingOffsets) {
  const auto out = search(
      "<html><head><title>needle</title><style>needle</style></head><body>"
      "<script><x>needle</x></script><style>needle</style><rp>needle</rp>hello needle"
      "</body></html>",
      "needle", 1, true);
  ASSERT_EQ(out.status, Status::Complete);
  ASSERT_EQ(out.results.count, 1);
  EXPECT_EQ(out.results.items[0].visibleTextOffset, 6u);
}

TEST(EpubSearch, EntitiesAndUnicodeOffsetsUseCodepoints) {
  const auto out = search("😀 café &amp; &#x4E2D;&#25991; &nbsp;needle", "needle", 1);
  ASSERT_EQ(out.status, Status::Complete);
  ASSERT_EQ(out.results.count, 1);
  EXPECT_EQ(out.results.items[0].visibleTextOffset, 13u);
  const auto entity = search("A &copy; &bogus; &amp; B", "© &bogus; & B", 1);
  ASSERT_EQ(entity.status, Status::Complete);
  ASSERT_EQ(entity.results.count, 1);
  EXPECT_EQ(entity.results.items[0].visibleTextOffset, 2u);
}

TEST(EpubSearch, UnicodeFoldingAndUtf8SnippetsStayWhole) {
  const auto out = search("ééééééééééééééÉCOLE 中文 😀 конец КОНЕЦ", "école", 1);
  ASSERT_EQ(out.results.count, 1);
  EXPECT_EQ(out.results.items[0].visibleTextOffset, 14u);
  EXPECT_EQ(static_cast<unsigned char>(out.results.items[0].snippet[0]), 0xC3);
  EXPECT_EQ(search("КОНЕЦ конец", "конец", 1).results.count, 2);
  EXPECT_EQ(search("中文中文", "中文", 1).results.count, 2);
}

TEST(EpubSearch, WhitespaceNormalizationRetainsRawReaderOffset) {
  const auto out = search(" \r\n\tfoo  \n bar", " foo bar ", 1);
  ASSERT_EQ(out.results.count, 1);
  EXPECT_EQ(out.results.items[0].visibleTextOffset, 3u);  // CRLF becomes one XML character.
  const auto after = search(" \r\n\tfoo  \n bar", "bar", 1);
  ASSERT_EQ(after.results.count, 1);
  EXPECT_EQ(after.results.items[0].visibleTextOffset, 10u);
}

TEST(EpubSearch, HiddenListTextCountsOffsetsButGeneratedMarkersDoNot) {
  const auto out = search("<ol><li>alpha</li><li hidden='hidden'>concealed</li><li>needle</li></ol>", "needle", 1);
  ASSERT_EQ(out.status, Status::Complete);
  ASSERT_EQ(out.results.count, 1);
  EXPECT_EQ(out.results.items[0].visibleTextOffset, 14u);
}

TEST(EpubSearch, QuotedMarkupCommentsPiAndCdataAreCorrectAcrossChunkBoundaries) {
  const std::string body =
      "<p title='a > needle'><!-- > needle <b> --><?ignored needle?>"
      "<![CDATA[visible < needle & text]]></p>";
  for (size_t chunk = 1; chunk <= 31; chunk++) {
    const auto out = search(body, "needle", chunk);
    ASSERT_EQ(out.status, Status::Complete) << chunk;
    ASSERT_EQ(out.results.count, 1) << chunk;
    EXPECT_EQ(out.results.items[0].visibleTextOffset, 10u) << chunk;
  }
}

TEST(EpubSearch, NamespaceDefaultAndUppercaseBody) {
  const auto out =
      search("<HTML xmlns='http://www.w3.org/1999/xhtml'><BODY><P>needle</P></BODY></HTML>", "needle", 1, true);
  ASSERT_EQ(out.status, Status::Complete);
  ASSERT_EQ(out.results.count, 1);
}

TEST(EpubSearch, MissingBodyAndMalformedXmlNeverPublishChapterResults) {
  EXPECT_EQ(search("<html>needle</html>", "needle", 1, true).status, Status::InvalidInput);
  const auto broken = search("<p>needle</b>", "needle", 1);
  EXPECT_EQ(broken.status, Status::InvalidInput);
  EXPECT_EQ(broken.results.count, 0);
  EXPECT_EQ(search("needle &unfinished", "needle").status, Status::InvalidInput);
  EXPECT_EQ(search("needle \xF4\x90\x80\x80", "needle").status, Status::InvalidInput);
}

TEST(EpubSearch, KeepsPriorChapterResultsWhenNextReadFails) {
  auto results = std::make_unique<Results>();
  results->count = 1;
  results->items[0].spineIndex = 1;
  auto parser = std::make_unique<ChapterSearch>(*results, 2);
  ASSERT_TRUE(parser->begin("needle"));
  const std::string html = "<html><body>needle</body></html>";
  parser->write(reinterpret_cast<const uint8_t*>(html.data()), html.size());
  EXPECT_EQ(parser->finish(false), Status::InvalidInput);
  ASSERT_EQ(results->count, 1);
  EXPECT_EQ(results->items[0].spineIndex, 1);
}

TEST(EpubSearch, LimitStopsAtThirtyTwoResultsWithoutGrowingStorage) {
  const auto out = search(std::string(200000, 'a'), "a");
  EXPECT_EQ(out.status, Status::LimitReached);
  EXPECT_EQ(out.results.count, MAX_RESULTS);
  for (uint8_t i = 0; i < MAX_RESULTS; i++) EXPECT_EQ(out.results.items[i].visibleTextOffset, i);
}

TEST(EpubSearch, LargeMarkupAndDeepNestingAreBounded) {
  EXPECT_EQ(search("<!--" + std::string(9000, '>') + "-->", "x").status, Status::LimitReached);
  EXPECT_EQ(search("<p data='" + std::string(9000, 'x') + "'>x</p>", "x").status, Status::LimitReached);
  std::string nested;
  for (unsigned i = 0; i < 70; i++) nested += "<div>";
  nested += "needle";
  for (unsigned i = 0; i < 70; i++) nested += "</div>";
  EXPECT_EQ(search(nested, "needle").status, Status::LimitReached);
}

TEST(EpubSearch, RejectsInternalDtdAndDoesNotLoadExternalFiles) {
  EXPECT_EQ(search("<!DOCTYPE html [<!ENTITY secret SYSTEM 'file:///etc/passwd'>]>"
                   "<html><body>&secret;</body></html>",
                   "root", 1, true)
                .status,
            Status::InvalidInput);
  const auto external = search(
      "<!DOCTYPE html SYSTEM 'http://invalid.example/never-fetch'>"
      "<html><body>needle</body></html>",
      "needle", 1, true);
  ASSERT_EQ(external.status, Status::Complete);
  ASSERT_EQ(external.results.count, 1);
}

TEST(EpubSearch, CancellationStopsWithinOneChunkAndDoesNotKeepWorking) {
  auto results = std::make_unique<Results>();
  unsigned callbacks = 0;
  auto parser = std::make_unique<ChapterSearch>(
      *results, 0, [](void* context) { return ++*static_cast<unsigned*>(context) == 4; }, &callbacks);
  ASSERT_TRUE(parser->begin("needle"));
  const std::string html = "<html><body>" + std::string(100000, 'x') + "</body></html>";
  const size_t consumed = parser->write(reinterpret_cast<const uint8_t*>(html.data()), html.size());
  EXPECT_LE(consumed, 3 * CHUNK_BYTES);
  EXPECT_EQ(parser->finish(), Status::Cancelled);
  EXPECT_EQ(parser->write('x'), 0u);
  EXPECT_EQ(callbacks, 4u);
}

TEST(EpubSearch, UncompressedChapterBudgetStopsZipBombLikeInput) {
  auto results = std::make_unique<Results>();
  auto parser = std::make_unique<ChapterSearch>(*results, 0);
  ASSERT_TRUE(parser->begin("needle"));
  const std::string open = "<html><body>";
  parser->write(reinterpret_cast<const uint8_t*>(open.data()), open.size());
  const std::string block(1024, 'x');
  while (parser->status() == Status::Running)
    parser->write(reinterpret_cast<const uint8_t*>(block.data()), block.size());
  EXPECT_EQ(parser->finish(), Status::LimitReached);
  EXPECT_EQ(parser->bytesRead(), MAX_CHAPTER_BYTES);
}

TEST(EpubSearch, XmlHeapBudgetRejectsSetupOomAndLeavesAllocatorReusable) {
  for (unsigned repeat = 0; repeat < 100; repeat++) {
    auto results = std::make_unique<Results>();
    auto failed = std::make_unique<ChapterSearch>(*results, 0, nullptr, nullptr, 32);
    EXPECT_FALSE(failed->begin("needle"));
    EXPECT_EQ(failed->status(), Status::OutOfMemory);
    EXPECT_EQ(failed->xmlBytesInUse(), 0u);
    EXPECT_LE(failed->peakXmlBytes(), 32u);
    EXPECT_EQ(search("needle", "needle").results.count, 1);
  }
}

TEST(EpubSearch, XmlHeapBudgetStopsUniqueAttributeGrowthAndReleasesEveryAllocation) {
  auto results = std::make_unique<Results>();
  auto parser = std::make_unique<ChapterSearch>(*results, 0);
  ASSERT_TRUE(parser->begin("needle"));
  const std::string open = "<html><body>needle";
  parser->write(reinterpret_cast<const uint8_t*>(open.data()), open.size());
  for (unsigned i = 0; i < 50000 && parser->status() == Status::Running; i++) {
    const std::string tag = "<p unique" + std::to_string(i) + "='x'>x</p>";
    parser->write(reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
  }
  ASSERT_EQ(parser->finish(), Status::OutOfMemory);
  EXPECT_EQ(results->count, 0);
  EXPECT_LE(parser->peakXmlBytes(), MAX_XML_BYTES);
  EXPECT_GT(parser->peakXmlBytes(), 16 * 1024u);
  EXPECT_EQ(parser->xmlBytesInUse(), 0u);
}

TEST(EpubSearch, RepeatedSuccessAndCancellationReleaseXmlHeapImmediately) {
  for (unsigned repeat = 0; repeat < 100; repeat++) {
    auto results = std::make_unique<Results>();
    auto parser = std::make_unique<ChapterSearch>(*results, 0);
    ASSERT_TRUE(parser->begin("needle"));
    const std::string text = "<html><body>needle</body></html>";
    parser->write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    EXPECT_EQ(parser->finish(), Status::Complete);
    EXPECT_EQ(parser->xmlBytesInUse(), 0u);
    EXPECT_GT(parser->peakXmlBytes(), 0u);
    EXPECT_LE(parser->peakXmlBytes(), MAX_XML_BYTES);
    auto cancelled = std::make_unique<ChapterSearch>(*results, 1, [](void*) { return true; });
    ASSERT_TRUE(cancelled->begin("needle"));
    EXPECT_EQ(cancelled->write('x'), 0u);
    EXPECT_EQ(cancelled->finish(), Status::Cancelled);
    EXPECT_EQ(cancelled->xmlBytesInUse(), 0u);
  }
}

TEST(EpubSearch, RemainingBookBudgetIsAnExactInputBound) {
  auto results = std::make_unique<Results>();
  auto parser = std::make_unique<ChapterSearch>(*results, 0, nullptr, nullptr, MAX_XML_BYTES, 100);
  ASSERT_TRUE(parser->begin("needle"));
  const std::string html = "<html><body>" + std::string(2000, 'x') + "</body></html>";
  EXPECT_LE(parser->write(reinterpret_cast<const uint8_t*>(html.data()), html.size()), 100u);
  EXPECT_EQ(parser->finish(), Status::LimitReached);
  EXPECT_EQ(parser->bytesRead(), 100u);
  EXPECT_EQ(parser->xmlBytesInUse(), 0u);
}

TEST(EpubSearch, FinishedParserCannotAccidentallyReuseOldMatcherState) {
  auto results = std::make_unique<Results>();
  auto parser = std::make_unique<ChapterSearch>(*results, 0);
  ASSERT_TRUE(parser->begin("needle"));
  const std::string html = "<html><body>needle</body></html>";
  parser->write(reinterpret_cast<const uint8_t*>(html.data()), html.size());
  ASSERT_EQ(parser->finish(), Status::Complete);
  EXPECT_FALSE(parser->begin("another"));
  EXPECT_EQ(parser->xmlBytesInUse(), 0u);
}
