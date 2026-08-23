# SDR Protocol Stack — Integrated First-Principles Guide

This index links the foundational concepts (`IQ Sampling`, `Ring Buffer`, `DSP FIR`, `Plugin Architecture`, `Threading`) into a single coherent pipeline explanation, and routes you to the right deep dive.

## Quick Start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
cd build && ctest --output-on-failure        # 10 tests must pass
cd .. && cp tests/mock_data.iq mock_data.iq
./build/src/sdr_main                         # Enter stops it; rm mock_data.iq after
```

---

## 1. End-to-End Data Flow (Conceptual)

```
RF Antenna
    ↓ (electromagnetic wave)
Mixer / Zero-IF Receiver (0° I + 90° Q)
    ↓ (analog baseband)
ADC (uint8 I + Q samples, interleaved)
    ↓ (binary .iq file or USB stream)
FileSource / RtlSdrSource (normalization: (uint8 - 127.5)/128)
    ↓ (std::complex<float> array)
SpscRingBuffer (lock-free SPSC queue)
    ↓ (pop → process → consume)
FirFilter / FirFilterSimd (scalar or AVX2 convolution)
    ↓ (filtered std::complex<float> array)
IDecoder / AdsBDecoder (plugin loaded via dlopen/create_decoder)
    ↓ (decoded packets)
Telemetry / Network Output
```

---

## 2. Integrated Mermaid Data Flow

```mermaid
flowchart LR
    subgraph RF_In[RF Ingestion]
        A[RF Antenna] --> B[Mixer 0°/90°]
        B --> C[ADC uint8 I+Q]
    end

    subgraph Source[Source Layer]
        C --> D[FileSource / RtlSdrSource]
        D --> E[Normalize (uint8-127.5)/128]
    end

    subgraph Memory[Memory Layer]
        E --> F[SpscRingBuffer push]
        F --> G[Buffer State: head, tail, mask, capacity]
    end

    subgraph DSP[DSP Layer]
        G --> H[pop ptr + consume]
        H --> I[FirFilter scalar / AVX2 SIMD]
        I --> J[Filtered output array]
    end

    subgraph Decode[Decode Layer]
        J --> K[PluginManager load .so]
        K --> L[create_decoder factory]
        L --> M[AdsBDecoder accept_samples]
    end

    subgraph Out[Output Layer]
        M --> N[Telemetry / JSON / UDP]
    end
```

---

## 3. Cross-Reference Table

| Component | Implementation File | Design Spec | First-Principles Doc |
|---|---|---|---|
| RF / ADC / Normalization | `src/ingress/FileSource.cpp` | `LLD.md` §3.A | `docs/01_iq_sampling.md` |
| Lock-Free Ring Buffer | `include/buffer/SpscRingBuffer.hpp`<br>`src/buffer/SpscRingBuffer.cpp` | `LLD.md` §3.B | `docs/02_ring_buffer.md` |
| FIR Filter (Scalar) | `include/dsp/DspBlock.hpp`<br>`src/dsp/FirFilter.cpp` | `LLD.md` §3.C | `docs/03_dsp_fir.md` |
| FIR Filter (SIMD) | `include/dsp/FirFilterSimd.hpp`<br>`src/dsp/FirFilterSimd.cpp` (stub) | `LLD.md` §3.C | `docs/03_dsp_fir.md`, `docs/08_benchmarks.md` |
| Plugin Architecture | `include/core/IDecoder.hpp`<br>`include/core/PluginManager.hpp` | `LLD.md` §3.D | `docs/04_plugin_arch.md`, `docs/07_plugin_authoring.md` |
| Thread Orchestration | `include/core/Orchestrator.hpp`<br>`src/core/Orchestrator.cpp` | `LLD.md` §2, §3.E | `docs/05_threading_orchestration.md` |
| Mock Data / CI | `tests/mock_data.iq` | `LLD.md` §5 | `docs/06_testing_and_ci.md` |
| Benchmark | `src/benchmark.cpp` (no CMake target) | `LLD.md` §8 | `docs/08_benchmarks.md` |

---

## 4. Key Design Decisions Explained

### Why C++20?
- `std::atomic` with explicit memory orders (`release`, `acquire`) — required for lock-free correctness.
- `std::complex<float>` native support (no manual `float` pair management).
- `alignas` for cache-line alignment (false sharing prevention).
- `std::function` for callback interfaces (`DataCallback`).

### Why Lock-Free Instead of `std::queue` + `std::mutex`?
- `std::mutex` blocks the USB thread; RF samples are lost permanently during blocking.
- Lock-free SPSC guarantees bounded latency (no priority inversion, no deadlock).
- Trade-off: more complex arithmetic (bitmask, unmasked counters, overflow checks) but deterministic performance.

### Why Plugin Factory Pattern (`create_decoder`)?
- The pipeline does not include plugin source at compile time.
- New protocols (e.g., ADS-B, ACARS, AIS) can be added as `.so` files without rebuilding `sdr_main`.
- The manager owns decoder instances (`std::unique_ptr`) to prevent memory leaks.

### Why Mock `.iq` Injection?
- Physical RTL-SDR hardware requires USB access, drivers (`librtlsdr`), and antenna setup — impractical in CI.
- Binary `.iq` files are deterministic: same input always produces same pipeline state and same output packets.
- Enables automated regression tests (`tests/unit/`, `tests/integration/`) without RF hardware.

---

## 5. Performance Targets (LLD §8)

- Hot path allocations: zero per sample (`push` / `pop` / `process`).
- Memory footprint: `< 64 MB` (ring buffer + taps + plugin code).
- DSP: scalar FIR baseline; SIMD (`AVX2`) path exists but is a passthrough stub today (`docs/08_benchmarks.md`).
- Thread priorities: all threads run at default priority; `SCHED_FIFO` is an optional roadmap item.
- Shutdown order: flag off → ingress stop/join → DSP join → output join (`docs/05_threading_orchestration.md` §3).

---

## 6. Error Handling (LLD §4)

- `FileSource`: file open failure logs to `std::cerr`, clears `m_running`, thread exits cleanly; pipeline idles on an empty ring buffer.
- `SpscRingBuffer`: overflow / oversize / wrap-splitting `push` returns `false` — caller (ingress) currently drops; no blocking.
- Non-power-of-two capacity throws `std::invalid_argument` from the constructor.
- `PluginManager`: `.so` load failure logs `dlerror()`; missing plugin dir logs and returns `false`; pipeline continues with existing decoders.
- `Orchestrator`: start without a source fails fast; stop is idempotent and always joins spawned threads (`docs/05_threading_orchestration.md`).

---

## 7. References

- `docs/01_iq_sampling.md` — ADC, complex baseband, normalization.
- `docs/02_ring_buffer.md` — Lock-free SPSC, memory ordering, false sharing, zero-copy contract.
- `docs/03_dsp_fir.md` — FIR convolution, scalar vs SIMD (stub status).
- `docs/04_plugin_arch.md` — `dlopen`, factory functions, plugin lifecycle, security.
- `docs/05_threading_orchestration.md` — Thread model, startup/shutdown sequencing, join-contract post-mortem.
- `docs/06_testing_and_ci.md` — Test inventory, mock `.iq` format, CWD rules, CI recipe, known gaps.
- `docs/07_plugin_authoring.md` — Cookbook for writing and building your own decoder plugin.
- `docs/08_benchmarks.md` — Benchmark status and methodology; profiling how-to.
- `LLD.md` — Full low-level design specification (directory mapping, interface contracts, performance constraints).
