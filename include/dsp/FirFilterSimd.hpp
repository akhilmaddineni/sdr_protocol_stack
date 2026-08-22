#pragma once
#include <complex>
#include <cstddef>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace sdr {

// SIMD FIR using AVX2 (8 floats, 4 complex pairs per instruction)
// Taps must be aligned to 32 bytes.
class FirFilterSimd {
public:
    void process(const std::complex<float>* in, std::complex<float>* out, size_t len);
};

} // namespace sdr
