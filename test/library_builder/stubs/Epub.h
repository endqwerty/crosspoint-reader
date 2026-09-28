#pragma once

#include <map>
#include <string>

#include "HalStorage.h"

struct FakeMetadata {
  std::string title = "Title";
  std::string author = "Author";
  std::string series;
  std::string seriesIndexText;
  bool success = true;
  std::string titleSort;
  std::string authorSort;
  std::string uuid;
};

inline std::map<std::string, FakeMetadata> bookMetadata;

class Epub {
  std::string path;

 public:
  Epub(const std::string& path, const char*) : path(path) {}

  bool loadMetadata(std::string& title, std::string& author) {
    ++fake::parses;
    const auto& metadata = bookMetadata[path];
    if (!metadata.success) return false;
    title = metadata.title;
    author = metadata.author;
    return true;
  }

  struct LibraryMetadata {
    std::string title;
    std::string author;
    std::string series;
    std::string seriesIndexText;
    std::string titleSort;
    std::string authorSort;
    std::string uuid;
  };

  bool loadMetadata(LibraryMetadata& out) {
    ++fake::parses;
    out = LibraryMetadata{};
    const auto& metadata = bookMetadata[path];
    if (!metadata.success) return false;
    out.title = metadata.title;
    out.author = metadata.author;
    out.series = metadata.series;
    out.seriesIndexText = metadata.seriesIndexText;
    out.titleSort = metadata.titleSort;
    out.authorSort = metadata.authorSort;
    out.uuid = metadata.uuid;
    return true;
  }
};
