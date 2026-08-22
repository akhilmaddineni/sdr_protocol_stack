#include "buffer/SpscRingBuffer.hpp"
#include <stdexcept>
#include <algorithm>

namespace sdr {

template <typename T>
SpscRingBuffer<T>::SpscRingBuffer(size_t capacity) {
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
        throw std::invalid_argument("SpscRingBuffer capacity must be a power of two");
    }
    m_capacity = capacity;
    m_buffer.resize(m_capacity); // Aligned allocator handles storage
}

template <typename T>
SpscRingBuffer<T>::~SpscRingBuffer() = default;

template <typename T>
bool SpscRingBuffer<T>::push(const T* data, size_t len) {
    if (len == 0 || len > m_capacity) return false;

    size_t head = m_head.load(std::memory_order_relaxed);
    size_t tail = m_tail.load(std::memory_order_acquire);
    if (head - tail >= m_capacity) return false; // Full
    if (m_capacity - (head - tail) < len) return false;

    size_t start_index = head & (m_capacity - 1);

    // Reject wrapped writes for simple contract
    if (start_index + len > m_capacity) return false;

    std::copy(data, data + len, m_buffer.data() + start_index);

    m_head.store(head + len, std::memory_order_release);
    return true;
}

template <typename T>
size_t SpscRingBuffer<T>::pop(T*& buffer_ref) {
    size_t head = m_head.load(std::memory_order_acquire);
    size_t tail = m_tail.load(std::memory_order_relaxed);

    if (head == tail) {
        buffer_ref = nullptr;
        return 0;
    }

    size_t start_index = tail & (m_capacity - 1);
    size_t available = head - tail;
    size_t contiguous = std::min(available, m_capacity - start_index);

    buffer_ref = m_buffer.data() + start_index;
    return contiguous;
}

template <typename T>
void SpscRingBuffer<T>::consume(size_t len) {
    size_t tail = m_tail.load(std::memory_order_relaxed);
    size_t new_tail = tail + len;
    m_tail.store(new_tail, std::memory_order_release);
}

template <typename T>
size_t SpscRingBuffer<T>::available() const {
    size_t head = m_head.load(std::memory_order_acquire);
    size_t tail = m_tail.load(std::memory_order_acquire);
    return head >= tail ? (head - tail) : (m_capacity - (tail - head));
}

// Explicit instantiation for std::complex<float>
template class SpscRingBuffer<std::complex<float>>;

} // namespace sdr
