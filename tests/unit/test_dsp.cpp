#include <gtest/gtest.h>
#include "dsp/FirFilter.hpp"
#include <complex>

TEST(FirFilterTest, BasicProcess) {
    std::vector<float> taps = {1.0f, 0.0f, 0.0f};
    sdr::FirFilter filter(taps);
    std::complex<float> in[] = {{1.0f, 0.0f}, {2.0f, 0.0f}, {3.0f, 0.0f}};
    std::complex<float> out[3] = {};
    filter.process(in, out, 3);
    EXPECT_FLOAT_EQ(out[0].real(), 1.0f);
    EXPECT_FLOAT_EQ(out[1].real(), 2.0f);
    EXPECT_FLOAT_EQ(out[2].real(), 3.0f);
}
