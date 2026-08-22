#pragma once
#include "DspBlock.hpp"
#include <vector>
#include <complex>
#include <cstddef>

namespace sdr {

class FirFilter : public DspBlock {
public:
    explicit FirFilter(const std::vector<float>& taps);
    ~FirFilter() override = default;

    void process(const std::complex<float>* in, std::complex<float>* out, size_t len) override;

    const std::vector<float>& taps() const { return m_taps; }

private:
    std::vector<float> m_taps;
    size_t m_tap_count{0};
};

} // namespace sdr
