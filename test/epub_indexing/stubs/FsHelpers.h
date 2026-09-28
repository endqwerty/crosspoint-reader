#pragma once
#include <string_view>

#include "../../huge_book_index/stubs/FsHelpers.h"
namespace FsHelpers {
inline bool hasPngExtension(std::string_view name) { return name.ends_with(".png"); }
inline bool hasJpgExtension(std::string_view name) { return name.ends_with(".jpg"); }
}  // namespace FsHelpers
