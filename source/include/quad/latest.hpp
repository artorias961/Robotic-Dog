#pragma once
// Latest<T>: the "latest value wins" slot from the diagrams, PC-side flavor.
// Writer never blocks; readers retry if they raced a write (seqlock pattern).
// T must be trivially copyable.
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace quad {

template <typename T>
  requires std::is_trivially_copyable_v<T>
class Latest {
 public:
  void store(const T& value) {
    const std::uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_release);   // odd = write in progress
    value_ = value;
    seq_.store(s + 2, std::memory_order_release);   // even = stable
  }

  T load() const {
    T out;
    std::uint32_t before, after;
    do {
      before = seq_.load(std::memory_order_acquire);
      out    = value_;
      after  = seq_.load(std::memory_order_acquire);
    } while (before != after || (before & 1u));
    return out;
  }

 private:
  std::atomic<std::uint32_t> seq_{0};
  T value_{};
};

}  // namespace quad
