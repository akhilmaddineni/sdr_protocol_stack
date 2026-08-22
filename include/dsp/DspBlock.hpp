#pragma once
#include <complex>
#include <cstddef>

namespace sdr {

class DspBlock {
public:
    virtual ~DspBlock() = default;
    virtual void process(const std::complex<float>* in, std::complex<float>* out, size_t len) = 0;
};

} // namespace sdr
