#include <gtest/gtest.h>

#include "EpubHrefResolveFixture.h"

TEST(EpubHrefResolveTest, ExactPathWinsOverEarlierSameFilename) {
  const Epub epub({"OEBPS/a/chapter.xhtml", "OEBPS/b/chapter.xhtml"});
  EXPECT_EQ(epub.resolveHrefToSpineIndex("OEBPS/b/chapter.xhtml#note"), 1);
  EXPECT_EQ(epub.resolveHrefToSpineIndex("OEBPS/a/chapter.xhtml"), 0);
}

TEST(EpubHrefResolveTest, FilenameFallbackTakesFirstMatch) {
  const Epub epub({"OEBPS/a/chapter.xhtml", "OEBPS/b/chapter.xhtml"});
  EXPECT_EQ(epub.resolveHrefToSpineIndex("other/chapter.xhtml"), 0);
}

TEST(EpubHrefResolveTest, UnknownOrAnchorOnlyHrefIsNotFound) {
  const Epub epub({"OEBPS/a/chapter.xhtml"});
  EXPECT_EQ(epub.resolveHrefToSpineIndex("OEBPS/a/missing.xhtml"), -1);
  EXPECT_EQ(epub.resolveHrefToSpineIndex("#note"), -1);
}
