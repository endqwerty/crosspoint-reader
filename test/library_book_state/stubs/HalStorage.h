#pragma once
// Model a transient SD lookup error reported as a missing file.
#include "../../library_builder/stubs/HalStorage.h"

namespace fake {
inline bool transientExistsMiss = false;
}  // namespace fake

class FaultInjectingHalStorage : public HalStorage {
 public:
  bool exists(const char* path) const {
    if (fake::transientExistsMiss) {
      fake::transientExistsMiss = false;
      return false;
    }
    return HalStorage::exists(path);
  }
};
inline FaultInjectingHalStorage faultInjectingStorage;

#undef Storage
#define Storage faultInjectingStorage
