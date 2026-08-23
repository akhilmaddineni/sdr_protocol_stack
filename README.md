# SDR Protocol Stack

A user-space C++20 Software-Defined Radio (SDR) pipeline that ingests raw IQ samples, applies DSP processing, and decodes protocol packets through a runtime plugin architecture.

## Overview

This project demonstrates staff-level embedded systems engineering by combining:

- **Lock-free concurrency**: A single-producer/single-consumer (SPSC) ring buffer moves high-throughput IQ data without mutex contention.
- **Hardware-aware DSP**: Scalar and SIMD FIR filter implementations with AVX2 support.
- **Dynamic plugin loading**: Protocol decoders (e.g., ADS-B) are loaded at runtime via `dlopen()` without pipeline restarts.
- **Deterministic testing**: Mock `.iq` binary files allow reproducible CI benchmarks without physical RF hardware.

Full architectural design is documented in [`LLD.md`](LLD.md). First-principles deep dives live in [`docs/`](docs/index.md).

## Documentation Map

| Document | Contents |
|---|---|
| [`LLD.md`](LLD.md) | Low-level design specification: components, contracts, threading spec |
| [`docs/01_iq_sampling.md`](docs/01_iq_sampling.md) | IQ sampling, complex baseband, uint8 → float normalization math |
| [`docs/02_ring_buffer.md`](docs/02_ring_buffer.md) | Lock-free SPSC design: memory ordering, false sharing, zero-copy contract |
| [`docs/03_dsp_fir.md`](docs/03_dsp_fir.md) | FIR convolution, tap design, scalar vs SIMD |
| [`docs/04_plugin_arch.md`](docs/04_plugin_arch.md) | `dlopen`/`dlsym`, factory pattern, plugin lifecycle and security |
| [`docs/05_threading_orchestration.md`](docs/05_threading_orchestration.md) | Thread model, lifecycle state machines, shutdown sequencing |
| [`docs/06_testing_and_ci.md`](docs/06_testing_and_ci.md) | Test inventory, mock data format, CWD rules, CI recipe, known gaps |
| [`docs/07_plugin_authoring.md`](docs/07_plugin_authoring.md) | Step-by-step guide to writing and building your own decoder plugin |
| [`docs/08_benchmarks.md`](docs/08_benchmarks.md) | Benchmark methodology, current status, profiling how-to |
| [`simulator/README.md`](simulator/README.md) | Interactive Streamlit simulator for pipeline parameters |

## Directory Structure

```
sdr_protocol_stack/
├── CMakeLists.txt              # Root build (C++20, Threads, GTest)
├── .gitignore
├── README.md
├── LLD.md                       # Low-Level Design specification
├── docs/                        # First-principles guides (see Documentation Map)
│   ├── index.md
│   ├── 01_iq_sampling.md
│   ├── 02_ring_buffer.md
│   ├── 03_dsp_fir.md
│   ├── 04_plugin_arch.md
│   ├── 05_threading_orchestration.md
│   ├── 06_testing_and_ci.md
│   ├── 07_plugin_authoring.md
│   └── 08_benchmarks.md
├── include/
│   ├── ISource.hpp              # Ingress interface (FileSource / future RtlSdrSource)
│   ├── buffer/
│   │   └── SpscRingBuffer.hpp   # Lock-free SPSC queue
│   ├── dsp/
│   │   ├── DspBlock.hpp         # DSP pipeline interface
│   │   ├── FirFilter.hpp        # Scalar FIR
│   │   └── FirFilterSimd.hpp    # SIMD FIR (AVX2-guarded; currently a passthrough stub)
│   └── core/
│       ├── IDecoder.hpp         # Plugin decoder interface
│       ├── PluginManager.hpp    # Runtime .so/.dylib loader
│       └── Orchestrator.hpp     # Thread lifecycle manager
├── src/
│   ├── CMakeLists.txt           # sdr_main executable target
│   ├── main.cpp                 # Pipeline bootstrap (reads ./mock_data.iq)
│   ├── benchmark.cpp            # Mock data benchmark (no CMake target yet; see docs/08)
│   ├── ingress/
│   │   ├── FileSource.hpp/.cpp  # Binary .iq file ingestion
│   ├── buffer/
│   │   └── SpscRingBuffer.cpp
│   ├── dsp/
│   │   ├── FirFilter.cpp
│   │   └── FirFilterSimd.cpp    # Passthrough stub (AVX2 intrinsics TODO)
│   └── core/
│       ├── Orchestrator.cpp
│       └── PluginManager.cpp
├── plugins/
│   └── adsb/
│       └── adsb_decoder.cpp     # Example ADS-B plugin (built manually; see docs/07)
├── simulator/                   # Streamlit parameter playground
└── tests/
    ├── CMakeLists.txt           # FetchContent(GTest), unit_tests + integration_tests
    ├── unit/
    │   ├── test_ring_buffer.cpp
    │   ├── test_ring_buffer_stress.cpp
    │   ├── test_dsp.cpp
    │   ├── test_dsp_simd.cpp
    │   ├── test_plugin_loader.cpp
    │   ├── test_thread_shutdown.cpp
    │   └── test_file_source.cpp # Present but not wired into CMake yet (see docs/06)
    ├── integration/
    │   ├── test_file_to_decoder.cpp
    │   └── test_pipeline_end_to_end.cpp
    └── mock_data.iq             # Tiny placeholder fixture; tests regenerate their own
```

## Build

Requires:
- CMake >= 3.20
- C++20 compiler (Clang/GCC/MSVC)
- POSIX threads (`Threads` package)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Targets produced:

