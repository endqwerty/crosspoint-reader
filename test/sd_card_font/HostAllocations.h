#pragma once

#include <cstddef>

struct SdFontTestAllocations {
  size_t attempts = 0;
  size_t requestedBytes = 0;
};
extern SdFontTestAllocations sdFontTestAllocations;
extern size_t failNextArraySize;
