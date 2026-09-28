#include "ScopedAllocationFailure.h"

#include <new>

namespace parser_test {

thread_local ScopedAllocationFailure* ScopedAllocationFailure::active_ = nullptr;

ScopedAllocationFailure::ScopedAllocationFailure(Kind kind, size_t bytes, size_t failOnMatch, size_t failCount)
    : kind_(kind), bytes_(bytes), failOnMatch_(failOnMatch), failCount_(failCount), previous_(active_) {
  active_ = this;
}

ScopedAllocationFailure::~ScopedAllocationFailure() { active_ = previous_; }

bool ScopedAllocationFailure::shouldFail(Kind kind, size_t bytes) {
  if (!active_ || (active_->kind_ != Kind::Any && active_->kind_ != kind) ||
      (active_->bytes_ != 0 && active_->bytes_ != bytes))
    return false;
  ++active_->matchingCalls_;
  if (active_->failOnMatch_ == 0 || active_->matchingCalls_ < active_->failOnMatch_ ||
      active_->matchingCalls_ - active_->failOnMatch_ >= active_->failCount_)
    return false;
  ++active_->failures_;
  return true;
}

}  // namespace parser_test

void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
  if (parser_test::ScopedAllocationFailure::shouldFail(parser_test::ScopedAllocationFailure::Kind::Object, bytes))
    return nullptr;
  try {
    return ::operator new(bytes);
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

void* operator new[](size_t bytes, const std::nothrow_t&) noexcept {
  if (parser_test::ScopedAllocationFailure::shouldFail(parser_test::ScopedAllocationFailure::Kind::Array, bytes))
    return nullptr;
  try {
    return ::operator new[](bytes);
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}
