# Low-Level Design (LLD): SDR Protocol Stack

## 1. Overview & Goals
The **High-Performance SDR Protocol Stack** is a user-space C++20 application that ingests raw IQ samples from an SDR (or mock `.iq` files) and decodes them in real-time through a decoupled DSP/plugin pipeline.

**Goals:**
- Zero-copy, lock-free SPSC concurrency between ingress, DSP, and output threads.
- Hardware-aware SIMD alignment (AVX 32-byte / NEON 16-byte) for DSP blocks.
- Runtime plugin decoder loading (`.so`/`.dylib`) without restarting the pipeline.
- Deterministic mock injection for CI and benchmark reproducibility.
- Safe graceful shutdown with no dangling threads or memory leaks.

**Non-Goals:**
- Actual RF driver development (`librtlsdr` wrapper is stub-level only).
- Real-time kernel scheduling guarantees (uses `SCHED_FIFO` optionally, not required).

## 2. Data Flow & Threading Model

```
File / RTL-SDR
      |
[Ingress Thread] --(SPSC RingBuffer)--> [DSP Thread] --(normalized float IQ)--> [Decoder Plugins] --> [Output/Telemetry Thread]
```

### Threading Spec
| Thread | Priority | Role | Shutdown Behavior |
|---|---|---|---|
| Ingress (`FileSource`) | Normal | Reads `.iq` or USB bulk into ring buffer. | `stop()` sets `m_running = false`; worker joins; breaks read loop. |
| DSP (`DspPipeline`) | Normal | Pops from ring buffer, applies `DspBlock`, pushes to decoder input. | `stop()` drains remaining samples, consumes, then exits; no drop after signal. |
| Output (`Telemetry`) | Low | Reads decoded packets, outputs JSON/stdout/UDP. | `stop()` flushes buffered packets, then exits. |
| Main / Orchestrator | Normal | Starts/stops threads in order; owns lifecycle. | Joins threads in reverse order (output, DSP, ingress). |

**Synchronization Rules:**
- Only `std::atomic` with `memory_order_release` (producer) and `memory_order_acquire` (consumer) on ring buffer head/tail.
- No mutexes inside the hot data path.
- `std::thread::join()` is mandatory before destructor returns.

## 3. Component Specifications

### A. Ingress Layer (`include/ISource.hpp`, `src/ingress/`)
- `ISource` interface:
  - `start()`: Initializes thread; idempotent (`m_running.exchange(true)`).
  - `stop()`: Idempotent (`m_running.exchange(false)`); joins thread; safe to call multiple times.
  - `setCallback(DataCallback)`: Sets the callback that receives `std::complex<float>*` + length. Must be set before `start()`.
  - `~ISource()`: Must call `stop()`; must not leak `std::thread`.
- `FileSource`:
  - Reads binary `.iq` (interleaved uint8 I/Q, standard RTL-SDR format).
  - Normalizes to `[-1.0, 1.0]` using `(val - 127.5) / 128.0`.
  - Chunk size configurable; yields after each chunk to prevent starvation.
- `RtlSdrSource`: Stub only. Wraps `librtlsdr` in future; must follow same `ISource` contract.

### B. Memory Management (`include/buffer/SpscRingBuffer.hpp`, `src/buffer/`)
**`SpscRingBuffer<T>` Requirements:**
- Lock-free SPSC: single writer (ingress), single reader (DSP).
- `std::atomic<size_t>` for `head` (write index) and `tail` (read index).
- Memory-ordering: `head.store(new_head, std::memory_order_release)`; `tail.load(std::memory_order_acquire)`.
- Capacity must be power-of-two; index arithmetic uses bitmask (`index & (cap - 1)`) for fast wrap-around.
- Page-aligned allocation (`alignas(4096)` or `posix_memalign`) to maximize cache locality.
- `bool push(const T* src, size_t len)`: Returns `false` if not enough contiguous space; does NOT split across wrap boundary (simpler contract). Caller must retry or wait.
- `size_t pop(T*& buffer_ref)`: Returns number of contiguous samples available; sets `buffer_ref` to internal pointer (zero-copy). Caller must call `consume(size_t len)` after processing.
- `void consume(size_t len)`: Advances `tail` by `len`; must not exceed available samples from last `pop()`.

