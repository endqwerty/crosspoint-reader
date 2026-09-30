#include <gtest/gtest.h>

#include <array>

#include "EpubIndexingFixture.h"

namespace {
using Phase = xml_fault::Phase;
void prepare(bool nav, bool ncx, int chapters = 3) {
  xml_fault::reset();
  Storage.files.clear();
  storageFaults = {};
  index_test::documents.clear();
  index_test::streamFailed = {};
  zipModel = {};
  const std::string padding(8192, 'x');
  std::string opf = "<package><metadata><title>Fixture</title></metadata><manifest>";
  if (nav) opf += "<item id='nav' href='nav.xhtml' media-type='application/xhtml+xml' properties='nav'/>";
  if (ncx) opf += "<item id='ncx' href='toc.ncx' media-type='application/x-dtbncx+xml'/>";
  for (int i = 1; i <= chapters; ++i) {
    const auto name = "c" + std::to_string(i);
    opf += "<item id='" + name + "' href='" + name + ".xhtml' media-type='application/xhtml+xml'/>";
    index_test::documents["OEBPS/" + name + ".xhtml"] = "<html><body>Chapter</body></html>";
  }
  opf += ncx ? "</manifest><spine toc='ncx'>" : "</manifest><spine>";
  for (int i = 1; i <= chapters; ++i) {
    opf += "<itemref idref='c" + std::to_string(i) + "'" + (i == 2 ? " data-padding='" + padding + "'" : "") + "/>";
  }
  opf += "</spine></package>";
  index_test::documents["OEBPS/content.opf"] = opf;
  std::string html = "<html xmlns:epub='http://www.idpf.org/2007/ops'><body><nav epub:type='toc'><ol>";
  std::string ncxXml = "<ncx><navMap>";
  for (int i = 1; i <= chapters; ++i) {
    const auto chapter = std::to_string(i);
    html += "<li" + (i == 2 ? " data-padding='" + padding + "'" : "") + "><a href='c" + chapter + ".xhtml'>NAV " +
            chapter + "</a></li>";
    ncxXml += "<navPoint" + (i == 2 ? " data-padding='" + padding + "'" : "") + "><navLabel><text>NCX " + chapter +
              "</text></navLabel><content src='c" + chapter + ".xhtml'/></navPoint>";
  }
  html += "</ol></nav></body></html>";
  ncxXml += "</navMap></ncx>";
  if (nav) index_test::documents["OEBPS/nav.xhtml"] = html;
  if (ncx) index_test::documents["OEBPS/toc.ncx"] = ncxXml;
  zipModel.entries.reserve(index_test::documents.size());
  for (const auto& [path, data] : index_test::documents) zipModel.entries.emplace_back(path, data.size());
}
void expectNoXmlLeaks() {
  for (auto phase : {Phase::Opf, Phase::Nav, Phase::Ncx}) {
    EXPECT_EQ(xml_fault::stats(phase).liveBytes, 0u);
    EXPECT_EQ(xml_fault::stats(phase).liveBlocks, 0u);
  }
}
void expectNoOpenFiles() {
  for (const auto& [path, file] : Storage.files) EXPECT_EQ(file.use_count(), 1) << path;
}
void expectToc(Epub& book, const char* label, int chapters = 3) {
  ASSERT_TRUE(book.bookMetadataCache);
  auto& cache = *book.bookMetadataCache;
  ASSERT_EQ(cache.getSpineCount(), chapters);
  ASSERT_EQ(cache.getTocCount(), label ? chapters : 0);
  if (!label) {
    for (int i = 0; i < chapters; ++i) EXPECT_EQ(cache.getSpineEntry(i).tocIndex, -1);
    return;
  }
  for (int i = 0; i < chapters; ++i) {
    const auto entry = cache.getTocEntry(i);
    EXPECT_EQ(entry.title, std::string(label) + " " + std::to_string(i + 1));
    EXPECT_EQ(entry.href, "OEBPS/c" + std::to_string(i + 1) + ".xhtml");
    EXPECT_EQ(entry.spineIndex, i);
    EXPECT_EQ(cache.getSpineEntry(i).tocIndex, i);
  }
}
size_t baselineCalls(bool nav, bool ncx, Phase phase) {
  prepare(nav, ncx);
  Epub book;
  EXPECT_TRUE(book.load());
  expectToc(book, nav ? "NAV" : "NCX");
  expectNoXmlLeaks();
  const auto calls = xml_fault::stats(phase).calls;
  printf("XML_OOM_MATRIX nav=%d ncx=%d phase=%d allocation_points=%zu\n", nav, ncx, static_cast<int>(phase), calls);
  return calls;
}
}  // namespace

