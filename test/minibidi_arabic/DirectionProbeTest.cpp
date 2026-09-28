#include <BidiUtils.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <string>

extern "C" {
#include "minibidi.h"
uchar reference_bidi_class(ucschar ch);
}
#undef when
#undef otherwise

namespace {
size_t classCalls = 0;

int referenceLevel(const char* text, int fallback, int limit) {
  if (!text || limit <= 0) return fallback & 1;
  auto* p = reinterpret_cast<const unsigned char*>(text);
  int checked = 0;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp || cp == REPLACEMENT_GLYPH) break;
    const uchar cls = reference_bidi_class(cp);
    if (cls == R || cls == AL) return 1;
    if (cls == L) return 0;
    if (++checked >= limit) break;
  }
  return fallback & 1;
}
}  // namespace

extern "C" uchar bidi_class(ucschar cp) {
  ++classCalls;
  return reference_bidi_class(cp);
}

TEST(DirectionProbe, PreservesAsciiPrefixesAndProbeLimits) {
  for (int prefix = 1; prefix < 128; ++prefix) {
    for (const char* suffix : {"Alphabet", "zebra", "שלום", "مرحبا", "中文", "123", ""}) {
      const std::string text = std::string(1, static_cast<char>(prefix)) + suffix;
      for (int limit : {-1, 0, 1, 2, 5, 64}) {
        for (int fallback : {-2, -1, 0, 1, 2}) {
          EXPECT_EQ(BidiUtils::detectParagraphLevel(text.c_str(), fallback, limit),
                    referenceLevel(text.c_str(), fallback, limit))
              << "prefix=" << prefix << " suffix=" << suffix << " limit=" << limit;
        }
        EXPECT_EQ(BidiUtils::startsWithRtl(text.c_str(), limit), referenceLevel(text.c_str(), 0, limit) != 0);
      }
    }
  }
}

TEST(DirectionProbe, PreservesUnicodeAndFallbackDirection) {
  for (const char* text : {"", "123!?", "שלום Latin", "مرحبا Latin", "éclair", "中文", "\u0301abc", "\u200Fabc",
                           "\u200Eשלום", "\uFFFDabc", "12 שלום", "12 abc"}) {
    for (int limit : {-1, 0, 1, 2, 3, 5, 64}) {
      for (int fallback : {0, 1}) {
        EXPECT_EQ(BidiUtils::detectParagraphLevel(text, fallback, limit), referenceLevel(text, fallback, limit))
            << text << " limit=" << limit;
      }
      EXPECT_EQ(BidiUtils::startsWithRtl(text, limit), referenceLevel(text, 0, limit) != 0);
    }
  }
  EXPECT_EQ(BidiUtils::detectParagraphLevel(nullptr, 1), 1);
  EXPECT_FALSE(BidiUtils::startsWithRtl(nullptr));
  EXPECT_EQ(BidiUtils::detectParagraphLevel("שלום"), 1);
  EXPECT_EQ(BidiUtils::detectParagraphLevel("مرحبا"), 1);
  EXPECT_EQ(BidiUtils::detectParagraphLevel("English", 1), 0);
  EXPECT_EQ(BidiUtils::detectParagraphLevel("12a", 1, 2), 1);
  EXPECT_EQ(BidiUtils::detectParagraphLevel("12a", 1, 3), 0);
}

TEST(DirectionProbe, AsciiWordsAvoidClassificationRequests) {
  classCalls = 0;
  for (int i = 0; i < 2000; ++i) {
    const char* word = i % 2 == 0 ? "Reading" : "books";
    EXPECT_EQ(BidiUtils::detectParagraphLevel(word, 1), 0);
    EXPECT_FALSE(BidiUtils::startsWithRtl(word));
  }
  const size_t asciiCalls = classCalls;
  classCalls = 0;
  EXPECT_EQ(BidiUtils::detectParagraphLevel("12a", 1, 3), 0);
  const size_t prefixCalls = classCalls;
  classCalls = 0;
  EXPECT_EQ(BidiUtils::detectParagraphLevel("שלום"), 1);
  EXPECT_EQ(BidiUtils::detectParagraphLevel("مرحبا"), 1);
  const size_t rtlCalls = classCalls;
  std::printf("DIRECTION_PROBE words=2000 api_calls=4000 ascii_lookups=%zu prefix_lookups=%zu rtl_lookups=%zu\n",
              asciiCalls, prefixCalls, rtlCalls);
  EXPECT_EQ(asciiCalls, 0u);
  EXPECT_EQ(prefixCalls, 2u);
  EXPECT_EQ(rtlCalls, 2u);
}
