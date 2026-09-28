#include <expat.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
struct AllocationStats {
  size_t calls = 0, live = 0, peak = 0, blocks = 0;
  size_t failAt = 0;
  bool failed = false;
};
AllocationStats stats;
struct alignas(std::max_align_t) Header {
  size_t size;
};

bool failAllocation() {
  ++stats.calls;
  if (stats.failAt == stats.calls) {
    stats.failed = true;
    return true;
  }
  return false;
}
void* allocate(size_t size) {
  if (failAllocation()) return nullptr;
  auto* h = static_cast<Header*>(std::malloc(sizeof(Header) + size));
  if (!h) return nullptr;
  h->size = size;
  stats.live += size;
  stats.peak = std::max(stats.peak, stats.live);
  ++stats.blocks;
  return h + 1;
}
void release(void* ptr) {
  if (!ptr) return;
  auto* h = static_cast<Header*>(ptr) - 1;
  stats.live -= h->size;
  --stats.blocks;
  std::free(h);
}
void* resize(void* ptr, size_t size) {
  if (!ptr) return allocate(size);
  if (failAllocation()) return nullptr;
  auto* h = static_cast<Header*>(ptr) - 1;
  const auto oldSize = h->size;
  auto* replacement = static_cast<Header*>(std::realloc(h, sizeof(Header) + size));
  if (!replacement) return nullptr;
  replacement->size = size;
  stats.live = stats.live - oldSize + size;
  stats.peak = std::max(stats.peak, stats.live);
  return replacement + 1;
}

struct Result {
  bool ok = false;
  XML_Error error = XML_ERROR_NONE;
  size_t starts = 0, ends = 0, textBytes = 0;
  uint64_t textHash = 14695981039346656037ULL;
  uint64_t attributeHash = 14695981039346656037ULL;
  size_t allocations = 0, peak = 0, retained = 0;
};
void hashBytes(uint64_t& hash, const char* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= static_cast<uint8_t>(data[i]);
    hash *= 1099511628211ULL;
  }
}
struct Handler {
  XML_Parser parser = nullptr;
  Result result;
  bool suspend = false, suspended = false;
  bool contextMatches = true;
};
void XMLCALL start(void* data, const XML_Char*, const XML_Char** attributes) {
  auto& h = *static_cast<Handler*>(data);
  ++h.result.starts;
  int offset = 0, size = 0;
  const auto* context = XML_GetInputContext(h.parser, &offset, &size);
  h.contextMatches &= (context != nullptr) == (XML_CONTEXT_BYTES != 0);
  for (size_t i = 0; attributes[i]; ++i) {
    hashBytes(h.result.attributeHash, attributes[i], std::strlen(attributes[i]) + 1);
  }
}
void XMLCALL end(void* data, const XML_Char*) { ++static_cast<Handler*>(data)->result.ends; }
void XMLCALL text(void* data, const XML_Char* bytes, int size) {
  auto& h = *static_cast<Handler*>(data);
  h.result.textBytes += size;
  hashBytes(h.result.textHash, bytes, size);
  if (h.suspend && !h.suspended) {
    h.suspended = true;
    XML_StopParser(h.parser, XML_TRUE);
  }
}
Result parse(const std::string& xml, size_t chunk, bool buffered, bool suspend = false, size_t failAt = 0) {
  stats = {};
  stats.failAt = failAt;
  const XML_Memory_Handling_Suite allocator{allocate, resize, release};
  Handler h;
  h.parser = XML_ParserCreate_MM(nullptr, &allocator, nullptr);
  if (!h.parser) {
    h.result.error = XML_ERROR_NO_MEMORY;
    EXPECT_EQ(stats.live, 0U);
    EXPECT_EQ(stats.blocks, 0U);
    return h.result;
  }
  h.suspend = suspend;
  XML_SetUserData(h.parser, &h);
  XML_SetElementHandler(h.parser, start, end);
  XML_SetCharacterDataHandler(h.parser, text);
  XML_Status status = XML_STATUS_OK;
  for (size_t pos = 0; pos < xml.size() && status == XML_STATUS_OK;) {
    const size_t size = std::min(chunk, xml.size() - pos);
    const bool final = pos + size == xml.size();
    if (buffered) {
      auto* buffer = XML_GetBuffer(h.parser, static_cast<int>(std::max<size_t>(1024, size)));
      if (!buffer) {
        status = XML_STATUS_ERROR;
        break;
      }
      std::memcpy(buffer, xml.data() + pos, size);
      status = XML_ParseBuffer(h.parser, static_cast<int>(size), final);
    } else {
      status = XML_Parse(h.parser, xml.data() + pos, static_cast<int>(size), final);
    }
    while (status == XML_STATUS_SUSPENDED) status = XML_ResumeParser(h.parser);
    pos += size;
  }
  h.result.ok = status == XML_STATUS_OK;
  h.result.error = XML_GetErrorCode(h.parser);
  EXPECT_TRUE(h.contextMatches);
  if (suspend && h.result.ok) EXPECT_TRUE(h.suspended);
  h.result.retained = stats.live;
  XML_ParserFree(h.parser);
  EXPECT_EQ(stats.live, 0U);
  EXPECT_EQ(stats.blocks, 0U);
  h.result.allocations = stats.calls;
  h.result.peak = stats.peak;
  return h.result;
}
std::string document() {
  std::string xml = "<book title='" + std::string(5000, 'a') + "'>";
  for (int i = 0; i < 256; ++i) xml += "<p>Été 中文 café &amp; &#x1F4D6; text.</p>";
  return xml + "</book>";
}

