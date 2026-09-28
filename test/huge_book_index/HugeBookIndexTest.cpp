// Indexing a book with thousands of chapters under a device-sized heap. Drives the
// real OPF, nav and book.bin passes in the order Epub::load runs them, over an
// in-memory SD card with the calibrated host allocation model (HeapCap.h).
#include <BookMetadataCache.h>
#include <ContentOpfParser.h>
#include <HeapCap.h>
#include <TocNavParser.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {
const std::string cachePath = "/cache/book";
const std::string basePath = "OEBPS/";
const std::string epubPath = "/books/huge.epub";

// Representative synthetic budgets, not measurements of free device heap.
// Native runs charge replacement new; ASan also observes C/libc++ allocations.
constexpr size_t OPEN_HEAP = 110 * 1024;
constexpr size_t GENEROUS_HEAP = 200 * 1024;

std::string pad5(const int i) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%05d", i);
  return buf;
}

uint32_t chapterBytes(const int i) { return 8000 + static_cast<uint32_t>(i * 37 % 9000); }

struct Book {
  std::string opf, nav;
  std::vector<std::pair<std::string, uint32_t>> zip;
};

// n chapters, one XHTML file each; every tocEvery-th chapter has a contents entry.
Book makeBook(const int n, const int tocEvery = 1, const bool longRecords = false) {
  heapcap::Untracked guard;
  Book b;
  std::string manifest =
      "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>";
  std::string spine, items;
  b.zip.push_back({"mimetype", 20});
  b.zip.push_back({"META-INF/container.xml", 200});
  for (int i = 1; i <= n; ++i) {
    const std::string file = (longRecords ? std::string(64, 'p') : "") + "c" + pad5(i) + ".xhtml";
    manifest += "<item id=\"c" + std::to_string(i) + "\" href=\"" + file + "\" media-type=\"application/xhtml+xml\"/>";
    spine += "<itemref idref=\"c" + std::to_string(i) + "\"/>";
    if ((i - 1) % tocEvery == 0) {
      items += "<li><a href=\"" + file + "\">" + (longRecords ? std::string(128, 't') : "") + "Chapter " +
               std::to_string(i) + "</a></li>";
    }
    b.zip.push_back({basePath + file, chapterBytes(i)});
  }
  b.zip.push_back({basePath + "nav.xhtml", 400});
  b.zip.push_back({basePath + "content.opf", 400});
  b.opf =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?><package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>Test</dc:title></metadata><manifest>" +
      manifest + "</manifest><spine>" + spine + "</spine></package>";
  b.nav =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\" "
      "xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>Contents</title></head><body>"
      "<nav epub:type=\"toc\"><ol>" +
      items + "</ol></nav></body></html>";
  return b;
}

template <typename P>
bool feed(P& parser, const std::string& xml) {
  // The same 1 KB slices Epub hands the parsers from the zip inflater.
  for (size_t at = 0; at < xml.size(); at += 1024) {
    const size_t n = std::min<size_t>(1024, xml.size() - at);
    if (parser.write(reinterpret_cast<const uint8_t*>(xml.data() + at), n) != n) return false;
  }
  return true;
}

struct IndexRun {
  bool ok = false;
  std::string failedAt;
  unsigned aborts = 0, uncontrolledOverruns = 0;
  std::string abortPhase;
  size_t abortRequest = 0;
  size_t peakOpf = 0, peakToc = 0, peakBookBin = 0, peakLoad = 0;
  size_t zipScans = 0;
  size_t nothrowCalls = 0, injectedFailures = 0;
  size_t assemblyAllocations = 0;
};

