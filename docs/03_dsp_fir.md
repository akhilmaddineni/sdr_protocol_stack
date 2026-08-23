# First Principles: FIR Filtering, Convolution, and SIMD

## 1. Convolution Mathematics

A Finite Impulse Response (FIR) filter produces output `y[n]` by convolving input `x[n]` with a finite-length impulse response `h[k]` (the filter taps):

```
y[n] = Σ(k=0 to M-1) h[k] · x[n - k]
```

Where `M` is the number of taps (`tap_count`). Each output sample is a weighted sum of the current input and the previous `M-1` inputs. This is a sliding-window operation: as the window slides forward by one sample, the oldest input drops out, a new input enters, and the sum is recomputed.

Reference implementation (`src/dsp/FirFilter.cpp`):

```cpp
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
```

Note: The scalar version does not maintain internal state across calls; it treats each `process()` invocation as an independent convolution over the provided `len` samples. In production, a stateful FIR would store previous `M-1` samples between calls to avoid the `if (idx >= 0)` guard (which creates a transient at the start of each chunk).

## 2. Tap Design and Filter Response

The frequency response of an FIR filter is the Discrete Fourier Transform (DFT) of its taps:

```
H(f) = Σ(k=0 to M-1) h[k] · e^(-j·2π·f·k/Fs)
```

For a simple low-pass filter (smooth average), taps are often symmetric (`h[k] = h[M-1-k]`), which guarantees linear phase (constant group delay — important for signal integrity). The `benchmark.cpp` uses a simple 3-tap filter: `{0.2, 0.4, 0.2}` (triangular/smoothing window).

## 3. Scalar vs SIMD (AVX2)

A scalar loop computes one `y[n]` per iteration. AVX2 (`__m256`) processes 8 floats or 4 complex pairs (`std::complex<float>`) in parallel using 256-bit registers. To use SIMD:

- Taps must be aligned to 32-byte boundaries (`alignas(32)`).
- Input buffers should be a multiple of 4 or 8 complex samples to avoid partial-vector tail handling.
- Unaligned loads (`_mm256_loadu_ps`) work but are slightly slower than aligned loads (`_mm256_load_ps`).

The `FirFilterSimd` class (`include/dsp/FirFilterSimd.hpp`) is guarded by `#ifdef __AVX2__`. **Current status: the SIMD implementation is a passthrough stub** — `process()` copies input to output without filtering or intrinsics (`src/dsp/FirFilterSimd.cpp`). The scalar `FirFilter` is the only real filter today; treat `FirFilterSimd` as an interface placeholder for the AVX2 port (see `docs/08_benchmarks.md` before quoting speedups).

## 4. Alignment Requirements

SIMD instructions (`_mm256_load_ps`, `_mm256_store_ps`) require memory addresses divisible by 32 bytes. If a pointer is misaligned, the instruction may trigger a hardware exception or fall back to slow unaligned access. The design uses `std::vector<T>` with default allocator; for production SIMD, taps and buffers should be allocated with `posix_memalign(32, ...)` or `std::align`.

Reference (`LLD.md`):

```
- Stores filter taps aligned to 32-byte (AVX) or 16-byte (NEON) boundaries.
- Will include both scalar and SIMD (FirFilterSimd) implementations for benchmarking.
```

## 5. Pipeline Integration

The `DspBlock` interface (`include/dsp/DspBlock.hpp`) abstracts the filter:

```cpp
class DspBlock {
public:
    virtual void process(const std::complex<float>* in,
                         std::complex<float>* out, size_t len) = 0;
};
```

`Orchestrator::dsp_loop()` pops contiguous samples from the ring buffer, passes them to `process()`, and writes the filtered result back to a fixed-size output buffer (`out_buf`, currently `1024` samples). Note: `to_process = std::min(available, out_buf.size())` prevents overflow.

```mermaid
flowchart TD
    A[Ring Buffer pop] --> B{len <= 1024?}
    B -->|Yes| C[process(ptr, out_buf, len)]
    B -->|No| D[cap to 1024 samples]
    D --> C
    C --> E[consume(len)]
    E --> F[Output Thread / Plugin]
```
