#include "dsp/FirFilter.hpp"
#include <algorithm>
#include <complex>

namespace sdr {

FirFilter::FirFilter(const std::vector<float>& taps) : m_taps(taps), m_tap_count(taps.size()) {}

void FirFilter::process(const std::complex<float>* in, std::complex<float>* out, size_t len) {
    if (m_tap_count == 0 || len == 0) {
        for (size_t i = 0; i < len; ++i) out[i] = in[i];
        return;
    }

    // Scalar FIR: simple convolution (naive, for demonstration)
    for (size_t i = 0; i < len; ++i) {
        std::complex<float> sum(0.0f, 0.0f);
        for (size_t j = 0; j < m_tap_count; ++j) {
            int idx = static_cast<int>(i) - static_cast<int>(j);
            if (idx >= 0) {
                sum += in[idx] * std::complex<float>(m_taps[j], 0.0f);
            }
        }
        out[i] = sum;
    }
}

} // namespace sdr
