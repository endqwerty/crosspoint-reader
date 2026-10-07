#include <gtest/gtest.h>

#include <string>

#include "ContentOpfParser.h"
#include "Epub/BookMetadataCache.h"

namespace {

void parse(ContentOpfParser& parser, const std::string& xml) {
  ASSERT_TRUE(parser.setup());
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
}

}  // namespace

TEST(ContentOpfParserMetadata, EntityCallbackDoesNotSplitOneAuthor) {
  const std::string xml =
      R"(<package xmlns:dc="urn:dc"><metadata><dc:creator>&#201;mile Zola</dc:creator></metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.author, "Émile Zola");
}

TEST(ContentOpfParserMetadata, SeparatesCreatorElementsAndCollapsesXmlWhitespace) {
  const std::string xml = R"(<package xmlns:dc="urn:dc"><metadata>
    <dc:title>  The
   Left Hand   of Darkness  </dc:title>
    <dc:creator> Ursula   K. Le Guin </dc:creator>
    <dc:creator>
Octavia E. Butler
</dc:creator>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.title, "The Left Hand of Darkness");
  EXPECT_EQ(parser.author, "Ursula K. Le Guin, Octavia E. Butler");
}

TEST(ContentOpfParserMetadata, ExtractsIsbnAsinAndCalibreSeries) {
  const std::string xml = R"(<package xmlns:dc="urn:dc" xmlns:opf="urn:opf"><metadata>
    <dc:identifier opf:scheme="ISBN">978-1-4028-9462-6</dc:identifier>
    <dc:identifier opf:scheme="MOBI-ASIN">B0DTT5LV77</dc:identifier>
    <meta name="calibre:series" content="The Expanse"/>
    <meta name="calibre:series_index" content="3.5"/>
  </metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.isbn, "978-1-4028-9462-6");
  EXPECT_EQ(parser.asin, "B0DTT5LV77");
  EXPECT_EQ(parser.series, "The Expanse");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 3.5f);
}

TEST(ContentOpfParserMetadata, ExtractsAmazonSchemeAsin) {
  const std::string xml = R"(<package xmlns:dc="urn:dc" xmlns:opf="urn:opf"><metadata>
    <dc:identifier opf:scheme="AMAZON">B0BF8Y54MS</dc:identifier>
  </metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.asin, "B0BF8Y54MS");
}

TEST(ContentOpfParserMetadata, ExtractsPrefixedIdentifiers) {
  const std::string xml = R"(<package xmlns:dc="urn:dc"><metadata>
    <dc:identifier>ISBN: 9781234567890</dc:identifier>
    <dc:identifier>ASIN: B012345678</dc:identifier>
  </metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.isbn, "9781234567890");
  EXPECT_EQ(parser.asin, "B012345678");
}

TEST(ContentOpfParserMetadata, ExtractsUrnIdentifiers) {
  const std::string xml = R"(<package xmlns:dc="urn:dc"><metadata>
    <dc:identifier>urn:isbn:9781234567890</dc:identifier>
    <dc:identifier>urn:asin:B012345678</dc:identifier>
  </metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.isbn, "9781234567890");
  EXPECT_EQ(parser.asin, "B012345678");
}

TEST(ContentOpfParserMetadata, ClampsOversizedMetadataAttributes) {
  const std::string hugeSeries(64 * 1024, 'S');
  const std::string xml =
      R"(<package><metadata><meta name="calibre:series" content=")" + hugeSeries + R"("/></metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series.size(), 512u);
  EXPECT_EQ(parser.series[0], 'S');
}

TEST(ContentOpfParserMetadata, ExtractsEpub3SeriesCollection) {
  const std::string xml = R"(<package xmlns:dc="urn:dc"><metadata>
    <meta id="series-1" property="belongs-to-collection">Murderbot Diaries</meta>
    <meta refines="#series-1" property="group-position">2</meta>
    <meta refines="#series-1" property="collection-type">series</meta>
  </metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Murderbot Diaries");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 2.0f);
}

