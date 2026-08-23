# SDR Protocol Stack — Integrated First-Principles Guide

This index links the four foundational concepts (`IQ Sampling`, `Ring Buffer`, `DSP FIR`, `Plugin Architecture`) into a single coherent pipeline explanation. It is intended for staff-level embedded engineers who need to understand both the mathematics and the software architecture.

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
| RF / ADC / Normalization | `src/ingress/FileSource.cpp` | `LLD.md` §2.A | `docs/01_iq_sampling.md` |
| Lock-Free Ring Buffer | `include/buffer/SpscRingBuffer.hpp`<br>`src/buffer/SpscRingBuffer.cpp` | `LLD.md` §2.B | `docs/02_ring_buffer.md` |
| FIR Filter (Scalar) | `include/dsp/DspBlock.hpp`<br>`src/dsp/FirFilter.cpp` | `LLD.md` §2.C | `docs/03_dsp_fir.md` |
| FIR Filter (SIMD) | `include/dsp/FirFilterSimd.hpp`<br>`src/dsp/FirFilterSimd.cpp` | `LLD.md` §2.C, §3.2 | `docs/03_dsp_fir.md` |
| Plugin Architecture | `include/core/IDecoder.hpp`<br>`include/core/PluginManager.hpp` | `LLD.md` §2.D | `docs/04_plugin_arch.md` |
| Thread Orchestration | `include/core/Orchestrator.hpp`<br>`src/core/Orchestrator.cpp` | `LLD.md` §2.E | `LLD.md` |
| Mock Data / CI | `tests/mock_data.iq` | `LLD.md` §4 | `docs/01_iq_sampling.md` |
| Benchmark | `src/benchmark.cpp` | `LLD.md` §8 | `docs/index.md` |

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
- DSP: scalar FIR baseline; SIMD (`AVX2`) provides 2-4x throughput for aligned buffers.
- Thread priorities: ingress (`SCHED_FIFO` optional, high); DSP (normal); output (low).
- Shutdown order: reverse creation (output → DSP → ingress) to prevent dangling references.

---

## 6. Error Handling (LLD §4)

- `FileSource`: file open failure logs to `std::cerr`, sets `m_running = false`, exits without spawning thread.
- `SpscRingBuffer`: overflow (`push` returns `false`) — caller (ingress) must retry or drop; no blocking.
- `PluginManager`: `.so` load failure logs `dlerror()`; pipeline continues with existing decoders.
- `Orchestrator`: any thread exception triggers cascading `stop()`; all threads joined before destruction.
- Memory alignment failure (`posix_memalign`) throws `std::bad_alloc`; caught by orchestrator.

---

## 7. References

- `docs/01_iq_sampling.md` — ADC, complex baseband, normalization.
- `docs/02_ring_buffer.md` — Lock-free SPSC, memory ordering, false sharing.
- `docs/03_dsp_fir.md` — FIR convolution, scalar vs SIMD.
- `docs/04_plugin_arch.md` — `dlopen`, factory functions, plugin lifecycle.
- `LLD.md` — Full low-level design specification (directory mapping, interface contracts, performance constraints).
