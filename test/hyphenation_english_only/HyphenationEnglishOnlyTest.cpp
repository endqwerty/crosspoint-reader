#include <gtest/gtest.h>

#include "Epub/hyphenation/LanguageRegistry.h"

TEST(HyphenationEnglishOnly, OnlyEnglishPatternsAreBuiltIn) {
  EXPECT_NE(getLanguageHyphenatorForPrimaryTag("en"), nullptr);
  for (const char* tag : {"de", "fr", "es", "it", "pl", "pt", "ru", "sv", "uk", "fi"}) {
    EXPECT_EQ(getLanguageHyphenatorForPrimaryTag(tag), nullptr) << tag;
  }
}