TEST(EpubIndexingOom, OpfAllocationFailuresDoNotPublishAndRetryWithoutClearingStaging) {
  const auto calls = baselineCalls(true, true, Phase::Opf);
  ASSERT_GT(calls, 10u);
  for (size_t failure = 1; failure <= calls; ++failure) {
    SCOPED_TRACE(failure);
    prepare(true, true);
    xml_fault::failAt(Phase::Opf, failure);
    {
      Epub book;
      const bool loaded = book.load();
      ASSERT_EQ(xml_fault::stats(Phase::Opf).failures, 1u);
      expectNoXmlLeaks();
      if (loaded) {
        expectToc(book, "NAV");
      } else {
        EXPECT_FALSE(book.bookMetadataCache);
        EXPECT_FALSE(book.cssParser);
        EXPECT_FALSE(Storage.exists("/cache/book.bin"));
        expectNoOpenFiles();
      }
      xml_fault::reset();
      index_test::streamFailed = {};
      ASSERT_TRUE(book.load());
      expectToc(book, "NAV");
      expectNoXmlLeaks();
      EXPECT_FALSE(Storage.exists("/cache/.items.bin"));
      EXPECT_FALSE(Storage.exists("/cache/spine.bin.tmp"));
      EXPECT_FALSE(Storage.exists("/cache/toc.bin.tmp"));
    }
    expectNoOpenFiles();
  }
}

TEST(EpubIndexingOom, NavAllocationFailureFallsBackWithoutPartialNavEntries) {
  const auto calls = baselineCalls(true, true, Phase::Nav);
  ASSERT_GT(calls, 10u);
  for (size_t failure = 1; failure <= calls; ++failure) {
    SCOPED_TRACE(failure);
    prepare(true, true);
    xml_fault::failAt(Phase::Nav, failure);
    {
      Epub book;
      ASSERT_TRUE(book.load());
      ASSERT_EQ(xml_fault::stats(Phase::Nav).failures, 1u);
      expectToc(book, index_test::parserFailed(Phase::Nav) ? "NCX" : "NAV");
      expectNoXmlLeaks();
    }
    expectNoOpenFiles();
  }
}

TEST(EpubIndexingOom, NavAllocationFailureWithoutNcxPublishesNoPartialToc) {
  const auto calls = baselineCalls(true, false, Phase::Nav);
  for (size_t failure = 1; failure <= calls; ++failure) {
    SCOPED_TRACE(failure);
    prepare(true, false);
    xml_fault::failAt(Phase::Nav, failure);
    Epub book;
    ASSERT_TRUE(book.load());
    ASSERT_EQ(xml_fault::stats(Phase::Nav).failures, 1u);
    expectToc(book, index_test::parserFailed(Phase::Nav) ? nullptr : "NAV");
    expectNoXmlLeaks();
  }
}

TEST(EpubIndexingOom, NcxAllocationFailurePublishesNoPartialToc) {
  const auto calls = baselineCalls(false, true, Phase::Ncx);
  ASSERT_GT(calls, 10u);
  for (size_t failure = 1; failure <= calls; ++failure) {
    SCOPED_TRACE(failure);
    prepare(false, true);
    xml_fault::failAt(Phase::Ncx, failure);
    Epub book;
    ASSERT_TRUE(book.load());
    ASSERT_EQ(xml_fault::stats(Phase::Ncx).failures, 1u);
    expectToc(book, index_test::parserFailed(Phase::Ncx) ? nullptr : "NCX");
    expectNoXmlLeaks();
  }
}

TEST(EpubIndexingOom, FailedNavAndNcxLeaveReadableBookWithoutPartialToc) {
  const auto calls = baselineCalls(true, true, Phase::Nav);
  for (size_t failure = 1; failure <= calls; ++failure) {
    SCOPED_TRACE(failure);
    prepare(true, true);
    xml_fault::failAt(Phase::Nav, failure);
    xml_fault::failAt(Phase::Ncx, 1);
    Epub book;
    ASSERT_TRUE(book.load());
    ASSERT_EQ(xml_fault::stats(Phase::Nav).failures, 1u);
    if (index_test::parserFailed(Phase::Nav)) {
      EXPECT_EQ(xml_fault::stats(Phase::Ncx).failures, 1u);
      expectToc(book, nullptr);
    } else {
      expectToc(book, "NAV");
    }
    expectNoXmlLeaks();
  }
}