**Cache Alignment Spec:**
- Buffer storage aligned to 32 bytes (AVX) / 64 bytes (AVX2) where applicable.
- `head` and `tail` should reside on separate cache lines (padding) to prevent false sharing.

### C. DSP Pipeline (`include/dsp/DspBlock.hpp`, `src/dsp/`)
**`DspBlock` Interface:**
- `virtual void process(const std::complex<float>* in, std::complex<float>* out, size_t len) = 0;`
- Output length must equal input length (`len`) for this version (no decimation yet).
- Must be safe to call from DSP thread only; no internal thread creation.

**`FirFilter` Implementation:**
- Stores taps in `std::vector<float>` aligned to 32 bytes via `aligned_alloc` or `std::align`.
- Scalar version (`process_scalar`) for portability.
- SIMD version (`process_simd`) using AVX2 (`#include <immintrin.h>`) or NEON (`#include <arm_neon.h>`) guarded by `#ifdef __AVX2__` / `#ifdef __ARM_NEON`.
- Tap array length must be a multiple of 4 (SIMD lane count) for SIMD path; scalar path handles any length.

### D. Plugin Architecture (`include/core/IDecoder.hpp`, `src/core/`)
**`IDecoder` Interface:**
- `virtual std::string get_name() const = 0;`
- `virtual void accept_samples(const std::complex<float>* data, size_t len) = 0;`
- `virtual ~IDecoder() = default;`
- Decoded output is pushed to a secondary lock-free queue or directly to the output thread (implementation choice); LLD requires decoders to NOT block.

**`PluginManager`:**
- Scans `plugins/` directory at startup (or on `reload()`).
- Uses `dlopen()` / `dlsym()` (POSIX) or `LoadLibrary()` (Windows stub) to load `.so`/`.dylib`.
- Each plugin exports `extern "C" IDecoder* create_decoder()` factory function.
- `PluginManager` owns the `dylib` handle and decoder instance; releases on `unload()` or destruction.
- Thread-safety: `load()` / `unload()` called only from main/orchestrator thread, not from DSP thread.

### E. Thread Orchestrator (`src/core/Orchestrator.hpp`)
- Owns `ISource`, `SpscRingBuffer`, `DspPipeline`, `PluginManager`, and output thread.
- `run()`: Starts threads in order (Ingress -> DSP -> Output); blocks until `stop()` called.
- `stop()`: Signals shutdown flags; joins threads in reverse order; drains ring buffer before joining DSP.
- Error handling: If any thread throws or exits unexpectedly, orchestrator logs error and initiates cascading stop.

## 4. Error Handling & Recovery
- `FileSource`: If file cannot open, `start()` logs error, sets `m_running = false`, returns without spawning thread.
- `SpscRingBuffer`: `pop()` returns 0 if empty; DSP thread must spin/yield, not busy-wait at 100% CPU. Use `std::this_thread::yield()` or `std::condition_variable` (optional; LLD allows either, but hot path must not use mutex).
- Plugin load failure: `PluginManager` logs `dlerror()` message; does not abort pipeline; continues with existing decoders.
- Memory alignment failure (`posix_memalign`): Throws `std::bad_alloc`; caught by orchestrator and converted to graceful shutdown.

## 5. Testing & CI Plan
- **Unit Tests (`tests/unit/`):**
  - `test_ring_buffer.cpp`: Push/pop/consume cycles, wrap-around, capacity limits, atomic ordering stress test (multi-threaded producer/consumer).
  - `test_dsp.cpp`: Verify FIR scalar vs SIMD output within epsilon (`1e-4`); test alignment of tap array.
  - `test_plugin_manager.cpp`: Mock `.so` (optional stub); verify factory loading and `get_name()`.
- **Integration Tests (`tests/integration/`):**
  - `test_file_to_decoder.cpp`: Inject known `.iq` file through `FileSource` -> `SpscRingBuffer` -> mock decoder; verify decoded packet count and content match baseline.
  - Deterministic: No random seeds; same input file = same output packets.