TEST(ContentOpfParserMetadata, ResolvesRefinementsBeforeCollectionDeclaration) {
  const std::string xml = R"(<package><metadata>
    <meta refines="#series-a" property="collection-type">series</meta>
    <meta refines="#series-a" property="group-position">7</meta>
    <meta id="series-a" property="belongs-to-collection">Deferred Series</meta>
  </metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Deferred Series");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 7.0f);
}

TEST(ContentOpfParserMetadata, ResolvesInterleavedCollectionRefinementsById) {
  const std::string xml = R"(<package><metadata>
    <meta id="series-a" property="belongs-to-collection">Primary Series</meta>
    <meta id="series-b" property="belongs-to-collection">Secondary Series</meta>
    <meta refines="#series-a" property="collection-type">series</meta>
    <meta refines="#series-a" property="group-position">3</meta>
    <meta refines="#series-b" property="collection-type">series</meta>
    <meta refines="#series-b" property="group-position">9</meta>
  </metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Primary Series");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 3.0f);
}

TEST(ContentOpfParserMetadata, KeepsSeriesIndexWithSelectedMetadataSource) {
  const std::string xml = R"(<package><metadata>
    <meta name="calibre:series" content="Calibre Series"/>
    <meta name="calibre:series_index" content="4"/>
    <meta id="epub-series" property="belongs-to-collection">EPUB Series</meta>
    <meta refines="#epub-series" property="collection-type">series</meta>
    <meta refines="#epub-series" property="group-position">9</meta>
  </metadata></package>)";
  ContentOpfParser parser("", "", xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Calibre Series");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 4.0f);
}

TEST(ContentOpfParserMetadata, StopsBeforeManifestWithoutOpeningTemporaryStorage) {
  const std::string xml = R"(<package xmlns:dc="urn:dc"><metadata>
    <dc:title>A Wizard of Earthsea</dc:title>
    <dc:creator>Ursula K. Le Guin</dc:creator>
    <dc:language>en</dc:language>
  </metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest>
  </package>)";
  Storage = {};
  const std::string cachePath = "/missing-cache";
  const std::string baseContentPath = "OPS/";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr, true);

  ASSERT_TRUE(parser.setup());
  EXPECT_LT(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_EQ(parser.title, "A Wizard of Earthsea");
  EXPECT_EQ(parser.author, "Ursula K. Le Guin");
  EXPECT_EQ(parser.language, "en");
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}

TEST(ContentOpfParserMetadata, NeverEntersManifestWhenMetadataElementIsMissing) {
  const std::string xml =
      R"(<package><manifest><item id="chapter" href="chapter.xhtml"/></manifest><spine/></package>)";
  Storage = {};
  const std::string cachePath = "/missing-cache";
  const std::string baseContentPath = "OPS/";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr, true);

  ASSERT_TRUE(parser.setup());
  EXPECT_LT(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}

TEST(ContentOpfParserSeriesCalibre, ReadsNameAndIndex) {
  const std::string xml = R"(<package><metadata>
    <meta name="calibre:series" content="Discworld"/>
    <meta name="calibre:series_index" content="5"/>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Discworld");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 5.0f);
}

TEST(ContentOpfParserSeriesCalibre, ReadsAFractionalIndex) {
  const std::string xml = R"(<package><metadata>
    <meta name="calibre:series" content="Discworld"/>
    <meta name="calibre:series_index" content="16.5"/>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 16.5f);
}

TEST(ContentOpfParserSeriesCalibre, SurvivesAMissingIndex) {
  const std::string xml = R"(<package><metadata>
    <meta name="calibre:series" content="Discworld"/>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Discworld");
  EXPECT_FALSE(parser.seriesIndex.has_value());
}

TEST(ContentOpfParserSeriesCalibre, DecodesEntitiesInTheName) {
  const std::string xml = R"(<package><metadata>
    <meta name="calibre:series" content="Fire &amp; Blood"/>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Fire & Blood");
}

TEST(ContentOpfParserSeriesCalibre, IgnoresACommentedOutMeta) {
  const std::string xml = R"(<package><metadata>
    <!-- <meta name="calibre:series" content="Ghost Series"/> -->
    <meta name="calibre:series" content="Discworld"/>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Discworld");
}