| Target | Path | Purpose |
|---|---|---|
| `sdr_main` | `build/src/sdr_main` | Demo pipeline over a mock `.iq` file |
| `unit_tests` | `build/tests/unit_tests` | 8 GoogleTest unit tests |
| `integration_tests` | `build/tests/integration_tests` | 2 end-to-end tests |

## Run

**Working directory matters.** `main.cpp` opens `mock_data.iq` and the `PluginManager` scans `plugins/` relative to the *current working directory*, not the executable path.

```bash
# From the repo root: provide a mock file first (copy of the committed one works)
cp tests/mock_data.iq mock_data.iq
./build/src/sdr_main          # runs until you press Enter
rm mock_data.iq
```

If the file is missing, `FileSource` logs an error and the pipeline idles on an empty ring buffer — it does not crash. See `docs/06_testing_and_ci.md#working-directory-rules` for the full resolution rules.

## Tests

Unit and integration tests use GoogleTest (fetched via CMake `FetchContent`). The recommended entry point is CTest, which also registers per-test working directories:

```bash
cd build
ctest --output-on-failure     # 10 tests total (8 unit + 2 integration)

# Or run the binaries directly (from build/, so relative paths resolve):
./tests/unit_tests
./tests/integration_tests
```

What is covered, the mock `.iq` binary format, and known test gaps are documented in [`docs/06_testing_and_ci.md`](docs/06_testing_and_ci.md).

## Key Design Points (see LLD.md and docs/)

- **Zero-copy ring buffer**: `SpscRingBuffer<T>` uses `std::atomic<size_t>` indices — producer publishes with `memory_order_release`, consumer observes with `memory_order_acquire`. Capacity must be a power of two; indexing uses bitmask wrap-around. The `Orchestrator` instantiates it with a fixed capacity of 8192 complex samples.
- **Chunked zero-copy reads**: `push()` rejects writes that would split across the wrap boundary (`len > capacity - start_index`) or exceed total capacity; `pop()` returns the contiguous run up to the end of storage (`min(available, capacity - tail_index)`), so large transfers are naturally chunked on the read side instead.
- **Thread model**: Three threads — ingress (`FileSource::readLoop`), DSP (`Orchestrator::dsp_loop`, yields when idle), output (`output_loop`, currently a stub that sleeps). No priority/SCHED_FIFO configuration is applied today (roadmap item). Shutdown: `stop()` flags off → source stop/join → join DSP → join output.
- **Plugin contract**: Every plugin exports `extern "C" IDecoder* create_decoder()`. `PluginManager` scans a directory for `.so`/`.dylib`, loads with `dlopen(RTLD_NOW)`, owns decoder instances, and tolerates missing/broken plugins by logging `dlerror()` and continuing.
- **Mock injection**: `FileSource` reads interleaved 8-bit unsigned IQ (`uint8_t I`, `uint8_t Q`) normalized to `[-1.0, +1.0)` via `(val - 127.5) / 128.0`. Pre-recorded `.iq` files enable deterministic CI without RF hardware.

## Performance & Constraints

- Hot path contains zero heap allocations per sample (fixed-size stack/vector buffers reused across iterations).
- DSP FIR is scalar by default; the `FirFilterSimd` class is AVX2-guarded in its header but currently implements a passthrough copy (intrinsics TODO — see `docs/08_benchmarks.md` before quoting SIMD speedups).
- Memory footprint target: < 64 MB (ring buffers + taps + plugin code).

## Real Data Ingestion

### Recorded `.iq` Files (works today)
`FileSource` reads binary interleaved 8-bit unsigned IQ (`uint8_t I`, `uint8_t Q`) normalized to `[-1.0, 1.0]`. Any RTL-SDR or HackRF raw capture saved as binary works:

```cpp
auto source = std::make_unique<sdr::FileSource>("real_capture.iq", 8192);
```

### Live RTL-SDR USB (roadmap)
A `RtlSdrSource` is planned (the `ISource` interface in `include/ISource.hpp` is the extension point). To ingest live RF, implement an `ISource` subclass that wraps `librtlsdr`:

```cpp
class RtlSdrSource : public ISource {
public:
    void start() override {
        // rtlsdr_open(ctx, index); rtlsdr_set_sample_rate(ctx, rate);
        // Launch readLoop thread calling rtlsdr_read_sync()
    }
    void setCallback(DataCallback cb) override { m_callback = std::move(cb); }
    void stop() override { rtlsdr_close(ctx); }
private:
    void readLoop() {
        uint8_t* buf = ...; // librtlsdr bulk buffer
        while (running) {
            int n_read = rtlsdr_read_sync(ctx, buf, chunk_size, &n_read);
            if (n_read >= 0 && m_callback) {
                std::vector<std::complex<float>> floatBuf(chunk_size/2);
                for (size_t i = 0; i < chunk_size/2; ++i) {
                    float i_val = (buf[2*i] - 127.5f) / 128.0f;
                    float q_val = (buf[2*i+1] - 127.5f) / 128.0f;
                    floatBuf[i] = std::complex<float>(i_val, q_val);
                }
                m_callback(floatBuf.data(), chunk_size/2);
            }
        }
    }
    rtlsdr_dev_t* ctx = nullptr;
};
```

Link against `librtlsdr` in `CMakeLists.txt` (`find_library` or `pkg_check_modules`). Whatever the transport, keep the `ISource` lifecycle guarantees documented in `docs/05_threading_orchestration.md`.

## References

- Low-Level Design: [`LLD.md`](LLD.md)
- POSIX Threads: *Programming with POSIX Threads* (Butenhof)
- Modern C++: *Effective Modern C++* (Meyers)
- DSP library reference: `liquid-dsp` (open-source)
- Lock-free SPSC: CppCon talks (Fedor Pikus / Herb Sutter)
