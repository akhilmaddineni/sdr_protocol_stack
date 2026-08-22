#include <gtest/gtest.h>
#include "buffer/SpscRingBuffer.hpp"
#include <complex>

TEST(SpscRingBufferTest, PushPopBasic) {
    sdr::SpscRingBuffer<std::complex<float>> rb(4);
    std::complex<float> data[] = {{1.0f, 2.0f}, {3.0f, 4.0f}};
    EXPECT_TRUE(rb.push(data, 2));
    std::complex<float>* ptr = nullptr;
    size_t len = rb.pop(ptr);
    EXPECT_EQ(len, 2);
    EXPECT_NE(ptr, nullptr);
    rb.consume(2);
}

TEST(SpscRingBufferTest, EmptyPopReturnsZero) {
    sdr::SpscRingBuffer<std::complex<float>> rb(4);
    std::complex<float>* ptr = nullptr;
    EXPECT_EQ(rb.pop(ptr), 0);
}