- **Benchmark / Profiling:**
  - `tests/perf/` (optional): Measure throughput (samples/sec) and CPU cache misses (`perf stat -e cache-misses`) for scalar vs SIMD FIR.

## 6. Build & Directory Mapping
The repo must match the LLD directory structure exactly:

```
sdr_protocol_stack/
├── CMakeLists.txt              # Root: C++20, Threads, Testing
├── include/
│   ├── ISource.hpp
│   ├── buffer/
│   │   └── SpscRingBuffer.hpp
│   ├── dsp/
│   │   ├── DspBlock.hpp
│   │   └── FirFilter.hpp
│   └── core/
│       ├── IDecoder.hpp
│       ├── PluginManager.hpp
│       └── Orchestrator.hpp
├── src/
│   ├── CMakeLists.txt          # Executable + sources
│   ├── main.cpp                 # Bootstraps Orchestrator
│   ├── ingress/
│   │   ├── FileSource.cpp
│   │   └── RtlSdrSource.cpp     # Stub
│   ├── buffer/
│   │   └── SpscRingBuffer.cpp
│   ├── dsp/
│   │   ├── DspPipeline.cpp
│   │   └── FirFilter.cpp
│   └── core/
│       ├── PluginManager.cpp
│       ├── Orchestrator.cpp
│       └── DecoderPlugins/
│           └── AdsBDecoder.cpp  # Example plugin
├── plugins/
│   └── adsb/
│       ├── CMakeLists.txt
│       └── adsb_decoder.cpp
└── tests/
    ├── CMakeLists.txt          # FetchContent(GTest)
    ├── unit/
    │   ├── test_ring_buffer.cpp
    │   ├── test_dsp.cpp
    │   └── test_plugin_manager.cpp
    └── integration/
        └── test_file_to_decoder.cpp
```

**CMake Requirements:**
- Root `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.20)`, `CXX_STANDARD 20`, `find_package(Threads REQUIRED)`, `enable_testing()`.
- `src/CMakeLists.txt`: Links executable (`sdr_main`) against `Threads::Threads`; includes `../include`.
- `tests/CMakeLists.txt`: Fetches GTest; builds `unit_tests`; links `GTest::gtest_main` and `Threads::Threads`.

## 7. Interfaces & Contracts (Formal)

**`ISource` Lifecycle:**
```
setCallback(cb) -> start() -> [running] -> stop() -> [stopped] -> ~ISource()
```
- `start()` called twice without `stop()` is a no-op (`m_running` guard).
- `stop()` called twice is a no-op (`joinable()` guard).

**`SpscRingBuffer` Contract:**
- `push()` never blocks; returns `false` on overflow. Caller decides backpressure (drop or retry).
- `pop()` never blocks; returns 0 if empty.
- `consume()` must be called exactly once per `pop()` with `len <=` returned value.
- Destruction: Must only occur after all producers and consumers have finished (orring buffer is idle).

**`DspBlock` Contract:**
- `process()` must not throw; any exception should be caught by `DspPipeline` wrapper and logged.
- SIMD path must check `len >= vector_size` (e.g., 4 or 8); if shorter, fall back to scalar.

**`PluginManager` Contract:**
- `load()` scans directory once at construction or on explicit call.
- `get_decoder(name)` returns pointer or `nullptr`; caller does not own pointer (manager owns it).
- `unload_all()` releases `.so` handles and destroys decoder objects.

## 8. Performance & Memory Constraints
- Hot path (push/pop/process) must contain zero heap allocations (`std::function` callbacks allowed only at setup, not per sample).
- `std::complex<float>` is the unified data type across all interfaces; no conversion overhead inside ring buffer.
- Memory footprint target: < 64 MB total for ring buffers + DSP taps + plugin code at startup.

## 9. References
- POSIX Threads (Butenhof) for `SCHED_FIFO`, `pthread_attr_setschedparam`.
- `liquid-dsp` source for FIR design and SIMD filter structures.
- CppCon talks on lock-free SPSC queues (Fedor Pikus / Herb Sutter).
