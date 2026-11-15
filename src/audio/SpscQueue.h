// Single-producer, single-consumer lock-free queue of trivially copyable items. One thread pushes,
// one other thread pops; neither ever blocks or allocates.
#pragma once
#include <atomic>
#include <cstddef>

namespace twill {

template <typename T, size_t N>  // N must be a power of two
class SpscQueue {
  static_assert((N & (N - 1)) == 0, "capacity must be a power of two");

 public:
  bool push(const T& item) {
    size_t w = write_.load(std::memory_order_relaxed);
    if (w - read_.load(std::memory_order_acquire) == N) return false;  // full
    items_[w & (N - 1)] = item;
    write_.store(w + 1, std::memory_order_release);
    return true;
  }

  bool pop(T& item) {
    size_t r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire)) return false;  // empty
    item = items_[r & (N - 1)];
    read_.store(r + 1, std::memory_order_release);
    return true;
  }

 private:
  T items_[N];
  std::atomic<size_t> write_{0};
  std::atomic<size_t> read_{0};
};

}  // namespace twill
