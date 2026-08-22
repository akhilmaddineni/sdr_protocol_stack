#pragma once

#include <atomic>
#include <cstddef>
#include <complex>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

namespace sdr {

// Lock-free Single-Producer-Single-Consumer ring buffer.
// Capacity must be a power of two.
// Zero-copy: pop() returns pointer to internal storage; caller must consume().
// Memory-aligned to 32 bytes for AVX compatibility.
template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacity);
    ~SpscRingBuffer();

    // Non-copyable, non-movable
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // Push from producer thread only.
    bool push(const T* data, size_t len);

    // Pop from consumer thread only. Returns available contiguous length.
    // buffer_ref is set to internal pointer (zero-copy read).
    size_t pop(T*& buffer_ref);

    // Advance tail after reading from buffer_ref.
    void consume(size_t len);

    size_t capacity() const { return m_capacity; }
    size_t available() const;

private:
    static constexpr size_t CACHE_LINE_SIZE = 64;

    size_t capacity_mask() const { return m_capacity - 1; }
    bool is_power_of_two(size_t n) const { return (n & (n - 1)) == 0; }

    // Separate head (producer) and tail (consumer) to prevent false sharing.
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_head{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_tail{0};

    size_t m_capacity{0};
    std::vector<T> m_buffer;
};

} // namespace sdr
