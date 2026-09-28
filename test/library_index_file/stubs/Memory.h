#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

inline bool failNextIndexAllocation = false;

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (std::exchange(failNextIndexAllocation, false)) return nullptr;
  return std::make_unique<T>(count);
}