TEST(ContentOpfParserSeriesCalibre, ToleratesARawGreaterThanInAnAttributeValue) {
  const std::string xml = R"(<package><metadata>
    <meta name="calibre:series" content="A > B"/>
    <meta name="calibre:series_index" content="2"/>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "A > B");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 2.0f);
}

TEST(ContentOpfParserSeriesEpub3, ReadsCollectionAndGroupPosition) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="c1">The Wheel of Time</meta>
    <meta refines="#c1" property="collection-type">series</meta>
    <meta refines="#c1" property="group-position">3</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "The Wheel of Time");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 3.0f);
}

TEST(ContentOpfParserSeriesEpub3, IgnoresACollectionWithNoDeclaredType) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="c1">Earthsea</meta>
    <meta refines="#c1" property="group-position">2</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_TRUE(parser.series.empty());
  EXPECT_FALSE(parser.seriesIndex.has_value());
}

TEST(ContentOpfParserSeriesEpub3, AcceptsAMiscasedCollectionType) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="c1">Earthsea</meta>
    <meta refines="#c1" property="collection-type">Series</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Earthsea");
}

TEST(ContentOpfParserSeriesEpub3, IgnoresABoxedSet) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="c1">Complete Works</meta>
    <meta refines="#c1" property="collection-type">set</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_TRUE(parser.series.empty());
}

TEST(ContentOpfParserSeriesEpub3, PrefersTheSeriesOverABoxedSetDeclaredBeforeIt) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="box">Complete Works</meta>
    <meta refines="#box" property="collection-type">set</meta>
    <meta property="belongs-to-collection" id="ser">Earthsea</meta>
    <meta refines="#ser" property="collection-type">series</meta>
    <meta refines="#ser" property="group-position">4</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Earthsea");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 4.0f);
}

TEST(ContentOpfParserSeriesEpub3, PrefersAnExplicitSeriesOverAnUntypedCollection) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="a">Some Anthology</meta>
    <meta property="belongs-to-collection" id="b">Earthsea</meta>
    <meta refines="#b" property="collection-type">series</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Earthsea");
}

TEST(ContentOpfParserSeriesEpub3, IgnoresUntypedCollections) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="a">First</meta>
    <meta property="belongs-to-collection" id="b">Second</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_TRUE(parser.series.empty());
}

TEST(ContentOpfParserSeriesEpub3, FallsBackPastACollectionWhoseNameIsBlank) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="a">   </meta>
    <meta property="belongs-to-collection" id="b">Earthsea</meta>
    <meta refines="#b" property="collection-type">series</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Earthsea");
}

TEST(ContentOpfParserSeriesEpub3, DoesNotTakeAPositionThatRefinesSomethingElse) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="c1">Earthsea</meta>
    <meta refines="#other" property="group-position">9</meta>
    <meta refines="#c1" property="collection-type">series</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Earthsea");
  EXPECT_FALSE(parser.seriesIndex.has_value());
}

TEST(ContentOpfParserSeriesEpub3, TrimsTheCollectionName) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="c1">
      The   Wheel of Time
    </meta>
    <meta refines="#c1" property="collection-type">series</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "The Wheel of Time");
}

TEST(ContentOpfParserSeriesEpub3, ResolvesARefineThatPrecedesItsCollection) {
  const std::string xml = R"(<package><metadata>
    <meta refines="#c1" property="collection-type">series</meta>
    <meta refines="#c1" property="group-position">7</meta>
    <meta property="belongs-to-collection" id="c1">Earthsea</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Earthsea");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 7.0f);
}

TEST(ContentOpfParserSeriesPrecedence, CalibreWinsWhenABookCarriesBoth) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="c1">Publisher Collection</meta>
    <meta refines="#c1" property="group-position">9</meta>
    <meta name="calibre:series" content="Discworld"/>
    <meta name="calibre:series_index" content="5"/>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Discworld");
  ASSERT_TRUE(parser.seriesIndex.has_value());
  EXPECT_FLOAT_EQ(*parser.seriesIndex, 5.0f);
}

