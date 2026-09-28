#include <Utf8.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <new>
#include <string>

#include "HostAllocations.h"
#include "RealFontPage.h"

namespace {
// Frozen r11 behavior, including exact-fit and narrow-width ellipsis handling.
std::string legacyTruncatedText(const GfxRenderer& renderer, const char* text, int maxWidth,
                                EpdFontFamily::Style style) {
  if (!text || maxWidth <= 0) return "";
  std::string item = text;
  const char* ellipsis = "\xe2\x80\xa6";
  const int textWidth = renderer.getTextWidth(page_test::FONT_ID, item.c_str(), style);
  if (textWidth <= maxWidth) return item;
  while (!item.empty() && renderer.getTextWidth(page_test::FONT_ID, (item + ellipsis).c_str(), style) >= maxWidth) {
    utf8RemoveLastChar(item);
  }
  return item.empty() ? ellipsis : item + ellipsis;
}

class TextTruncation : public testing::TestWithParam<EpdFontFamily::Style> {};

TEST_P(TextTruncation, MatchesPreviousOutputAtEveryPixelBoundary) {
  page_test::RealFontPage page;
  static constexpr const char* TITLES[] = {"",
                                           "A",
                                           "A long title with an office, a flight, and AVATAR",
                                           "Café, déjà vu, naïve façade",
                                           "Zażółć gęślą jaźń. Łódź.",
                                           "Tiếng Việt: đọc sách mỗi ngày.",
                                           "Читатель открывает книгу.",
                                           "A\u0301 o\u0308 ffi AV",
                                           "Latin العربية עברית 123",
                                           "CJK title 中文 日本語 한글",
                                           "Malformed \x80\xc2\xc3 end",
                                           "Incomplete tail \xe2\x80"};
  for (const char* title : TITLES) {
    const int width = page.renderer.getTextWidth(page_test::FONT_ID, title, GetParam());
    for (int maxWidth = -1; maxWidth <= width + 1; ++maxWidth) {
      SCOPED_TRACE(testing::Message() << "title=" << title << " width=" << maxWidth);
      EXPECT_EQ(page.renderer.truncatedText(page_test::FONT_ID, title, maxWidth, GetParam()),
                legacyTruncatedText(page.renderer, title, maxWidth, GetParam()));
    }
  }
  EXPECT_TRUE(page.renderer.truncatedText(page_test::FONT_ID, nullptr, 200, GetParam()).empty());
}

TEST(TextTruncationAllocations, LongLatinTitleUsesOneReusableCandidate) {
  page_test::RealFontPage page;
  const std::string title(512, 'W');
  epub_page_test::allocations = {};
  epub_page_test::captureAllocations = true;
  const auto baseline = legacyTruncatedText(page.renderer, title.c_str(), 120, EpdFontFamily::REGULAR);
  epub_page_test::captureAllocations = false;
  const auto oldCounts = epub_page_test::allocations;

  epub_page_test::allocations = {};
  epub_page_test::captureAllocations = true;
  const auto actual = page.renderer.truncatedText(page_test::FONT_ID, title.c_str(), 120);
  epub_page_test::captureAllocations = false;
  const auto newCounts = epub_page_test::allocations;

  EXPECT_EQ(actual, baseline);
  // Out-of-line libc++ string allocations can bypass this executable's new
  // replacement under ASan. Bound observed calls; the legacy loop is a positive
  // control that repeated concatenations are still visible to the counter.
  // Upstream keeps the input and one reusable search candidate.
  EXPECT_LE(newCounts.calls, 2u);
  EXPECT_GT(oldCounts.calls, 400u);
  EXPECT_LT(newCounts.bytes, oldCounts.bytes);
  std::printf(
      "truncation_512_latin: r11_observed_new_calls=%zu current_observed_new_calls=%zu r11_observed_bytes=%zu "
      "current_observed_bytes=%zu\n",
      oldCounts.calls, newCounts.calls, oldCounts.bytes, newCounts.bytes);
}

TEST(TextTruncationAllocations, FittingLatinTitleNeedsOnlyItsReturnedString) {
  page_test::RealFontPage page;
  const std::string title(32, 'i');
  // Check the observer independently: this fitting path may allocate entirely
  // inside libc++, beyond the executable's replacement operator new.
  epub_page_test::allocations = {};
  epub_page_test::captureAllocations = true;
  void* control = ::operator new(title.size() + 1);
  epub_page_test::captureAllocations = false;
  ::operator delete(control);
  ASSERT_GT(epub_page_test::allocations.calls, 0u);

  epub_page_test::allocations = {};
  epub_page_test::captureAllocations = true;
  const auto actual = page.renderer.truncatedText(page_test::FONT_ID, title.c_str(), 10000);
  epub_page_test::captureAllocations = false;
  const auto counts = epub_page_test::allocations;
  EXPECT_EQ(actual, title);
  EXPECT_LE(counts.calls, 1u);
}

INSTANTIATE_TEST_SUITE_P(
    FontStyles, TextTruncation,
    testing::Values(EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC,
                    static_cast<EpdFontFamily::Style>(EpdFontFamily::SUP | EpdFontFamily::BOLD),
                    static_cast<EpdFontFamily::Style>(EpdFontFamily::SUB | EpdFontFamily::ITALIC)));
}  // namespace
