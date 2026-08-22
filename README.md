# SDR Protocol Stack

A user-space C++20 Software-Defined Radio (SDR) pipeline that ingests raw IQ samples, applies DSP processing, and decodes protocol packets through a runtime plugin architecture.

## Overview

This project demonstrates staff-level embedded systems engineering by combining:

- **Lock-free concurrency**: A single-producer/single-consumer (SPSC) ring buffer moves high-throughput IQ data without mutex contention.
- **Hardware-aware DSP**: Scalar and SIMD-aligned FIR filter implementations with AVX2 support.
- **Dynamic plugin loading**: Protocol decoders (e.g., ADS-B) are loaded at runtime via `dlopen()` without pipeline restarts.
- **Deterministic testing**: Mock `.iq` binary files allow reproducible CI benchmarks without physical RF hardware.

Full architectural design is documented in [`LLD.md`](LLD.md).

## Directory Structure

```
sdr_protocol_stack/
├── CMakeLists.txt              # Root build (C++20, Threads, GTest)
├── .gitignore
├── README.md
├── LLD.md                       # Low-Level Design specification
├── include/
│   ├── ISource.hpp              # Ingress interface (FileSource / RtlSdrSource)
│   ├── buffer/
│   │   └── SpscRingBuffer.hpp   # Lock-free SPSC queue
│   ├── dsp/
│   │   ├── DspBlock.hpp         # DSP pipeline interface
│   │   ├── FirFilter.hpp        # Scalar FIR
│   │   └── FirFilterSimd.hpp    # SIMD FIR (AVX2 guarded)
│   └── core/
│       ├── IDecoder.hpp         # Plugin decoder interface
│       ├── PluginManager.hpp    # Runtime .so loader
│       └── Orchestrator.hpp     # Thread lifecycle manager
├── src/
│   ├── main.cpp                 # Pipeline bootstrap
│   ├── benchmark.cpp            # Mock data benchmark
│   ├── ingress/
│   │   ├── FileSource.hpp/.cpp  # Binary .iq file ingestion
│   │   └── (RtlSdrSource stub)  # RTL-SDR wrapper (future)
│   ├── buffer/
│   │   └── SpscRingBuffer.cpp
│   ├── dsp/
│   │   ├── FirFilter.cpp
│   │   └── FirFilterSimd.cpp
│   └── core/
│       ├── Orchestrator.cpp
│       └── PluginManager.cpp
├── plugins/
│   └── adsb/
│       └── adsb_decoder.cpp     # Example ADS-B plugin with factory export
└── tests/
    ├── CMakeLists.txt
    ├── unit/
    │   ├── test_ring_buffer.cpp
    │   ├── test_ring_buffer_stress.cpp
    │   ├── test_dsp.cpp
    │   ├── test_dsp_simd.cpp
    │   ├── test_plugin_loader.cpp
    │   └── test_thread_shutdown.cpp
    ├── integration/
    │   ├── test_file_to_decoder.cpp
    │   └── test_pipeline_end_to_end.cpp
    └── mock_data.iq             # Small binary mock for CI
```

## Build

Requires:
- CMake >= 3.20
- C++20 compiler (Clang/GCC/MSVC)
- POSIX threads (`Threads` package)

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Run

```bash
# Main executable
./src/sdr_main

# Benchmark (writes temporary mock .iq, runs pipeline, cleans up)
./src/benchmark  # or build target
```

## Tests

Unit and integration tests use GoogleTest (fetched via CMake `FetchContent`).

```bash
cd build
make unit_tests integration_tests
./tests/unit_tests        # 8 tests (ring buffer, DSP, plugin loader, shutdown)
./tests/integration_tests # End-to-end pipeline verification
```

## Key Design Points (see LLD.md)

- **Zero-copy ring buffer**: `SpscRingBuffer<T>` uses `std::atomic<size_t>` with `memory_order_release` (producer) and `memory_order_acquire` (consumer). Capacity must be a power of two; array indexing uses bitmask for fast wrap-around.
- **Thread model**: Ingress (high priority, `SCHED_FIFO` optional) pushes raw bytes; DSP (normal) applies FIR; Output (low) pushes decoded packets. Shutdown joins in reverse order (output -> DSP -> ingress) with safe `std::thread::join()` guards.
- **Plugin contract**: Every plugin exports `extern "C" IDecoder* create_decoder()`. `PluginManager` scans `.so`/`.dylib` files, loads with `dlopen()`, and owns the decoder lifecycle.
- **Mock injection**: `FileSource` reads interleaved 8-bit unsigned IQ (`uint8_t I`, `uint8_t Q`) normalized to `[-1.0, 1.0]` using `(val - 127.5) / 128.0`. Pre-recorded `.iq` files enable deterministic CI without RF hardware.

## Performance & Constraints

- Hot path contains zero heap allocations per sample.
- DSP FIR is scalar by default; SIMD path (`FirFilterSimd`) is guarded behind `#ifdef __AVX2__` and requires 32-byte aligned input buffers.
- Memory footprint target: < 64 MB (ring buffers + taps + plugin code).

## References

- Low-Level Design: [`LLD.md`](LLD.md)
- POSIX Threads: *Programming with POSIX Threads* (Butenhof)
- Modern C++: *Effective Modern C++* (Meyers)
- DSP library reference: `liquid-dsp` (open-source)
- Lock-free SPSC: CppCon talks (Fedor Pikus / Herb Sutter)
