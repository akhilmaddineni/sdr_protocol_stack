#include "dsp/FirFilterSimd.hpp"
#include <algorithm>
#include <complex>

namespace sdr {

void FirFilterSimd::process(const std::complex<float>* in, std::complex<float>* out, size_t len) {
#ifdef __AVX2__
    // SIMD stub: full AVX2 implementation would use intrinsics here.
#endif
    for (size_t i = 0; i < len; ++i) {
        out[i] = in[i];
    }
}

} // namespace sdr