// Mirrors the cache-building half of Epub::load, then loads the result.
IndexRun indexBook(const Book& book, const size_t cap, const size_t failNothrowAt = 0,
                   const size_t maxAllocation = SIZE_MAX, const size_t failZipScan = 0) {
  {
    heapcap::Untracked guard;
    Storage.files.clear();
    zipModel = {};
    zipModel.entries = book.zip;
    zipModel.failScan = failZipScan;
  }
  IndexRun run;
  std::string phase = "opf";
  heapcap::reset(cap, failNothrowAt, maxAllocation);
  auto endPhase = [&](size_t& peak) {
    peak = heapcap::peak();
    if (heapcap::aborts() && run.abortPhase.empty()) {
      heapcap::Untracked guard;
      run.abortPhase = phase;
      run.abortRequest = heapcap::firstAbortSize();
    }
    heapcap::resetPeak();
  };
  auto finish = [&](const char* failedAt) {
    run.aborts = heapcap::aborts();
    run.uncontrolledOverruns = heapcap::uncontrolledOverruns();
    run.nothrowCalls = heapcap::nothrowCalls();
    run.injectedFailures = heapcap::injectedFailures();
    run.zipScans = zipModel.scans;
    heapcap::stop();
    // Raw C allocations are observed but cannot be refused by the new injector.
    if (maxAllocation == SIZE_MAX) {
      EXPECT_EQ(run.uncontrolledOverruns, 0u) << "Uninjected allocation exceeded the host byte budget";
    }
    run.failedAt = failedAt;
    run.ok = run.failedAt.empty();
    return run;
  };

  {
    BookMetadataCache cache(cachePath);
    BookMetadataCache::BookMetadata metadata;
    if (!cache.beginWrite() || !cache.beginContentOpfPass()) return finish("begin");
    {
      ContentOpfParser opf(cachePath, basePath, book.opf.size(), &cache);
      const bool parsed = opf.setup() && feed(opf, book.opf);
      if (!parsed) {
        endPhase(run.peakOpf);
        return finish("opf");
      }
      metadata.title = opf.title;
    }
    if (!cache.endContentOpfPass()) return finish("opf-end");
    endPhase(run.peakOpf);

    phase = "toc";
    if (!cache.beginTocPass()) {
      endPhase(run.peakToc);
      return finish("toc");
    }
    {
      TocNavParser nav(basePath, book.nav.size(), &cache);
      if (!nav.setup() || !feed(nav, book.nav)) {
        endPhase(run.peakToc);
        return finish("toc-parse");
      }
    }
    if (!cache.endTocPass() || !cache.endWrite()) return finish("toc-end");
    endPhase(run.peakToc);

    phase = "bookbin";
    const size_t allocationsBefore = heapcap::allocationCalls();
    const bool built = cache.buildBookBin(epubPath, metadata);
    run.assemblyAllocations = heapcap::allocationCalls() - allocationsBefore;
    endPhase(run.peakBookBin);
    if (!built) return finish("bookbin");
    cache.cleanupTmpFiles();
  }

  phase = "load";
  heapcap::resetPeak();
  BookMetadataCache loaded(cachePath);
  const bool ok = loaded.load();
  endPhase(run.peakLoad);
  return finish(ok ? "" : "load");
}

void print(const int n, const size_t cap, const IndexRun& r) {
  printf(
      "HUGE_INDEX n=%d cap=%zu ok=%d failed_at=%s aborts=%u abort_phase=%s abort_request=%zu peak_opf=%zu "
      "peak_toc=%zu peak_bookbin=%zu peak_load=%zu zip_scans=%zu uninjected_overruns=%u\n",
      n, cap == SIZE_MAX ? 0 : cap, r.ok ? 1 : 0, r.failedAt.empty() ? "-" : r.failedAt.c_str(), r.aborts,
      r.abortPhase.empty() ? "-" : r.abortPhase.c_str(), r.abortRequest, r.peakOpf, r.peakToc, r.peakBookBin,
      r.peakLoad, r.zipScans, r.uncontrolledOverruns);
}

std::vector<uint8_t> bookBinBytes() {
  heapcap::Untracked guard;
  const auto it = Storage.files.find(cachePath + "/book.bin");
  return it == Storage.files.end() ? std::vector<uint8_t>{} : it->second->bytes;
}

// Every spine entry carries the running total of the inflated sizes and the first
// contents entry pointing at it (or, without one, the previous chapter's entry).
void expectCache(const int n, const int tocEvery = 1) {
  BookMetadataCache cache(cachePath);
  ASSERT_TRUE(cache.load());
  ASSERT_EQ(cache.getSpineCount(), n);
  ASSERT_EQ(cache.getTocCount(), (n + tocEvery - 1) / tocEvery);
  uint32_t total = 0;
  for (int i = 0; i < n; ++i) {
    total += chapterBytes(i + 1);
    ASSERT_EQ(cache.getCumulativeSize(i), total) << "spine " << i;
  }
  for (int i : {0, 1, 2, n / 2, n - 2, n - 1}) {
    const auto spine = cache.getSpineEntry(i);
    EXPECT_EQ(spine.href, basePath + "c" + pad5(i + 1) + ".xhtml");
    EXPECT_EQ(spine.tocIndex, i / tocEvery) << "spine " << i;
  }
}
}  // namespace