TEST(ContentOpfParserSeriesPrecedence, KeepsCalibreSourceEvenWhenWhitespaceOnly) {
  const std::string xml = R"(<package><metadata>
    <meta name="calibre:series" content="  "/>
    <meta property="belongs-to-collection" id="c1">Earthsea</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "  ");
}

TEST(ContentOpfParserSeriesAbsent, LeavesTheFieldsEmpty) {
  const std::string xml = R"(<package xmlns:dc="urn:dc"><metadata>
    <dc:title>A Standalone</dc:title>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_TRUE(parser.series.empty());
  EXPECT_FALSE(parser.seriesIndex.has_value());
}

TEST(ContentOpfParserSeriesAbsent, DoesNotDisturbTitleOrAuthor) {
  const std::string xml = R"(<package xmlns:dc="urn:dc"><metadata>
    <dc:title>Small Gods</dc:title>
    <dc:creator>Terry Pratchett</dc:creator>
    <meta name="calibre:series" content="Discworld"/>
    <meta name="calibre:series_index" content="13"/>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.title, "Small Gods");
  EXPECT_EQ(parser.author, "Terry Pratchett");
  EXPECT_EQ(parser.series, "Discworld");
}

TEST(ContentOpfParserBounds, SyncSeriesTextRemainsBounded) {
  for (const bool attribute : {true, false}) {
    const std::string value(100000, 'a');
    const std::string tag = attribute
                                ? "<meta name=\"calibre:series\" content=\"" + value + "\"/>"
                                : "<meta property=\"belongs-to-collection\" id=\"series\">" + value +
                                      "</meta><meta refines=\"#series\" property=\"collection-type\">series</meta>";
    const std::string xml = "<package><metadata>" + tag + "</metadata></package>";
    const std::string cachePath = "";
    const std::string baseContentPath = "";
    ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);
    parse(parser, xml);
    EXPECT_EQ(parser.series, value.substr(0, 512));
  }
}

TEST(ContentOpfParserBounds, ChunkedWhitespaceAndEntitiesCannotGrowCollectionBeyondItsLimit) {
  const std::string xml =
      "<package><metadata><meta property=\"belongs-to-collection\" id=\"series\">" + std::string(510, 'a') +
      " &#233; extra</meta><meta refines=\"#series\" property=\"collection-type\">series</meta></metadata></package>";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);
  ASSERT_TRUE(parser.setup());
  for (const unsigned char byte : xml) EXPECT_EQ(parser.write(byte), 1u);
  EXPECT_LE(parser.series.size(), 512u);
}

TEST(ContentOpfParserBounds, LargeTitleAndAuthorHaveBoundedMetadataResults) {
  const std::string xml = "<package xmlns:dc=\"urn:dc\"><metadata><dc:title>" + std::string(100000, 'a') +
                          "</dc:title><dc:creator>" + std::string(100000, 'b') + "</dc:creator></metadata></package>";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);
  parse(parser, xml);
  EXPECT_LE(parser.title.size(), 512u);
  EXPECT_LE(parser.author.size(), 512u);
}

TEST(ContentOpfParserBounds, FullLengthCollectionRemainsAvailable) {
  const std::string value = std::string(253, 'a') + "é";
  const std::string xml =
      "<package><metadata><meta property=\"belongs-to-collection\" id=\"series\">" + value +
      "</meta><meta refines=\"#series\" property=\"collection-type\">series</meta></metadata></package>";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);
  parse(parser, xml);
  EXPECT_EQ(parser.series, value);
}

TEST(ContentOpfParserMetadata, ClampsOversizedMetadataTextInsteadOfGrowingUnbounded) {
  const std::string hugeTitle(64 * 1024, 'A');
  const std::string xml =
      "<package xmlns:dc=\"urn:dc\"><metadata><dc:title>" + hugeTitle + " tail</dc:title></metadata></package>";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.title.size(), 512u);
  EXPECT_EQ(parser.title[0], 'A');
}

