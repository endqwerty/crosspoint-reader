#include <gtest/gtest.h>

#include "Epub/hyphenation/LanguageRegistry.h"

TEST(HyphenationEnglishOnly, OnlyEnglishPatternsAreBuiltIn) {
  const LanguageEntryView entries = getLanguageEntries();
  ASSERT_EQ(entries.size, 1u);
  EXPECT_STREQ(entries.data[0].primaryTag, "en");
  EXPECT_NE(getLanguageHyphenatorForPrimaryTag("en"), nullptr);
  for (const char* tag : {"de", "fr", "es", "it", "pl", "pt", "ru", "sv", "uk", "fi"}) {
    EXPECT_EQ(getLanguageHyphenatorForPrimaryTag(tag), nullptr) << tag;
  }
}