TEST(HugeBookIndex, FiveThousandChaptersIndexUnderOpenHeap) {
  const Book book = makeBook(5000);
  const IndexRun r = indexBook(book, OPEN_HEAP);
  print(5000, OPEN_HEAP, r);
  EXPECT_EQ(r.aborts, 0u) << "the device would abort in " << r.abortPhase;
  ASSERT_TRUE(r.ok) << r.failedAt;
  expectCache(5000);
}

// Past what the heap can hold, the build must still end in a book that loads or a
// clean refusal that leaves no book.bin behind. Each pass that grows with the
// chapter count (OPF, TOC, book.bin) is where one of these sizes stops.
TEST(HugeBookIndex, LargerBooksIndexOrAreRefusedWithoutAbort) {
  const std::vector<std::pair<int, size_t>> cases = {
      {1500, 44 * 1024},  {6000, OPEN_HEAP},      {7000, OPEN_HEAP},      {9000, OPEN_HEAP},
      {20000, OPEN_HEAP}, {10000, GENEROUS_HEAP}, {20000, GENEROUS_HEAP}, {32000, GENEROUS_HEAP}};
  for (const auto& [n, cap] : cases) {
    const IndexRun r = indexBook(makeBook(n), cap);
    print(n, cap, r);
    EXPECT_EQ(r.aborts, 0u) << n << " chapters: the device would abort in " << r.abortPhase;
    if (r.ok) {
      expectCache(n);
    } else {
      EXPECT_TRUE(bookBinBytes().empty()) << n << " chapters: refused, but a book.bin was left";
    }
  }
}

// A book.bin written in several chunks is byte for byte the one written in one. Only
// every 1,000th chapter has a contents entry, so the chapters after a chunk edge take
// theirs from the previous chunk.
TEST(HugeBookIndex, ChunkedBookBinMatchesOneChunk) {
  const Book book = makeBook(5000, 1000);
  const IndexRun whole = indexBook(book, SIZE_MAX);
  ASSERT_TRUE(whole.ok) << whole.failedAt;
  const std::vector<uint8_t> expected = bookBinBytes();

  const IndexRun chunked = indexBook(book, OPEN_HEAP);
  print(5000, OPEN_HEAP, chunked);
  EXPECT_EQ(chunked.aborts, 0u) << "the device would abort in " << chunked.abortPhase;
  ASSERT_TRUE(chunked.ok) << chunked.failedAt;
  EXPECT_GE(chunked.zipScans, 2u) << "the capped build should take more than one chunk";
  EXPECT_TRUE(bookBinBytes() == expected);
  expectCache(5000, 1000);
}

// Books that fit one chunk keep their single zip directory scan (none below the
// batch lookup threshold).
TEST(HugeBookIndex, OrdinaryBooksKeepOneDirectoryScan) {
  for (const int n : {300, 400, 2000}) {
    const IndexRun r = indexBook(makeBook(n), OPEN_HEAP);
    print(n, OPEN_HEAP, r);
    EXPECT_EQ(r.aborts, 0u);
    ASSERT_TRUE(r.ok) << r.failedAt;
    EXPECT_EQ(r.zipScans, n >= 400 ? 1u : 0u);
    expectCache(n);
  }
}

TEST(HugeBookIndex, EveryNothrowAllocationFailureEitherRecoversOrLeavesNoPartialCache) {
  const auto book = makeBook(5000, 1000);
  const auto baseline = indexBook(book, OPEN_HEAP);
  ASSERT_TRUE(baseline.ok);
  ASSERT_GT(baseline.nothrowCalls, 10u);
  const auto expected = bookBinBytes();
  for (size_t fail = 1; fail <= baseline.nothrowCalls; ++fail) {
    SCOPED_TRACE(fail);
    const auto result = indexBook(book, OPEN_HEAP, fail);
    EXPECT_EQ(result.injectedFailures, 1u);
    EXPECT_EQ(result.aborts, 0u);
    if (result.ok || result.failedAt == "load") {
      EXPECT_EQ(bookBinBytes(), expected);
      expectCache(5000, 1000);
    } else {
      EXPECT_TRUE(bookBinBytes().empty());
    }
    EXPECT_EQ(heapcap::live(), 0u);
  }
}

