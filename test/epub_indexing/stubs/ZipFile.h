#pragma once
#define ZipFile IndexedZipFile
#include "../../huge_book_index/stubs/ZipFile.h"
#undef ZipFile
#include "../Archive.h"
class ZipFile : public IndexedZipFile {
 public:
  using IndexedZipFile::IndexedZipFile;
  bool readFileToStream(const char* path, Print& output, size_t chunk, bool shortOk = false) {
    return index_test::stream(path, output, chunk, shortOk);
  }
};
