#include "core/IDecoder.hpp"
#include <string>
#include <complex>
#include <cstddef>
#include <iostream>

namespace sdr {

class AdsBDecoder : public IDecoder {
public:
    std::string get_name() const override { return "adsb"; }
    void accept_samples(const std::complex<float>* data, size_t len) override {
        // Stub: in production, demodulate PPM and decode Mode-S packets.
        std::cout << "AdsBDecoder: received " << len << " samples" << std::endl;
    }
};

extern "C" sdr::IDecoder* create_decoder() {
    return new AdsBDecoder();
}

} // namespace sdr