class ExpatStreaming : public testing::TestWithParam<size_t> {};
TEST_P(ExpatStreaming, BothInputApisPreserveTextAndAttributesAcrossBoundaries) {
  const auto xml = document();
  std::string expected;
  for (int i = 0; i < 256; ++i) expected += "Été 中文 café & 📖 text.";
  uint64_t hash = 14695981039346656037ULL;
  hashBytes(hash, expected.data(), expected.size());
  uint64_t attributeHash = 14695981039346656037ULL;
  hashBytes(attributeHash, "title", 6);
  const std::string attribute(5000, 'a');
  hashBytes(attributeHash, attribute.c_str(), attribute.size() + 1);
  for (bool buffered : {false, true}) {
    const auto result = parse(xml, GetParam(), buffered);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.starts, 257U);
    EXPECT_EQ(result.ends, 257U);
    EXPECT_EQ(result.textBytes, expected.size());
    EXPECT_EQ(result.textHash, hash);
    EXPECT_EQ(result.attributeHash, attributeHash);
  }
}
TEST_P(ExpatStreaming, SuspendResumePreservesUnreadInput) {
  const auto xml = document();
  for (bool buffered : {false, true}) {
    const auto expected = parse(xml, GetParam(), buffered);
    const auto resumed = parse(xml, GetParam(), buffered, true);
    ASSERT_TRUE(resumed.ok);
    EXPECT_EQ(resumed.starts, expected.starts);
    EXPECT_EQ(resumed.ends, expected.ends);
    EXPECT_EQ(resumed.textHash, expected.textHash);
    EXPECT_EQ(resumed.attributeHash, expected.attributeHash);
  }
}
TEST_P(ExpatStreaming, InvalidAndTruncatedXmlReturnsError) {
  for (const auto* xml : {"<book><p>text</book>", "<book><p>text", "<book>&invalid;</book>", "<book>\xC3</book>"}) {
    for (bool buffered : {false, true}) {
      const auto result = parse(xml, GetParam(), buffered);
      EXPECT_FALSE(result.ok);
      EXPECT_NE(result.error, XML_ERROR_NONE);
    }
  }
}
INSTANTIATE_TEST_SUITE_P(ChunkSizes, ExpatStreaming, testing::Values(1, 7, 1024, 4096));

TEST(ExpatFailures, EveryParserAllocationFailureReleasesMemory) {
  const auto xml = document();
  for (bool buffered : {false, true}) {
    const auto baseline = parse(xml, 1024, buffered);
    ASSERT_TRUE(baseline.ok);
    for (size_t at = 1; at <= baseline.allocations; ++at) {
      SCOPED_TRACE(at);
      const auto failed = parse(xml, 1024, buffered, false, at);
      EXPECT_TRUE(stats.failed);
      EXPECT_FALSE(failed.ok);
      EXPECT_EQ(failed.error, XML_ERROR_NO_MEMORY);
    }
    EXPECT_TRUE(parse(xml, 1024, buffered).ok);
  }
}

TEST(ExpatProfile, ReportsParserRequestedMemoryForStreamingWorkloads) {
  std::string xml = "<book>";
  for (int i = 0; i < 4096; ++i) xml += "<p>Ordinary reading text, one paragraph at a time.</p>";
  xml += "</book>";
  RecordProperty("context_bytes", XML_CONTEXT_BYTES);
  for (bool buffered : {false, true}) {
    const auto result = parse(xml, 1024, buffered);
    ASSERT_TRUE(result.ok);
    const std::string prefix = buffered ? "buffered_" : "direct_";
    RecordProperty(prefix + "peak_requested_bytes", std::to_string(result.peak));
    RecordProperty(prefix + "retained_requested_bytes", std::to_string(result.retained));
    RecordProperty(prefix + "allocation_calls", std::to_string(result.allocations));
    RecordProperty(prefix + "text_hash", std::to_string(result.textHash));
    RecordProperty(prefix + "text_bytes", std::to_string(result.textBytes));
    EXPECT_EQ(result.starts, 4097U);
    EXPECT_EQ(result.ends, 4097U);
  }
}
}  // namespace
