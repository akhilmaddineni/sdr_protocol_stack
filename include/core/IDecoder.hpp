#pragma once
#include <string>
#include <complex>
#include <cstddef>

namespace sdr {

class IDecoder {
public:
    virtual ~IDecoder() = default;
    virtual std::string get_name() const = 0;
    virtual void accept_samples(const std::complex<float>* data, size_t len) = 0;
};

} // namespace sdr