TEST(HugeBookIndex, FragmentedHeapRefusesOversizedBlocksWithoutThrowing) {
  const auto book = makeBook(5000);
  for (const size_t block : {size_t{256}, size_t{1024}, size_t{4096}, size_t{16384}}) {
    const auto result = indexBook(book, OPEN_HEAP, 0, block);
    // Fragmentation faults apply to replacement new; report other oversized calls.
    printf("FRAGMENTED_HEAP block=%zu uninjected_overruns=%u\n", block, result.uncontrolledOverruns);
    EXPECT_GT(result.injectedFailures, 0u);
    EXPECT_EQ(result.aborts, 0u);
    if (result.ok || result.failedAt == "load") {
      expectCache(5000);
    } else {
      EXPECT_TRUE(bookBinBytes().empty());
    }
    EXPECT_EQ(heapcap::live(), 0u);
  }
}

TEST(HugeBookIndex, MetadataOnlyParsingAllocatesNoBuildIndex) {
  const auto book = makeBook(5000);
  heapcap::reset(OPEN_HEAP, 1);
  {
    ContentOpfParser opf(cachePath, basePath, book.opf.size(), nullptr, true);
    EXPECT_TRUE(opf.setup());
    // Metadata-only parsing intentionally short-writes once its fields are complete.
    EXPECT_FALSE(feed(opf, book.opf));
    EXPECT_EQ(opf.title, "Test");
    EXPECT_EQ(heapcap::nothrowCalls(), 0u);
    EXPECT_EQ(heapcap::aborts(), 0u);
  }
  heapcap::stop();
  EXPECT_EQ(heapcap::uncontrolledOverruns(), 0u);
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HugeBookIndex, CacheIndexRangeOverflowRefusesBookInsteadOfWrapping) {
  const auto result = indexBook(makeBook(32769), SIZE_MAX);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.failedAt, "opf-end");
  EXPECT_EQ(result.aborts, 0u);
  EXPECT_TRUE(bookBinBytes().empty());
}

TEST(HugeBookIndex, CumulativeSizeOverflowDiscardsPartialCache) {
  auto book = makeBook(5000);
  for (auto& entry : book.zip) entry.second = UINT32_MAX / 4;
  const auto result = indexBook(book, OPEN_HEAP);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.failedAt, "bookbin");
  EXPECT_EQ(result.aborts, 0u);
  EXPECT_TRUE(bookBinBytes().empty());
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HugeBookIndex, TocIndexRangeOverflowRefusesBookInsteadOfWrapping) {
  heapcap::Untracked guard;
  Storage.files.clear();
  BookMetadataCache cache(cachePath);
  ASSERT_TRUE(cache.beginWrite());
  ASSERT_TRUE(cache.beginContentOpfPass());
  cache.createSpineEntry("chapter.xhtml");
  ASSERT_TRUE(cache.endContentOpfPass());
  ASSERT_TRUE(cache.beginTocPass());
  for (int i = 0; i < 32769; ++i) cache.createTocEntry("Chapter", "chapter.xhtml", "", 0);
  EXPECT_FALSE(cache.endTocPass());
  EXPECT_FALSE(cache.endWrite());
  EXPECT_FALSE(cache.buildBookBin(epubPath, {}));
  EXPECT_TRUE(bookBinBytes().empty());
}

TEST(HugeBookIndex, FailedSecondZipChunkDiscardsIncompleteCache) {
  const auto result = indexBook(makeBook(5000), OPEN_HEAP, 0, SIZE_MAX, 2);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.failedAt, "bookbin");
  EXPECT_EQ(result.zipScans, 2u);
  EXPECT_EQ(result.aborts, 0u);
  EXPECT_TRUE(bookBinBytes().empty());
  EXPECT_EQ(heapcap::live(), 0u);
}

