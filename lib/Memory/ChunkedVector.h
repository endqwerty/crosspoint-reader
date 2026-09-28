#pragma once

#include <Memory.h>

#include <bit>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>

// Append-only sequence with fallible, non-relocating chunks. Chunks grow to
// MaxChunk elements, then stay fixed; the inline directory bounds total size.
// Elements must be default-constructible. Random access is O(1).
// Keep instantiations few: each argument combination generates separate code.
template <typename T, size_t FirstChunk, size_t MaxChunk, size_t MaxChunks>
class ChunkedVector {
  static_assert(FirstChunk > 0 && std::has_single_bit(FirstChunk));
  static_assert(MaxChunk >= FirstChunk && std::has_single_bit(MaxChunk));

  // Chunks [0, GROWTH_CHUNKS) hold FirstChunk << c elements; the rest hold MaxChunk.
  static constexpr size_t GROWTH_CHUNKS = std::bit_width(MaxChunk / FirstChunk) - 1;
  static constexpr size_t GROWTH_SIZE = FirstChunk * ((size_t{1} << GROWTH_CHUNKS) - 1);
  static_assert(MaxChunks > GROWTH_CHUNKS);

  static constexpr size_t chunkCapacity(const size_t chunk) {
    return chunk < GROWTH_CHUNKS ? FirstChunk << chunk : MaxChunk;
  }

  struct Location {
    size_t chunk;
    size_t offset;
  };
  static constexpr Location locate(const size_t i) {
    if (i < GROWTH_SIZE) {
      const size_t chunk = std::bit_width(i / FirstChunk + 1) - 1;
      return {chunk, i - FirstChunk * ((size_t{1} << chunk) - 1)};
    }
    const size_t j = i - GROWTH_SIZE;
    return {GROWTH_CHUNKS + j / MaxChunk, j % MaxChunk};
  }

 public:
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  static constexpr size_t maxSize() { return GROWTH_SIZE + (MaxChunks - GROWTH_CHUNKS) * MaxChunk; }

  T& operator[](const size_t i) {
    const Location loc = locate(i);
    return chunks_[loc.chunk][loc.offset];
  }
  const T& operator[](const size_t i) const {
    const Location loc = locate(i);
    return chunks_[loc.chunk][loc.offset];
  }
  T& back() { return (*this)[size_ - 1]; }
  const T& back() const { return (*this)[size_ - 1]; }

  // False means the element was not stored: the directory is full, or a chunk
  // allocation failed. Never aborts.
  [[nodiscard]] bool push_back(T value) {
    const Location loc = locate(size_);
    if (loc.chunk >= MaxChunks) return false;
    if (!chunks_[loc.chunk]) {
      chunks_[loc.chunk] = makeUniqueNoThrow<T[]>(chunkCapacity(loc.chunk));
      if (!chunks_[loc.chunk]) return false;
    }
    chunks_[loc.chunk][loc.offset] = std::move(value);
    size_++;
    return true;
  }

  // Mutable random access allows the build indexes to use std::sort/lower_bound.
  class iterator {
   public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;
    iterator() = default;
    iterator(ChunkedVector* owner, const size_t index) : owner_(owner), index_(index) {}
    reference operator*() const { return (*owner_)[index_]; }
    pointer operator->() const { return &**this; }
    reference operator[](const difference_type n) const { return *(*this + n); }
    iterator& operator++() {
      ++index_;
      return *this;
    }
    iterator operator++(int) {
      auto old = *this;
      ++*this;
      return old;
    }
    iterator& operator--() {
      --index_;
      return *this;
    }
    iterator operator--(int) {
      auto old = *this;
      --*this;
      return old;
    }
    iterator& operator+=(const difference_type n) {
      index_ = static_cast<size_t>(static_cast<difference_type>(index_) + n);
      return *this;
    }
    iterator& operator-=(const difference_type n) { return *this += -n; }
    iterator operator+(const difference_type n) const {
      auto result = *this;
      return result += n;
    }
    iterator operator-(const difference_type n) const {
      auto result = *this;
      return result -= n;
    }
    friend iterator operator+(const difference_type n, const iterator it) { return it + n; }
    difference_type operator-(const iterator other) const {
      return static_cast<difference_type>(index_) - static_cast<difference_type>(other.index_);
    }
    bool operator==(const iterator other) const { return owner_ == other.owner_ && index_ == other.index_; }
    bool operator!=(const iterator other) const { return !(*this == other); }
    bool operator<(const iterator other) const { return index_ < other.index_; }
    bool operator>(const iterator other) const { return other < *this; }
    bool operator<=(const iterator other) const { return !(other < *this); }
    bool operator>=(const iterator other) const { return !(*this < other); }

   private:
    ChunkedVector* owner_ = nullptr;
    size_t index_ = 0;
  };

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, size_); }

  class const_iterator {
   public:
    const_iterator(const ChunkedVector* owner, const size_t index) : owner_(owner), index_(index) {}
    const T& operator*() const { return (*owner_)[index_]; }
    const_iterator& operator++() {
      index_++;
      return *this;
    }
    bool operator!=(const const_iterator& other) const { return index_ != other.index_; }

   private:
    const ChunkedVector* owner_;
    size_t index_;
  };

  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, size_); }

 private:
  std::unique_ptr<T[]> chunks_[MaxChunks];
  size_t size_ = 0;
};
