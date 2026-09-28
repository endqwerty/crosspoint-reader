#pragma once

#include <cstddef>
#include <limits>
#include <string>

namespace string_test {
inline size_t capacity = std::numeric_limits<size_t>::max();
inline unsigned concatCalls = 0;
inline unsigned reserveCalls = 0;
inline bool failReserve = false;
inline int concatsBeforeFailure = -1;
inline bool allocationFailed = false;
inline void reset() {
  capacity = std::numeric_limits<size_t>::max();
  concatCalls = 0;
  reserveCalls = 0;
  failReserve = false;
  concatsBeforeFailure = -1;
  allocationFailed = false;
}
}  // namespace string_test

// Model the Arduino String API used by ArduinoJson's actual String writer.
// A failed concat leaves its prior bytes intact, as on the ESP32 implementation.
class String {
  std::string text;

 public:
  String() = default;
  String(const std::string& value) : text(value) {}
  String(const char* value) : text(value ? value : "") {}
  String& operator=(const char* value) {
    text.assign(value ? value : "");
    return *this;
  }
  const char* c_str() const { return text.c_str(); }
  size_t length() const { return text.size(); }
  bool isEmpty() const { return text.empty(); }
  bool empty() const { return text.empty(); }
  bool reserve(const size_t size) {
    ++string_test::reserveCalls;
    if (string_test::failReserve || size > string_test::capacity) {
      string_test::allocationFailed = true;
      return false;
    }
    text.reserve(size);
    return true;
  }
  bool concat(const char* value) { return concat(value, std::char_traits<char>::length(value)); }
  bool concat(const char* value, const size_t count) {
    ++string_test::concatCalls;
    const bool injectedFailure = string_test::concatsBeforeFailure == 0;
    if (string_test::concatsBeforeFailure > 0) --string_test::concatsBeforeFailure;
    if (injectedFailure || count > string_test::capacity || text.size() > string_test::capacity - count) {
      string_test::allocationFailed = true;
      return false;
    }
    text.append(value, count);
    return true;
  }
};
