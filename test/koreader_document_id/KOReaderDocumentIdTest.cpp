#include <HalStorage.h>
#include <gtest/gtest.h>

#include "KOReaderDocumentId.h"

// The stub MD5Builder's "hash" is the number of bytes it was given.

TEST(KOReaderDocumentIdTest, FullReadsHashEveryChunk) {
  Storage.scripted = HalFile{};
  Storage.scripted.size = 5000;  // offsets 0, 1024 and 4096 lie inside the file
  EXPECT_EQ(KOReaderDocumentId::calculate("/book.epub"), std::to_string(1024 + 1024 + (5000 - 4096)));
}

TEST(KOReaderDocumentIdTest, ReadErrorReturnsNoHash) {
  Storage.scripted = HalFile{};
  Storage.scripted.size = 5000;
  Storage.scripted.results = {1024, -1};
  EXPECT_EQ(KOReaderDocumentId::calculate("/book.epub"), "");
}

TEST(KOReaderDocumentIdTest, EmptyReadAddsNothing) {
  Storage.scripted = HalFile{};
  Storage.scripted.size = 5000;
  Storage.scripted.results = {1024, 0};
  EXPECT_EQ(KOReaderDocumentId::calculate("/book.epub"), std::to_string(1024 + (5000 - 4096)));
}
