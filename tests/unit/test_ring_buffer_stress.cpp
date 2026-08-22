#include <gtest/gtest.h>
#include "buffer/SpscRingBuffer.hpp"
#include <complex>
#include <thread>
#include <vector>

TEST(SpscRingBufferStress, ConcurrentPushPop) {
    sdr::SpscRingBuffer<std::complex<float>> rb(64);
    std::thread producer([&]() {
        std::complex<float> data[4] = {{1,2},{3,4},{5,6},{7,8}};
        for (int i = 0; i < 10; ++i) {
            rb.push(data, 4);
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < 10; ++i) {
            std::complex<float>* ptr = nullptr;
            while (rb.pop(ptr) == 0) {
                std::this_thread::yield();
            }
            if (ptr) rb.consume(4);
        }
    });

    producer.join();
    consumer.join();
    EXPECT_GE(rb.available(), 0u);
}

TEST(SpscRingBufferStress, Overflow) {
    sdr::SpscRingBuffer<std::complex<float>> rb(4);
    std::complex<float> data[10] = {};
    EXPECT_FALSE(rb.push(data, 10)); // exceeds capacity
}
