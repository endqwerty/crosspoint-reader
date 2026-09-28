#include <FolderSearch.h>
#include <FsHelpers.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <utility>

namespace {
struct Node {
  std::string name;
  bool directory;
  std::vector<std::string> children;
};
struct FakeStorage;
class FakeFile {
 public:
  FakeFile() = default;
  FakeFile(FakeStorage* storage, Node* node);
  ~FakeFile() {
    if (storage) close();
  }
  FakeFile(const FakeFile&) = delete;
  FakeFile& operator=(const FakeFile&) = delete;
  FakeFile(FakeFile&& other) noexcept { *this = std::move(other); }
  FakeFile& operator=(FakeFile&& other) noexcept {
    if (this != &other) {
      if (storage) close();
      storage = std::exchange(other.storage, nullptr);
      node = std::exchange(other.node, nullptr);
      cursor = other.cursor;
      iterationFailed = other.iterationFailed;
    }
    return *this;
  }
  explicit operator bool() const { return node != nullptr; }
  bool isDirectory() const { return node && node->directory; }
  size_t getName(char* buffer, size_t size) {
    const auto length = std::min(size - 1, node->name.size());
    memcpy(buffer, node->name.data(), length);
    buffer[length] = '\0';
    return length;
  }
  bool hasError() const { return iterationFailed; }
  FakeFile openNextFile();
  void close();

