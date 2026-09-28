#pragma once
#include <HalStorage.h>

#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

namespace serialization {
inline bool writeBytesChecked(HalFile& file, const void* bytes, const size_t size) {
  return size == 0 || file.write(reinterpret_cast<const uint8_t*>(bytes), size) == size;
}

inline bool readBytesChecked(HalFile& file, void* bytes, const size_t size) {
  return size == 0 || (size <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
                       file.read(bytes, size) == static_cast<int>(size));
}

template <typename T>
bool writePodChecked(HalFile& file, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  return writeBytesChecked(file, &value, sizeof(T));
}

template <typename T>
bool readPodChecked(HalFile& file, T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if constexpr (std::is_same_v<T, bool>) {
    static_assert(sizeof(bool) == sizeof(uint8_t));
    uint8_t byte = 0;
    if (!readBytesChecked(file, &byte, sizeof(byte)) || byte > 1) return false;
    value = byte != 0;
  } else {
    T decoded{};
    if (!readBytesChecked(file, &decoded, sizeof(decoded))) return false;
    value = decoded;
  }
  return true;
}

inline bool writeStringChecked(HalFile& file, const std::string_view value) {
  if (value.size() > std::numeric_limits<uint32_t>::max()) return false;
  return writePodChecked(file, static_cast<uint32_t>(value.size())) &&
         writeBytesChecked(file, value.data(), value.size());
}

// Callers bound retained string storage for their format. A failed payload read
// clears the partial string; length failures leave the destination unchanged.
inline bool readStringChecked(HalFile& file, std::string& value, const size_t maxBytes) {
  uint32_t size = 0;
  if (!readPodChecked(file, size) || size > maxBytes) return false;
  if (size == 0) {
    value.clear();
    return true;
  }
  const auto position = file.position();
  const auto fileSize = file.size();
  if (position > fileSize || size > fileSize - position) return false;
  value.resize(size);
  if (readBytesChecked(file, value.data(), size)) return true;
  value.clear();
  return false;
}

template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
void readPod(HalFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

inline void readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  s.resize(len);
  is.read(&s[0], len);
}

inline void readString(HalFile& file, std::string& s) {
  uint32_t len;
  readPod(file, len);
  s.resize(len);
  file.read(&s[0], len);
}
}  // namespace serialization