TEST(EpubIndexingOom, RestartStorageFailureStopsPublicationAndAllowsRetry) {
  for (bool failClose : {false, true}) {
    prepare(true, true);
    xml_fault::failAt(Phase::Nav, 1);
    if (failClose)
      storageFaults.failTocCloseAt = 1;
    else
      storageFaults.failTocOpenAt = 2;
    {
      Epub book;
      EXPECT_FALSE(book.load());
      EXPECT_FALSE(book.bookMetadataCache);
      EXPECT_FALSE(book.cssParser);
      EXPECT_FALSE(Storage.exists("/cache/book.bin"));
      EXPECT_EQ(xml_fault::stats(Phase::Ncx).calls, 0u);
      expectNoXmlLeaks();
      expectNoOpenFiles();
      xml_fault::reset();
      storageFaults = {};
      index_test::streamFailed = {};
      ASSERT_TRUE(book.load());
      expectToc(book, "NAV");
    }
    expectNoOpenFiles();
  }
}

TEST(EpubIndexingOom, MalformedTocDiscardsPartialEntriesBeforeFallback) {
  for (int broken = 0; broken < 3; ++broken) {
    prepare(true, true);
    if (broken == 0 || broken == 2) index_test::documents["OEBPS/nav.xhtml"].resize(180);
    if (broken == 1 || broken == 2) index_test::documents["OEBPS/toc.ncx"].resize(130);
    if (broken == 1) {
      index_test::documents.erase("OEBPS/nav.xhtml");
    }
    Epub book;
    ASSERT_TRUE(book.load());
    expectToc(book, broken == 0 ? "NCX" : nullptr);
    expectNoXmlLeaks();
  }
}

TEST(EpubIndexingOom, LargeIndexedTocFallbackPreservesEverySpineMapping) {
  prepare(true, true, 512);
  size_t calls;
  {
    Epub book;
    ASSERT_TRUE(book.load());
    expectToc(book, "NAV", 512);
    calls = xml_fault::stats(Phase::Nav).calls;
  }
  ASSERT_GT(calls, 10u);
  printf("XML_OOM_LARGE chapters=512 nav_allocation_points=%zu\n", calls);
  for (size_t failure = 1; failure <= calls; ++failure) {
    SCOPED_TRACE(failure);
    prepare(true, true, 512);
    xml_fault::failAt(Phase::Nav, failure);
    {
      Epub book;
      ASSERT_TRUE(book.load());
      ASSERT_EQ(xml_fault::stats(Phase::Nav).failures, 1u);
      expectToc(book, index_test::parserFailed(Phase::Nav) ? "NCX" : "NAV", 512);
      expectNoXmlLeaks();
    }
    expectNoOpenFiles();
  }
}

TEST(EpubColdOpen, ProfilesColdIndexAndWarmMetadataLoads) {
  for (const int chapters : {32, 128, 512, 2048}) {
    SCOPED_TRACE(chapters);
    prepare(true, false, chapters);
    storageMetrics = {};
    heapcap::reset(200 * 1024);
    Epub book;
    const bool loaded = book.load();
    const auto cold = storageMetrics;
    const auto allocations = heapcap::allocationCalls();
    const auto peak = heapcap::peak();
    const auto aborts = heapcap::aborts();
    heapcap::stop();
    ASSERT_TRUE(loaded);
    ASSERT_EQ(aborts, 0u);
    if (chapters >= 400) EXPECT_LT(cold.reads, static_cast<size_t>(chapters) * 5);
    // Below 400 spine items the TOC lookup is linear but resumes at the previous match.
    if (chapters < 400) EXPECT_LT(cold.reads, static_cast<size_t>(chapters) * 20);
    expectToc(book, "NAV", chapters);
    expectNoXmlLeaks();
    storageMetrics = {};
    Epub warm;
    ASSERT_TRUE(warm.load());
    const auto cached = storageMetrics;
    expectToc(warm, "NAV", chapters);
    printf(
        "EPUB_OPEN chapters=%d cold_reads=%zu cold_bytes=%zu cold_seeks=%zu cold_writes=%zu "
        "allocations=%zu peak=%zu warm_reads=%zu warm_bytes=%zu warm_seeks=%zu\n",
        chapters, cold.reads, cold.readBytes, cold.seeks, cold.writes, allocations, peak, cached.reads,
        cached.readBytes, cached.seeks);
  }
}
