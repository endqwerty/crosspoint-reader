#pragma once

#include <cstddef>

namespace parser_test {

// Only intercept explicitly nothrow allocations on this test thread. GTest,
// std::vector and Expat allocations are unaffected by an armed fault.
class ScopedAllocationFailure {
 public:
  enum class Kind { Object, Array, Any };

  explicit ScopedAllocationFailure(Kind kind, size_t bytes = 0, size_t failOnMatch = 1, size_t failCount = 1);
  ~ScopedAllocationFailure();
  ScopedAllocationFailure(const ScopedAllocationFailure&) = delete;
  ScopedAllocationFailure& operator=(const ScopedAllocationFailure&) = delete;

  size_t failures() const { return failures_; }
  size_t matchingCalls() const { return matchingCalls_; }
  static bool shouldFail(Kind kind, size_t bytes);

 private:
  Kind kind_;
  size_t bytes_;
  size_t failOnMatch_;
  size_t failCount_;
  size_t failures_ = 0;
  size_t matchingCalls_ = 0;
  ScopedAllocationFailure* previous_;
  static thread_local ScopedAllocationFailure* active_;
};

}  // namespace parser_test
