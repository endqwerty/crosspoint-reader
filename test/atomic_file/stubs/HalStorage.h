#pragma once
#include "../../library_builder/stubs/HalStorage.h"

#undef Storage
struct AtomicStorage : HalStorage {
  std::string readFile(const char* path) {
    auto file = open(path);
    if (!file) return {};
    std::string result(file.fileSize(), '\0');
    if (file.read(result.data(), result.size()) != static_cast<int>(result.size())) return {};
    return result;
  }
};
inline AtomicStorage atomicStorage;
#define Storage atomicStorage
