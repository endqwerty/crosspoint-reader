#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "ContainerParser.h"
#include "ContentOpfParser.h"
#include "Epub/BookMetadataCache.h"
#include "TocNavParser.h"
#include "TocNcxParser.h"

namespace {
template <typename Parser>
void feed(Parser& parser, const std::string& xml, size_t chunk) {
  ASSERT_TRUE(parser.setup());
  for (size_t offset = 0; offset < xml.size(); offset += chunk) {
    const size_t count = std::min(chunk, xml.size() - offset);
    ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + offset), count), count) << offset;
  }
}
class MetadataStreaming : public testing::TestWithParam<size_t> {};
TEST_P(MetadataStreaming, ContainerPathSurvivesLongAttributesAndUtf8Splits) {
  const std::string path = "OEBPS/Été 中文/content.opf";
  const std::string xml = "<container ignored='" + std::string(5000, 'x') +
                          "'><rootfiles><rootfile media-type='application/oebps-package+xml' full-path='" + path +
                          "'/></rootfiles></container>";
  ContainerParser parser(xml.size());
  feed(parser, xml, GetParam());
  EXPECT_EQ(parser.fullPath, path);
}
TEST_P(MetadataStreaming, OpfMetadataKeepsBoundedUtf8TextAndCreators) {
  const std::string label = "Été 中文 &amp; &#x1F4D6; " + std::string(5000, 'x');
  const std::string xml = "<package xmlns:dc='urn:dc'><metadata><dc:title>" + label +
                          "</dc:title><dc:creator>Émile Zola</dc:creator>"
                          "<dc:creator>Ursula Le Guin</dc:creator></metadata></package>";
  const std::string path;
  ContentOpfParser parser(path, path, xml.size(), nullptr);
  feed(parser, xml, GetParam());
  const std::string prefix = "Été 中文 & 📖 ";
  EXPECT_EQ(parser.title, prefix + std::string(512 - prefix.size(), 'x'));
  EXPECT_EQ(parser.author, "Émile Zola, Ursula Le Guin");
}
TEST_P(MetadataStreaming, NavPreservesNestedLongLabelsAndResolvedLinks) {
  const std::string label = std::string(5000, 'x') + " Été 中文";
  const std::string xml =
      "<html xmlns:epub='urn:epub'><body><nav epub:type='toc'><ol><li><a href='../Text/a%20b.xhtml#caf%C3%A9'>" +
      label +
      " &amp; &#x1F4D6;</a><ol><li><a href='second.xhtml'>Second</a>"
      "</li></ol></li></ol></nav></body></html>";
  const std::string base = "OPS/Navigation/";
  BookMetadataCache cache;
  TocNavParser parser(base, xml.size(), &cache);
  feed(parser, xml, GetParam());
  ASSERT_EQ(cache.toc.size(), 2U);
  EXPECT_EQ(cache.toc[0].title, label + " & 📖");
  EXPECT_EQ(cache.toc[0].href, "OPS/Text/a b.xhtml");
  EXPECT_EQ(cache.toc[0].anchor, "café");
  EXPECT_EQ(cache.toc[0].depth, 1);
  EXPECT_EQ(cache.toc[1].title, "Second");
  EXPECT_EQ(cache.toc[1].href, "OPS/Navigation/second.xhtml");
  EXPECT_EQ(cache.toc[1].depth, 2);
}
TEST_P(MetadataStreaming, NcxPreservesNestedLongLabelsAndResolvedLinks) {
  const std::string label = std::string(5000, 'x') + " Été 中文";
  const std::string xml = "<ncx><navMap><navPoint><navLabel><text>" + label +
                          " &amp; &#x1F4D6;</text></navLabel><content src='../Text/a%20b.xhtml#caf%C3%A9'/>"
                          "<navPoint><navLabel><text>Second</text></navLabel><content src='second.xhtml'/>"
                          "</navPoint></navPoint></navMap></ncx>";
  const std::string base = "OPS/Navigation/";
  BookMetadataCache cache;
  TocNcxParser parser(base, xml.size(), &cache);
  feed(parser, xml, GetParam());
  ASSERT_EQ(cache.toc.size(), 2U);
  EXPECT_EQ(cache.toc[0].title, label + " & 📖");
  EXPECT_EQ(cache.toc[0].href, "OPS/Text/a b.xhtml");
  EXPECT_EQ(cache.toc[0].anchor, "café");
  EXPECT_EQ(cache.toc[0].depth, 1);
  EXPECT_EQ(cache.toc[1].title, "Second");
  EXPECT_EQ(cache.toc[1].href, "OPS/Navigation/second.xhtml");
  EXPECT_EQ(cache.toc[1].depth, 2);
}
INSTANTIATE_TEST_SUITE_P(ChunkSizes, MetadataStreaming, testing::Values(1, 17, 1024, 2048));

TEST(MetadataStreamingFailure, MalformedXmlStopsAllMetadataParsers) {
  const std::string xml = "<root><broken></root>";
  const auto* bytes = reinterpret_cast<const uint8_t*>(xml.data());
  const std::string base = "OPS/";
  BookMetadataCache cache;
  ContainerParser container(xml.size());
  ContentOpfParser opf(base, base, xml.size(), &cache);
  TocNavParser nav(base, xml.size(), &cache);
  TocNcxParser ncx(base, xml.size(), &cache);
  for (Print* parser : {static_cast<Print*>(&container), static_cast<Print*>(&opf), static_cast<Print*>(&nav),
                        static_cast<Print*>(&ncx)}) {
    EXPECT_EQ(parser->write(bytes, xml.size()), 0U);
  }
  ASSERT_TRUE(container.setup());
  ASSERT_TRUE(opf.setup());
  ASSERT_TRUE(nav.setup());
  ASSERT_TRUE(ncx.setup());
  for (Print* parser : {static_cast<Print*>(&container), static_cast<Print*>(&opf), static_cast<Print*>(&nav),
                        static_cast<Print*>(&ncx)}) {
    EXPECT_EQ(parser->write(bytes, xml.size()), 0U);
    EXPECT_EQ(parser->write(bytes, xml.size()), 0U);
  }
  EXPECT_TRUE(cache.toc.empty());
}
}  // namespace
