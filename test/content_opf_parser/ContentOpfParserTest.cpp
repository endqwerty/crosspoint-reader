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
  EXPECT_EQ(parser.seriesIndexText, "5");
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

  EXPECT_EQ(parser.seriesIndexText, "16.5");
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
  EXPECT_TRUE(parser.seriesIndexText.empty());
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
  EXPECT_EQ(parser.seriesIndexText, "2");
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
  EXPECT_EQ(parser.seriesIndexText, "3");
}

TEST(ContentOpfParserSeriesEpub3, AcceptsACollectionWithNoDeclaredType) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="c1">Earthsea</meta>
    <meta refines="#c1" property="group-position">2</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Earthsea");
  EXPECT_EQ(parser.seriesIndexText, "2");
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
  EXPECT_EQ(parser.seriesIndexText, "4");
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

TEST(ContentOpfParserSeriesEpub3, TakesTheFirstUntypedCollectionWhenNoneClaimsToBeASeries) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="a">First</meta>
    <meta property="belongs-to-collection" id="b">Second</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "First");
}

TEST(ContentOpfParserSeriesEpub3, FallsBackPastACollectionWhoseNameIsBlank) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="a">   </meta>
    <meta property="belongs-to-collection" id="b">Earthsea</meta>
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
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Earthsea");
  EXPECT_TRUE(parser.seriesIndexText.empty());
}

TEST(ContentOpfParserSeriesEpub3, TrimsTheCollectionName) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="c1">
      The   Wheel of Time
    </meta>
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
  EXPECT_EQ(parser.seriesIndexText, "7");
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
  EXPECT_EQ(parser.seriesIndexText, "5");
}

TEST(ContentOpfParserSeriesPrecedence, FallsBackToEpub3WhenTheCalibreNameIsBlank) {
  const std::string xml = R"(<package><metadata>
    <meta name="calibre:series" content="  "/>
    <meta property="belongs-to-collection" id="c1">Earthsea</meta>
  </metadata></package>)";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.series, "Earthsea");
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
  EXPECT_TRUE(parser.seriesIndexText.empty());
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

TEST(ContentOpfParserBounds, OversizedSeriesDoesNotCollapseIntoAnotherSeriesPrefix) {
  for (const bool attribute : {true, false}) {
    const std::string value(100000, 'a');
    const std::string tag = attribute ? "<meta name=\"calibre:series\" content=\"" + value + "\"/>"
                                      : "<meta property=\"belongs-to-collection\">" + value + "</meta>";
    const std::string xml = "<package><metadata>" + tag + "</metadata></package>";
    const std::string cachePath = "";
    const std::string baseContentPath = "";
    ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);
    parse(parser, xml);
    EXPECT_TRUE(parser.series.empty());
  }
}

TEST(ContentOpfParserBounds, ChunkedWhitespaceAndEntitiesCannotGrowCollectionBeyondItsLimit) {
  const std::string xml = "<package><metadata><meta property=\"belongs-to-collection\">" + std::string(254, 'a') +
                          " &#233; extra</meta></metadata></package>";
  const std::string cachePath = "";
  const std::string baseContentPath = "";
  ContentOpfParser parser(cachePath, baseContentPath, xml.size(), nullptr);
  ASSERT_TRUE(parser.setup());
  for (const unsigned char byte : xml) EXPECT_EQ(parser.write(byte), 1u);
  EXPECT_TRUE(parser.series.empty());
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
      "<package><metadata><meta property=\"belongs-to-collection\">" + value + "</meta></metadata></package>";
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
