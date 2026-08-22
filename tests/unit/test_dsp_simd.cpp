#include <gtest/gtest.h>
#include "dsp/FirFilter.hpp"
#include "dsp/FirFilterSimd.hpp"
#include <complex>
#include <cmath>

TEST(FirFilterSimdTest, BasicProcess) {
    sdr::FirFilterSimd simd;
    std::complex<float> in[] = {{1.0f, 0.0f}, {2.0f, 0.0f}, {3.0f, 0.0f}};
    std::complex<float> out[3] = {};
    simd.process(in, out, 3);
    EXPECT_FLOAT_EQ(out[0].real(), 1.0f);
}