TEST(ContentOpfParserBounds, TruncationStaysAtTheFirstUnrepresentableGlyphAcrossCallbacks) {
  const std::string prefix(511, 'a');
  const std::string xml = "<package xmlns:dc=\"urn:dc\"><metadata><dc:title>" + prefix +
                          "&#x1F600;Z</dc:title><dc:creator>" + prefix +
                          "&#x1F600;Z</dc:creator><dc:creator>Later author</dc:creator><dc:language>" + prefix +
                          "&#x1F600;Z</dc:language></metadata></package>";
  for (const size_t chunk : {1u, 7u, 128u, 4096u}) {
    SCOPED_TRACE(chunk);
    const std::string cachePath = "";
    const std::string baseContentPath = "";
    ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);
    ASSERT_TRUE(parser.setup());
    for (size_t offset = 0; offset < xml.size(); offset += chunk) {
      const size_t count = std::min(chunk, xml.size() - offset);
      ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + offset), count), count);
    }
    EXPECT_EQ(parser.title, prefix);
    EXPECT_EQ(parser.author, prefix);
    EXPECT_EQ(parser.language, prefix);
  }
}

TEST(ContentOpfParserCover, ResolvesEpub2CoverWithoutReadingCacheStorage) {
  const std::string xml = R"(<package><metadata><meta name="cover" content="cover-id"/></metadata>
    <manifest><item id="cover-id" href="cover.jpg" media-type="image/jpeg"/>
    <item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest>
    <spine><itemref idref="chapter"/></spine>
    <guide><reference type="cover" href="cover.xhtml"/></guide></package>)";
  const std::string cachePath = "/missing-cache";
  const std::string basePath = "OPS/";
  Storage = {};
  {
    ContentOpfParser parser(cachePath, basePath, xml.size(), nullptr);
    parse(parser, xml);
    EXPECT_EQ(parser.coverItemHref, "OPS/cover.jpg");
    EXPECT_EQ(parser.guideCoverPageHref, "OPS/cover.xhtml");
  }
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}

TEST(ContentOpfParserCover, ResolvesEpub3CoverWithoutReadingCacheStorage) {
  const std::string xml = R"(<package><metadata/>
    <manifest><item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/></manifest>
    <spine/></package>)";
  const std::string cachePath = "/missing-cache";
  const std::string basePath = "OPS/";
  Storage = {};
  {
    ContentOpfParser parser(cachePath, basePath, xml.size(), nullptr);
    parse(parser, xml);
    EXPECT_EQ(parser.coverItemHref, "OPS/cover.png");
  }
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}

TEST(ContentOpfParserCover, ReadingParserStillOpensManifestCache) {
  const std::string xml = R"(<package><metadata/>
    <manifest><item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/></manifest>
    <spine/></package>)";
  const std::string cachePath = "/reading-cache";
  const std::string basePath = "OPS/";
  BookMetadataCache cache;
  Storage = {};
  {
    ContentOpfParser parser(cachePath, basePath, xml.size(), &cache);
    parse(parser, xml);
    EXPECT_EQ(parser.coverItemHref, "OPS/cover.png");
  }
  EXPECT_EQ(Storage.writeOpens, 1);
  EXPECT_EQ(Storage.readOpens, 1);
}

namespace {
struct ParsedSortKeys {
  std::string titleSort, authorSort, uuid;
};

ParsedSortKeys parseSortKeys(const std::string& metadata) {
  const std::string xml = "<package><metadata>" + metadata + "</metadata></package>";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);
  parse(parser, xml);
  return {parser.titleSort, parser.authorSort, parser.uuid};
}
}  // namespace

TEST(ContentOpfParserSortKeys, ReadsCalibreEpub2SortKeysAndLibraryUuid) {
  // As written by calibre 9.14 "Save to disk". The "calibre"-scheme UUID is not
  // stable across exports; the uuid-scheme identifier is the library's book UUID.
  const auto keys = parseSortKeys(R"(
    <dc:title>The Narrow Corridor</dc:title>
    <dc:creator opf:file-as="Acemoglu, Daron &amp; Robinson, James A." opf:role="aut">Daron Acemoglu</dc:creator>
    <dc:creator opf:role="aut">James A. Robinson</dc:creator>
    <dc:identifier opf:scheme="calibre" id="calibre_id">f58c0ee7-0f39-4de0-be9f-4e9660b46335</dc:identifier>
    <dc:identifier opf:scheme="uuid" id="uuid_id">1731E1CA-38A6-47DA-9C5B-6F324AB6A3BF</dc:identifier>
    <meta name="calibre:title_sort" content="Narrow Corridor, The"/>)");
  EXPECT_EQ(keys.titleSort, "Narrow Corridor, The");
  EXPECT_EQ(keys.authorSort, "Acemoglu, Daron & Robinson, James A.");
  EXPECT_EQ(keys.uuid, "1731e1ca-38a6-47da-9c5b-6f324ab6a3bf");
}