 private:
  FakeStorage* storage = nullptr;
  Node* node = nullptr;
  size_t cursor = 0;
  bool iterationFailed = false;
};
struct FakeStorage {
  std::map<std::string, Node> nodes{{"/", {"/", true, {}}}};
  std::set<std::string> denied;
  std::vector<std::string> opened;
  size_t reads = 0;
  size_t failReadAt = 0;
  int handles = 0;
  int peakHandles = 0;
  void add(const std::string& parent, const std::string& name, bool directory = false) {
    auto path = library::joinLibraryPath(parent, name);
    nodes.at(parent).children.push_back(path);
    nodes.emplace(path, Node{name, directory, {}});
  }
  FakeFile open(const char* raw) {
    std::string path(raw);
    if (path.size() > 1 && path.back() == '/') path.pop_back();
    opened.push_back(path);
    if (denied.count(path) || !nodes.count(path)) return {};
    return FakeFile(this, &nodes.at(path));
  }
};
FakeFile::FakeFile(FakeStorage* storage, Node* node) : storage(storage), node(node) {
  ++storage->handles;
  storage->peakHandles = std::max(storage->peakHandles, storage->handles);
}
void FakeFile::close() {
  EXPECT_NE(storage, nullptr) << "HalFile::close requires an initialized handle";
  if (storage) --storage->handles;
  node = nullptr;
  storage = nullptr;
}
FakeFile FakeFile::openNextFile() {
  ++storage->reads;
  if (storage->reads == storage->failReadAt) {
    iterationFailed = true;
    return {};
  }
  if (cursor >= node->children.size()) return {};
  return FakeFile(storage, &storage->nodes.at(node->children[cursor++]));
}

bool readable(std::string_view name) {
  return FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasTxtExtension(name) ||
         FsHelpers::hasMarkdownExtension(name) || FsHelpers::hasBmpExtension(name) || FsHelpers::hasPngExtension(name);
}
class SearchTest : public testing::Test {
 protected:
  FakeStorage storage;
  FolderSearch<FakeFile> search;
  std::array<char, 500> buffer{};
  void start(const std::string& query, const std::string& root = "/", bool hidden = false) {
    search.start(root, library::fold(query), hidden);
  }
  void step() { search.step(storage, buffer.data(), buffer.size(), readable); }
  std::vector<std::string> finish() {
    size_t steps = 0;
    while (search.active() && ++steps < 100000) step();
    EXPECT_FALSE(search.active());
    EXPECT_EQ(storage.handles, 0);
    auto found = search.takeResults();
    FsHelpers::sortFileList(found);
    return found;
  }
};

TEST_F(SearchTest, DescendsThroughNonmatchingParentsAndFindsFoldersAndFiles) {
  storage.add("/", "Unrelated", true);
  storage.add("/Unrelated", "Dune collection", true);
  storage.add("/Unrelated/Dune collection", "Dune.epub");
  storage.add("/", "Dune.txt");
  start("dun");
  EXPECT_EQ(finish(), (std::vector<std::string>{"Unrelated/Dune collection/", "Dune.txt",
                                                "Unrelated/Dune collection/Dune.epub"}));
  EXPECT_FALSE(search.partial());
  EXPECT_LE(storage.peakHandles, 2);
}
TEST_F(SearchTest, DuplicateBasenamesKeepDistinctOpenAndDeletePaths) {
  for (const auto* name : {"Alice", "Bob"}) {
    storage.add("/", name, true);
    storage.add(std::string("/") + name, "Book.epub");
  }
  start("book");
  const auto found = finish();
  ASSERT_EQ(found.size(), 2u);
  for (const auto& relative : found) {
    const auto full = library::joinLibraryPath("/", relative);
    EXPECT_EQ(storage.nodes.count(full), 1u);
    EXPECT_NE(full.find("Book.epub"), std::string::npos);
  }
  EXPECT_NE(found[0], found[1]);
}
TEST_F(SearchTest, NestedRootStaysWithinItsSubtree) {
  storage.add("/", "Books", true);
  storage.add("/Books", "Shelf", true);
  storage.add("/Books/Shelf", "Book.epub");
  storage.add("/", "Book.txt");
  start("book", "/Books");
  EXPECT_EQ(finish(), (std::vector<std::string>{"Shelf/Book.epub"}));
  EXPECT_EQ(storage.opened.front(), "/Books");
}
TEST_F(SearchTest, MatchingParentDoesNotMakeEveryChildMatch) {
  storage.add("/", "Dune", true);
  storage.add("/Dune", "Unrelated.epub");
  start("dune");
  EXPECT_EQ(finish(), (std::vector<std::string>{"Dune/"}));
}
TEST_F(SearchTest, PreservesRawUnicodePathsWhileFoldingQuery) {
  const std::string folder = "Cafe\xcc\x81";
  storage.add("/", folder, true);
  storage.add("/" + folder, "Énéide.EPUB");
  start("ene ep");
  EXPECT_EQ(finish(), (std::vector<std::string>{folder + "/Énéide.EPUB"}));
}
TEST_F(SearchTest, HiddenSubtreesAndSystemMetadataAreSkipped) {
  for (const auto* folder : {".hidden", "System Volume Information"}) {
    storage.add("/", folder, true);
    storage.add(std::string("/") + folder, "Book.epub");
  }
  storage.add("/", ".Book.txt");
  start("book");
  EXPECT_TRUE(finish().empty());
  EXPECT_EQ(storage.opened.size(), 1u);
  start("book", "/", true);
  EXPECT_EQ(finish(), (std::vector<std::string>{".Book.txt", ".hidden/Book.epub"}));
}
TEST_F(SearchTest, UsesSupportedFileFilterButAlwaysVisitsFolders) {
  storage.add("/", "Book.bin");
  storage.add("/", "Book.pdf");
  storage.add("/", "Book.MD");
  storage.add("/", "Book.PNG");
  storage.add("/", "Book.XTCH");
  start("book");
  EXPECT_EQ(finish(), (std::vector<std::string>{"Book.MD", "Book.PNG", "Book.XTCH"}));
}
TEST_F(SearchTest, EachStepReadsAtMostOneEntryAndCancelClosesHandles) {
  for (int i = 0; i < 100; ++i) storage.add("/", "Book" + std::to_string(i) + ".epub");
  start("book");
  for (int i = 0; i < 5; ++i) {
    const auto previous = storage.reads;
    step();
    EXPECT_LE(storage.reads - previous, 1u);
  }
  EXPECT_TRUE(search.active());
  search.cancel();
  EXPECT_EQ(storage.handles, 0);
  EXPECT_FALSE(search.active());
  EXPECT_TRUE(search.takeResults().empty());
}
TEST_F(SearchTest, RestartDiscardsOldResultsAndPendingFolders) {
  storage.add("/", "Old", true);
  storage.add("/Old", "Old.epub");
  storage.add("/", "New.epub");
  start("old");
  step();
  step();
  start("new");
  EXPECT_EQ(finish(), (std::vector<std::string>{"New.epub"}));
  EXPECT_FALSE(search.partial());
}
TEST_F(SearchTest, UnreadableFolderMarksPartialAndOtherFoldersStillWork) {
  storage.add("/", "Bad", true);
  storage.add("/", "Good", true);
  storage.add("/Good", "Book.epub");
  storage.denied.insert("/Bad");
  start("book");
  EXPECT_EQ(finish(), (std::vector<std::string>{"Good/Book.epub"}));
  EXPECT_TRUE(search.partial());
}
TEST_F(SearchTest, MissingRootIsPartialRatherThanNoMatches) {
  start("book", "/Gone");
  EXPECT_TRUE(finish().empty());
  EXPECT_TRUE(search.partial());
}
TEST_F(SearchTest, ExactResultLimitIsCompleteUntilAnotherMatchAppears) {
  for (size_t i = 0; i < search.MAX_RESULTS; ++i) storage.add("/", "Book" + std::to_string(i) + ".epub");
  start("book");
  EXPECT_EQ(finish().size(), search.MAX_RESULTS);
  EXPECT_FALSE(search.partial());
  storage.add("/", "Book extra.epub");
  start("book");
  EXPECT_EQ(finish().size(), search.MAX_RESULTS);
  EXPECT_TRUE(search.partial());
}
TEST_F(SearchTest, PendingDirectoryCountLimitIsExplicit) {
  for (size_t i = 0; i <= search.MAX_PENDING; ++i) storage.add("/", "Shelf" + std::to_string(i), true);
  start("absent");
  EXPECT_TRUE(finish().empty());
  EXPECT_TRUE(search.partial());
  EXPECT_LE(storage.peakHandles, 2);
}
TEST_F(SearchTest, PendingPathByteLimitIsExplicit) {
  for (int i = 0; i < 100; ++i) storage.add("/", std::string(240, 's') + std::to_string(i), true);
  start("absent");
  EXPECT_TRUE(finish().empty());
  EXPECT_TRUE(search.partial());
}
TEST_F(SearchTest, ResultByteLimitIsExplicit) {
  for (int i = 0; i < 100; ++i) storage.add("/", "Book" + std::string(230, 'a') + std::to_string(i) + ".epub");
  start("book");
  const auto found = finish();
  size_t bytes = 0;
  for (const auto& name : found) bytes += name.size();
  EXPECT_LE(bytes, search.MAX_RESULT_BYTES);
  EXPECT_LT(found.size(), 100u);
  EXPECT_TRUE(search.partial());
}
TEST_F(SearchTest, DeepTraversalDoesNotAccumulateOpenDirectories) {
  std::string parent = "/";
  for (int i = 0; i < 80; ++i) {
    storage.add(parent, "d", true);
    parent = library::joinLibraryPath(parent, "d");
  }
  storage.add(parent, "Book.epub");
  start("book");
  EXPECT_EQ(finish().size(), 1u);
  EXPECT_FALSE(search.partial());
  EXPECT_LE(storage.peakHandles, 2);
}
TEST_F(SearchTest, OverlongFullPathIsSkippedAndReported) {
  std::string parent = "/";
  for (int i = 0; i < 6; ++i) {
    storage.add(parent, std::string(240, 'd'), true);
    parent = library::joinLibraryPath(parent, std::string(240, 'd'));
  }
  storage.add(parent, "Book.epub");
  start("book");
  EXPECT_TRUE(finish().empty());
  EXPECT_TRUE(search.partial());
}
TEST_F(SearchTest, TruncatedNameIsNeverReturnedAsAUsablePath) {
  storage.add("/", "Book" + std::string(600, 'x') + ".epub");
  start("book");
  EXPECT_TRUE(finish().empty());
  EXPECT_TRUE(search.partial());
}
TEST_F(SearchTest, EmptyNameAndPathSeparatorsAreRejected) {
  storage.add("/", "Blank");
  storage.nodes.at("/Blank").name.clear();
  storage.add("/", "Book/escape.epub");
  storage.add("/", "Book\\escape.epub");
  start("book");
  EXPECT_TRUE(finish().empty());
  EXPECT_TRUE(search.partial());
}
TEST_F(SearchTest, DotEntriesCannotRevisitParentOrSelf) {
  storage.add("/", ".", true);
  storage.add("/", "..", true);
  storage.add("/", "Book.epub");
  start("book", "/", true);
  EXPECT_EQ(finish(), (std::vector<std::string>{"Book.epub"}));
  EXPECT_EQ(storage.opened.size(), 1u);
  EXPECT_FALSE(search.partial());
}
TEST_F(SearchTest, NoMatchesCompletesWithoutPartialFlag) {
  storage.add("/", "Shelf", true);
  storage.add("/Shelf", "Book.epub");
  start("absent");
  EXPECT_TRUE(finish().empty());
  EXPECT_FALSE(search.partial());
}

TEST_F(SearchTest, InterruptedEnumerationReportsPartialAndRetryCompletes) {
  storage.add("/", "Book1.epub");
  storage.add("/", "Book2.epub");
  storage.failReadAt = 2;
  start("book");
  EXPECT_EQ(finish(), (std::vector<std::string>{"Book1.epub"}));
  EXPECT_TRUE(search.partial());
  start("book");
  EXPECT_EQ(finish(), (std::vector<std::string>{"Book1.epub", "Book2.epub"}));
  EXPECT_FALSE(search.partial());
}
}  // namespace
