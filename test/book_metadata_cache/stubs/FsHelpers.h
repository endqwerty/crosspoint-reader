#pragma once
#include <string>
namespace FsHelpers {
inline std::string normalisePath(const std::string& path) { return path; }
inline bool hasTxtExtension(const std::string&) { return false; }
inline bool hasMarkdownExtension(const std::string&) { return false; }
}  // namespace FsHelpers
