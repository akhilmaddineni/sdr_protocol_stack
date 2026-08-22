#pragma once

#include <complex>
#include <functional>
#include <cstdint>
#include <cstddef>

namespace sdr {

// Callback type for receiving normalized IQ data (complex floats)
using DataCallback = std::function<void(const std::complex<float>*, std::size_t)>;

class ISource {
public:
    virtual ~ISource() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void setCallback(DataCallback cb) = 0;
};

} // namespace sdr
