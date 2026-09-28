#pragma once

#include <Arduino.h>

#include <cstddef>

// The backup is the committed copy until replacement and backup cleanup succeed.
// All callers use the same transaction lock in addition to HAL's per-operation lock.
namespace atomic_file {
// Bound both serialization and loading to the existing persistence read limit.
inline constexpr size_t MAX_FILE_BYTES = 50000;
bool write(const char* path, const char* data, size_t size);
String read(const char* path, bool* exists = nullptr);
}  // namespace atomic_file