TEST(ContentOpfParserSortKeys, ReadsEpub3RefinesButOnlyAFirstCreatorAuthorSorts) {
  // The complete metadata of a real calibre-exported EPUB 3: the author is the
  // fourth creator, and ten refines describe the others before reaching hers.
  // The Library's author string starts with the illustrator, so her sort would
  // head a group it does not describe.
  const auto keys = parseSortKeys(R"(
    <dc:title id="id">Proud to Be the Villainess: Volume 1</dc:title>
    <dc:creator id="creator02">Kuga Huna</dc:creator>
    <dc:creator id="creator03">Bérénice Vourdon</dc:creator>
    <dc:creator id="creator04">Emlyn Dornemann</dc:creator>
    <dc:creator id="id-2">Mary=Doe</dc:creator>
    <dc:rights>©2023 Mary=Doe, Kuga Huna/SQUARE ENIX CO., LTD.</dc:rights>
    <dc:identifier>calibre:5</dc:identifier>
    <dc:identifier>uuid:3F04EE43-A503-46BE-9C5E-4A43941BD7A3</dc:identifier>
    <dc:identifier id="pub-id">9781718396357</dc:identifier>
    <dc:language>en</dc:language>
    <dc:contributor id="id-1">calibre (9.14.0) [https://calibre-ebook.com]</dc:contributor>
    <opf:meta refines="#id" property="title-type">main</opf:meta>
    <opf:meta refines="#id" property="file-as">Proud to Be the Villainess: Volume 1</opf:meta>
    <meta refines="#creator02" property="display-seq">2</meta>
    <meta refines="#creator02" property="file-as">HUNA, KUGA</meta>
    <meta refines="#creator02" scheme="marc:relators" property="role">ill</meta>
    <meta refines="#creator03" property="display-seq">3</meta>
    <meta refines="#creator03" property="file-as">VOURDON, BÉRÉNICE</meta>
    <meta refines="#creator03" scheme="marc:relators" property="role">trl</meta>
    <meta refines="#creator04" property="display-seq">4</meta>
    <meta refines="#creator04" property="file-as">DORNEMANN, EMLYN</meta>
    <meta refines="#creator04" scheme="marc:relators" property="role">edt</meta>
    <meta refines="#pub-id" scheme="onix:codelist5" property="identifier-type">15</meta>
    <meta property="dcterms:modified">2026-04-09T12:21:15Z</meta>
    <meta name="cover" content="Cover_jpg"/>
    <opf:meta refines="#id-1" property="role" scheme="marc:relators">bkp</opf:meta>
    <opf:meta refines="#id-2" property="role" scheme="marc:relators">aut</opf:meta>
    <opf:meta refines="#id-2" property="file-as">MARY=DOE</opf:meta>)");
  EXPECT_EQ(keys.titleSort, "Proud to Be the Villainess: Volume 1");
  EXPECT_TRUE(keys.authorSort.empty());
  EXPECT_EQ(keys.uuid, "3f04ee43-a503-46be-9c5e-4a43941bd7a3");
}

TEST(ContentOpfParserSortKeys, ResolvesRefinesThatPrecedeTheirCreators) {
  const auto keys = parseSortKeys(R"(
    <meta refines="#illustrator" property="role">ill</meta>
    <meta refines="#author" property="role">aut</meta>
    <meta refines="#author" property="file-as">Le Guin, Ursula K.</meta>
    <meta refines="#title" property="file-as">Wizard of Earthsea, A</meta>
    <dc:title id="title">A Wizard of Earthsea</dc:title>
    <dc:creator id="author">Ursula K. Le Guin</dc:creator>
    <dc:creator id="illustrator">Ruth Robbins</dc:creator>)");
  EXPECT_EQ(keys.titleSort, "Wizard of Earthsea, A");
  EXPECT_EQ(keys.authorSort, "Le Guin, Ursula K.");
}

TEST(ContentOpfParserSortKeys, TitleFileAsOutranksCalibreTitleSort) {
  const auto keys = parseSortKeys(R"(
    <meta name="calibre:title_sort" content="Hobbit, The"/>
    <dc:title opf:file-as="Hobbit"> The Hobbit </dc:title>
    <dc:creator>J. R. R. Tolkien</dc:creator>
    <dc:identifier>urn:uuid:0f3c2b1a-0000-4000-8000-00000000000a</dc:identifier>)");
  EXPECT_EQ(keys.titleSort, "Hobbit");
  EXPECT_TRUE(keys.authorSort.empty());
  EXPECT_EQ(keys.uuid, "0f3c2b1a-0000-4000-8000-00000000000a");
}

TEST(ContentOpfParserSortKeys, OnlyTheFirstCreatorNamesTheAuthorSort) {
  const auto keys = parseSortKeys(R"(
    <dc:creator opf:role="aut">First Author</dc:creator>
    <dc:creator opf:role="aut" opf:file-as="Author, Second">Second Author</dc:creator>)");
  EXPECT_TRUE(keys.authorSort.empty());
  const auto illustrated = parseSortKeys(R"(
    <dc:creator opf:role="ill" opf:file-as="Baynes, Pauline">Pauline Baynes</dc:creator>
    <dc:creator opf:role="aut" opf:file-as="Tolkien, J. R. R.">J. R. R. Tolkien</dc:creator>)");
  EXPECT_TRUE(illustrated.authorSort.empty());
}

TEST(ContentOpfParserSortKeys, IgnoresIdentifiersThatAreNotTheBookUuid) {
  const auto keys = parseSortKeys(R"(
    <dc:identifier opf:scheme="calibre">f58c0ee7-0f39-4de0-be9f-4e9660b46335</dc:identifier>
    <dc:identifier opf:scheme="ISBN">9781718396357</dc:identifier>
    <dc:identifier opf:scheme="uuid">not-a-uuid</dc:identifier>
    <dc:identifier opf:scheme="uuid">1731e1ca-38a6-47da-9c5b-6f324ab6a3bf-extra</dc:identifier>)");
  EXPECT_TRUE(keys.uuid.empty());
  EXPECT_TRUE(keys.titleSort.empty());
}

TEST(ContentOpfParserSortKeys, UuidSchemeOutranksAPrefixedIdentifierInEitherOrder) {
  const auto keys = parseSortKeys(R"(
    <dc:identifier>urn:uuid:aaaaaaaa-0000-4000-8000-000000000000</dc:identifier>
    <dc:identifier id="uuid_id">bbbbbbbb-0000-4000-8000-000000000000</dc:identifier>
    <dc:identifier>urn:uuid:cccccccc-0000-4000-8000-000000000000</dc:identifier>)");
  EXPECT_EQ(keys.uuid, "bbbbbbbb-0000-4000-8000-000000000000");
}

TEST(ContentOpfParserBounds, SyncFieldsDoNotResumeAfterDroppedGlyphAcrossCallbacks) {
  const std::string prefix(510, 'a');
  const std::string xml = "<package><metadata><identifier scheme='ISBN'>" + prefix +
                          "&#x1f600;x</identifier>"
                          "<meta property='belongs-to-collection' id='s'>" +
                          prefix +
                          "&#x1f600;x</meta>"
                          "<meta property='collection-type' refines='#s'>series</meta></metadata></package>";
  const std::string path;
  ContentOpfParser parser(path, path, xml.size(), nullptr);
  ASSERT_TRUE(parser.setup());
  for (size_t offset = 0; offset < xml.size(); offset += 7) {
    const size_t length = std::min(size_t{7}, xml.size() - offset);
    ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + offset), length), length);
  }
  EXPECT_EQ(parser.isbn, prefix);
  EXPECT_EQ(parser.series, prefix);
}
