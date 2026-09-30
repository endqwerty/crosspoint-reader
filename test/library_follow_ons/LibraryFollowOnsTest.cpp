#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Epub.h"
#include "LibraryBookState.h"
#include "LibraryBuilder.h"
#include "LibraryFollowOns.h"
#include "LibraryIndexFile.h"

using namespace library;

namespace {

// Calibre's export layout: every book in a folder of its own.
std::string bookPath(const std::string& author, const std::string& title) {
  return "/" + (author.empty() ? std::string("Unknown") : author) + "/" + title + "/" + title + ".epub";
}

class LibraryFollowOnsTest : public ::testing::Test {
 protected:
  BuildStats stats;
  LibraryIndexFile index;

  void SetUp() override {
    fake::reset();
    bookMetadata.clear();
  }

  std::string addBook(const std::string& title, const std::string& author, const std::string& series = "",
                      const std::string& position = "") {
    const std::string path = bookPath(author, title);
    fake::add(path, title + " contents");
    bookMetadata[path] = {title, author, series, position};
    return path;
  }

  void buildAndOpen() {
    ASSERT_TRUE(buildLibraryIndex("/", stats, true));
    ASSERT_TRUE(index.open(libraryIndexPath()));
  }

  std::vector<std::string> titlesAfter(const std::string& path, const size_t maxCount = 3) {
    std::vector<FollowOn> out(maxCount);
    const size_t count = findFollowOns(index, path, out.data(), maxCount);
    std::vector<std::string> titles;
    for (size_t i = 0; i < count; ++i) titles.push_back(out[i].title);
    return titles;
  }
};

using Titles = std::vector<std::string>;

}  // namespace

TEST_F(LibraryFollowOnsTest, SuggestsLaterVolumesInSeriesOrderAcrossFolders) {
  addBook("Book Four", "Pirate Aba", "The Wandering Inn", "4");
  const auto one = addBook("Book One", "Pirate Aba", "The Wandering Inn", "1");
  addBook("Book Three", "Pirate Aba", "The Wandering Inn", "3");
  const auto two = addBook("Book Two", "Pirate Aba", "The Wandering Inn", "2");
  buildAndOpen();
  EXPECT_EQ(titlesAfter(one), (Titles{"Book Two", "Book Three", "Book Four"}));
  EXPECT_EQ(titlesAfter(two), (Titles{"Book Three", "Book Four"}));
  EXPECT_EQ(titlesAfter(one, 1), (Titles{"Book Two"}));
}

TEST_F(LibraryFollowOnsTest, FractionalVolumesSortBetweenTheirNeighbours) {
  const auto two = addBook("Two", "Author One", "Saga", "2");
  addBook("Three", "Author One", "Saga", "3");
  addBook("Two And A Half", "Author One", "Saga", "2.5");
  buildAndOpen();
  EXPECT_EQ(titlesAfter(two), (Titles{"Two And A Half", "Three"}));
}

TEST_F(LibraryFollowOnsTest, SeriesSuggestionsStopAtTheSeriesBoundary) {
  const auto a2 = addBook("A Two", "First Writer", "Series A", "2");
  addBook("A One", "First Writer", "Series A", "1");
  addBook("B One", "Second Writer", "Series B", "1");
  addBook("Standalone", "Third Writer");
  buildAndOpen();
  EXPECT_TRUE(titlesAfter(a2).empty());
}

TEST_F(LibraryFollowOnsTest, FinishedBooksAreSkipped) {
  const auto one = addBook("One", "Some Author", "Saga", "1");
  const auto two = addBook("Two", "Some Author", "Saga", "2");
  addBook("Three", "Some Author", "Saga", "3");
  buildAndOpen();
  ASSERT_TRUE(writeBookState(bookStateKey(two), {false, ReadingState::Finished}));
  EXPECT_EQ(titlesAfter(one), (Titles{"Three"}));
}

TEST_F(LibraryFollowOnsTest, BooksMissingFromTheCardAreSkipped) {
  const auto one = addBook("One", "Some Author", "Saga", "1");
  const auto two = addBook("Two", "Some Author", "Saga", "2");
  addBook("Three", "Some Author", "Saga", "3");
  buildAndOpen();
  ASSERT_TRUE(Storage.remove(two.c_str()));
  EXPECT_EQ(titlesAfter(one), (Titles{"Three"}));
}

TEST_F(LibraryFollowOnsTest, LastVolumeFallsBackToTheAuthorsLaterTitles) {
  const auto last = addBook("Saga Two", "Ann Author", "Saga", "2");
  addBook("Saga One", "Ann Author", "Saga", "1");
  addBook("Zzz Standalone", "Ann Author");
  buildAndOpen();
  // Nothing follows in the series; the author's later titles are offered instead.
  EXPECT_EQ(titlesAfter(last), (Titles{"Zzz Standalone"}));
}

TEST_F(LibraryFollowOnsTest, StandaloneBooksSuggestTheSameAuthorsLaterTitlesOnly) {
  const auto alpha = addBook("Alpha", "Ann Author");
  addBook("Beta", "Ann Author");
  addBook("Gamma", "Ann Author");
  addBook("Delta", "Zed Writer");
  buildAndOpen();
  EXPECT_EQ(titlesAfter(alpha), (Titles{"Beta", "Gamma"}));
}

TEST_F(LibraryFollowOnsTest, BookWithNoAuthorIdentityGetsNoAuthorSuggestions) {
  const auto anonymous = addBook("Nameless", "");
  addBook("Other", "");
  buildAndOpen();
  EXPECT_TRUE(titlesAfter(anonymous).empty());
}

TEST_F(LibraryFollowOnsTest, UnknownPathAndClosedIndexSuggestNothing) {
  const auto known = addBook("Known", "Ann Author");
  addBook("Later", "Ann Author");
  buildAndOpen();
  EXPECT_TRUE(titlesAfter("/Nobody/Missing/Missing.epub").empty());
  FollowOn out[3];
  EXPECT_EQ(findFollowOns(index, known, nullptr, 3), 0u);
  EXPECT_EQ(findFollowOns(index, known, out, 0), 0u);
  index.close();
  EXPECT_EQ(findFollowOns(index, known, out, 3), 0u);
}

TEST_F(LibraryFollowOnsTest, ReadFailureSuggestsNothing) {
  const auto one = addBook("One", "Some Author", "Saga", "1");
  addBook("Two", "Some Author", "Saga", "2");
  buildAndOpen();
  fake::failRead = 0;
  EXPECT_TRUE(titlesAfter(one).empty());
}