TEST(HugeBookIndex, LongMetadataRecordsReuseAssemblyAllocations) {
  for (const int n : {300, 1000}) {
    const auto book = makeBook(n, 1, true);
    const auto result = indexBook(book, OPEN_HEAP);
    ASSERT_TRUE(result.ok) << result.failedAt;
    EXPECT_EQ(result.aborts, 0u);
    EXPECT_EQ(heapcap::live(), 0u);
    // Calibrated native and ASan counters both include libc++ string growth.
    EXPECT_GT(result.assemblyAllocations, 3u);
    EXPECT_LT(result.assemblyAllocations, static_cast<size_t>(n * 3 + 100));
    BookMetadataCache cache(cachePath);
    ASSERT_TRUE(cache.load());
    EXPECT_EQ(cache.getSpineEntry(n - 1).href, basePath + std::string(64, 'p') + "c" + pad5(n) + ".xhtml");
    EXPECT_EQ(cache.getTocEntry(n - 1).title, std::string(128, 't') + "Chapter " + std::to_string(n));
    printf("ASSEMBLY_ALLOCATIONS chapters=%d calls=%zu peak=%zu\n", n, result.assemblyAllocations, result.peakBookBin);
  }
}

TEST(HugeBookIndex, LongMetadataChunkedOutputMatchesOneChunk) {
  const auto book = makeBook(5000, 1, true);
  const auto whole = indexBook(book, SIZE_MAX);
  ASSERT_TRUE(whole.ok) << whole.failedAt;
  const auto expected = bookBinBytes();
  const auto chunked = indexBook(book, OPEN_HEAP);
  ASSERT_TRUE(chunked.ok) << chunked.failedAt;
  EXPECT_EQ(chunked.aborts, 0u);
  EXPECT_GE(chunked.zipScans, 2u);
  EXPECT_EQ(bookBinBytes(), expected);
  EXPECT_EQ(heapcap::live(), 0u);
  printf("LONG_CHUNKED_ASSEMBLY calls=%zu peak=%zu scans=%zu\n", chunked.assemblyAllocations, chunked.peakBookBin,
         chunked.zipScans);
}

TEST(HugeBookIndex, ReusedRecordsClearEmptyFieldsAndReleasePeakCapacity) {
  Storage.files.clear();
  zipModel = {};
  std::vector<BookMetadataCache::TocEntry> entries;
  entries.reserve(4);
  entries.emplace_back(std::string(4096, 't'), "a.xhtml", "", 0, 0);
  entries.emplace_back("", std::string(4090, 'p') + ".xhtml", "", 1, 1);
  entries.emplace_back("Café", "c.xhtml", std::string(4096, 'a'), 2, 2);
  entries.emplace_back("", "d.xhtml", "", 3, 3);
  zipModel.entries.reserve(entries.size());
  for (const auto& entry : entries) zipModel.entries.emplace_back(entry.href, 100);
  BookMetadataCache writer(cachePath);
  ASSERT_TRUE(writer.beginWrite());
  ASSERT_TRUE(writer.beginContentOpfPass());
  for (const auto& entry : entries) writer.createSpineEntry(entry.href);
  ASSERT_TRUE(writer.endContentOpfPass());
  ASSERT_TRUE(writer.beginTocPass());
  for (const auto& entry : entries) writer.createTocEntry(entry.title, entry.href, entry.anchor, entry.level);
  ASSERT_TRUE(writer.endTocPass());
  ASSERT_TRUE(writer.endWrite());
  const BookMetadataCache::BookMetadata metadata{"Title", "Author", "en", "", ""};
  heapcap::reset(32 * 1024);
  const bool built = writer.buildBookBin(epubPath, metadata);
  const size_t peak = heapcap::peak();
  const auto aborts = heapcap::aborts();
  const size_t live = heapcap::live();
  heapcap::stop();
  ASSERT_TRUE(built);
  EXPECT_EQ(heapcap::uncontrolledOverruns(), 0u);
  EXPECT_EQ(aborts, 0u);
  EXPECT_EQ(live, 0u);
  EXPECT_LE(peak, 32u * 1024);
  BookMetadataCache reader(cachePath);
  ASSERT_TRUE(reader.load());
  for (int i = 0; i < 4; ++i) {
    const auto toc = reader.getTocEntry(i);
    EXPECT_EQ(toc.title, entries[i].title);
    EXPECT_EQ(toc.href, entries[i].href);
    EXPECT_EQ(toc.anchor, entries[i].anchor);
    EXPECT_EQ(toc.level, entries[i].level);
    EXPECT_EQ(toc.spineIndex, i);
    EXPECT_EQ(reader.getSpineEntry(i).tocIndex, i);
    EXPECT_EQ(reader.getCumulativeSize(i), 100u * (i + 1));
  }
  printf("REUSED_RECORD_CAPACITY peak=%zu live_after_build=%zu\n", peak, live);
}
