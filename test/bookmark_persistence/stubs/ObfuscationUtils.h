#pragma once
#include <cstddef>
#include <string>

// Credential decoding is outside the persistence/bookmark JSON test boundary.
namespace obfuscation {
inline std::string deobfuscateFromBase64(const char*, size_t, bool* ok, bool* tooLong) {
  *ok = false;
  *tooLong = false;
  return {};
}
}  // namespace obfuscation
