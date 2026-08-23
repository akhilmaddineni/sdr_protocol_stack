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

### Threading Spec (as implemented — see also `docs/05_threading_orchestration.md`)
| Thread | Priority | Role | Shutdown Behavior |
|---|---|---|---|
| Ingress (`FileSource::readLoop`) | Normal (no scheduling policy applied) | Reads `.iq` file in chunks, normalizes, pushes into ring buffer via callback. | Exits at EOF or when `m_running` clears; `stop()` always joins the worker if joinable. |
| DSP (`Orchestrator::dsp_loop`) | Normal | Pops contiguous samples from ring buffer, applies `DspBlock`, consumes. | Exits when `m_running` clears; processes whatever chunk is in flight, does not drain buffered samples after the flag drops. |
| Output (`Orchestrator::output_loop`) | Normal (**stub**: sleeps 10 ms per iteration) | Intended to read decoded packets and emit JSON/stdout/UDP. Not wired to decoders yet. | Exits when `m_running` clears. |
| Main / Orchestrator | Normal | `start()` wires callback, loads plugins, spawns threads; `stop()` owns shutdown. | Sequence: clear flag → `source->stop()` (join ingress) → join DSP → join output. |

**Synchronization Rules:**
- Only `std::atomic` with `memory_order_release` (producer/consumer publishes) and `memory_order_acquire` (cross-thread observations) on ring buffer head/tail; relaxed loads are used for a thread's *own* index.
- No mutexes inside the hot data path.
- `std::thread::join()` is mandatory before destructor returns. Invariant enforced after the `FileSource::stop()` fix (see `docs/05_threading_orchestration.md` §4): `stop()` joins whenever the worker thread is joinable, regardless of who cleared `m_running`.

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
  - Chunk size configurable (default 8192 complex samples); yields after each chunk to prevent starvation.
  - Exits the read loop at EOF and clears its own `m_running`; `stop()` must therefore join on the `joinable()` guard alone (see `docs/05_threading_orchestration.md` §4).
- `RtlSdrSource`: Not yet implemented (roadmap). Must wrap `librtlsdr` behind the same `ISource` contract.

### B. Memory Management (`include/buffer/SpscRingBuffer.hpp`, `src/buffer/`)
**`SpscRingBuffer<T>` Requirements (as implemented):**
- Lock-free SPSC: single writer (ingress), single reader (DSP).
- `std::atomic<size_t>` for `head` (write index) and `tail` (read index), monotonically increasing; array position derived via bitmask.
- Memory-ordering: `head.store(new_head, std::memory_order_release)` after data write; `tail.load(std::memory_order_acquire)` before space check. A thread's own index is loaded `relaxed`.
- Capacity must be power-of-two, enforced in the constructor (`throw std::invalid_argument` otherwise); index arithmetic uses bitmask (`index & (cap - 1)`).
- Storage: contiguous `std::vector<T>`, explicitly instantiated for `std::complex<float>` only. Head/tail atomics are padded to separate 64-byte cache lines with `alignas(64)` to prevent false sharing.
- `bool push(const T* src, size_t len)`: Returns `false` if `len == 0`, `len > capacity`, buffer lacks free space, **or the write would split across the wrap boundary** (`start_index + len > cap`). Caller must retry or wait. This keeps the write path branch-simple at the cost of rejecting end-of-buffer transfers.
- `size_t pop(T*& buffer_ref)`: Returns the number of *contiguous* samples readable: `min(head - tail, capacity - (tail & mask))`; sets `buffer_ref` to internal pointer (zero-copy). Returns 0 and nulls the pointer when empty. Caller must call `consume(len)` after processing.
- `void consume(size_t len)`: Advances `tail` by `len`; must not exceed available samples from last `pop()`.
- `size_t available() const`: Snapshot of in-flight samples (acquire loads on both indices; approximate if called concurrently).

**Cache Alignment Spec:**
- `head` and `tail` reside on separate 64-byte cache lines (`alignas(CACHE_LINE_SIZE)` padding).
- Buffer storage uses default allocator alignment today; explicit 32-byte alignment of taps/buffers for AVX remains future work (see §3.C).

### C. DSP Pipeline (`include/dsp/DspBlock.hpp`, `src/dsp/`)
**`DspBlock` Interface:**
- `virtual void process(const std::complex<float>* in, std::complex<float>* out, size_t len) = 0;`
- Output length must equal input length (`len`) for this version (no decimation yet).
- Must be safe to call from DSP thread only; no internal thread creation.

**`FirFilter` Implementation (as implemented):**
- Stores taps in a plain `std::vector<float>` (no explicit 32-byte alignment yet; required before enabling aligned AVX loads).
- Scalar version (`FirFilter::process`) is the working filter: naive per-chunk convolution, stateless between calls (see `docs/03_dsp_fir.md` §1 for the chunk-transient caveat).
- SIMD version (`FirFilterSimd`) is header-guarded by `#ifdef __AVX2__`, but `process()` currently performs a **passthrough copy** (no intrinsics yet). Do not benchmark it as a filter.
- Tap array length need not be a multiple of the SIMD lane count in the scalar path; add that constraint when the AVX2 path lands.

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

### E. Thread Orchestrator (`include/core/Orchestrator.hpp`, `src/core/Orchestrator.cpp`)
- Owns an `ISource`, a fixed-capacity (8192 complex samples) `SpscRingBuffer`, the `DspBlock`, and `PluginManager`; spawns DSP and output threads.
- `start()`: Guards with `m_running.exchange(true)`; fails fast if no source is set. Loads plugins from `"plugins/"` (relative to CWD), wires the source callback to `ring->push()`, then starts ingress, DSP, and output threads.
- `stop()`: Idempotent via `m_running.exchange(false)`. Sequence: stop/join ingress → join DSP → join output. Destructor calls `stop()`.
- Error handling: Missing source → logged, start aborted. File open failure inside `FileSource` → logged, thread exits cleanly (pipeline idles). Plugin load failures → logged by `PluginManager`, pipeline continues.
- Known limitation: the output thread is a stub; decoders are loaded but not yet fed from `dsp_loop` (roadmap: decoder queue between DSP and output).

## 4. Error Handling & Recovery (as implemented)
- `FileSource`: If the file cannot open, `readLoop` logs to `std::cerr`, clears `m_running`, and returns without invoking the callback; the pipeline idles on an empty ring buffer.
- `SpscRingBuffer`: `pop()` returns 0 when empty; `dsp_loop` calls `std::this_thread::yield()` rather than spinning hot. `push()` overflow returns `false`; the ingress callback drops the chunk silently (backpressure policy = drop).
- Plugin load failure: `PluginManager` logs `dlerror()` and skips the file; the pipeline continues with existing decoders. A missing plugin directory logs and returns `false` without throwing.
- Ring buffer capacity violation (`non-power-of-two`) throws `std::invalid_argument` from the constructor; no other allocation paths are expected to throw in steady state.

## 5. Testing & CI Plan (current state — full guide in `docs/06_testing_and_ci.md`)
- **Unit Tests (`tests/unit/`, built into `unit_tests`):**
  - `test_ring_buffer.cpp`: Push/pop/consume cycle, empty-pop-returns-zero contract.
  - `test_ring_buffer_stress.cpp`: Concurrent producer/consumer stress; overflow rejection.
  - `test_dsp.cpp` / `test_dsp_simd.cpp`: FIR scalar output; SIMD stub basic process.
  - `test_plugin_loader.cpp`: `PluginManager` safety when the plugin directory is missing; unload idempotence.
  - `test_thread_shutdown.cpp`: Orchestrator start/stop/double-stop safety.
  - `test_file_source.cpp`: **Known gap — present on disk but not added to `tests/CMakeLists.txt`; its include path (`"FileSource.hpp"`) is stale and would need `"ingress/FileSource.hpp"` before wiring in.**
- **Integration Tests (`tests/integration/`, built into `integration_tests`):**
  - `test_file_to_decoder.cpp`: Regenerates `tests/mock_data.iq` relative to CWD (data dependency).
  - `test_pipeline_end_to_end.cpp`: Full `FileSource → SpscRingBuffer → FIR → stop()` lifecycle with timing window; asserts clean shutdown.
- **Benchmark / Profiling:**
  - `src/benchmark.cpp` measures wall-clock pipeline time over a generated mock file. Not yet a CMake target — manual build instructions and methodology live in `docs/08_benchmarks.md`.

## 6. Build & Directory Mapping (as implemented)

```
sdr_protocol_stack/
├── CMakeLists.txt              # Root: C++20, Threads, Testing, compile_commands
├── include/
│   ├── ISource.hpp             # DataCallback + ISource interface
│   ├── buffer/
│   │   └── SpscRingBuffer.hpp
│   ├── dsp/
│   │   ├── DspBlock.hpp
│   │   ├── FirFilter.hpp
│   │   └── FirFilterSimd.hpp   # AVX2-guarded stub
│   └── core/
│       ├── IDecoder.hpp
│       ├── PluginManager.hpp
│       └── Orchestrator.hpp
├── src/
│   ├── CMakeLists.txt          # sdr_main target (links Threads + dl)
│   ├── main.cpp                # Bootstraps Orchestrator with FileSource + FirFilter
│   ├── benchmark.cpp           # Standalone (no CMake target yet)
│   ├── ingress/
│   │   ├── FileSource.hpp/.cpp
│   ├── buffer/
│   │   └── SpscRingBuffer.cpp  # Explicit instantiation for std::complex<float>
│   ├── dsp/
│   │   ├── FirFilter.cpp
│   │   └── FirFilterSimd.cpp   # Passthrough stub
│   └── core/
│       ├── PluginManager.cpp   # opendir scan + dlopen/dlsym
│       └── Orchestrator.cpp    # dsp_loop / output_loop threads
├── plugins/
│   └── adsb/
│       └── adsb_decoder.cpp    # Example plugin; no CMakeLists — build manually (docs/07)
├── simulator/                  # Streamlit UI (app.py, requirements.txt)
├── docs/                       # First-principles guides 01–08 + index.md
└── tests/
    ├── CMakeLists.txt          # FetchContent(GTest); unit_tests + integration_tests
    ├── unit/                   # 6 compiled files (+ test_file_source.cpp, unwired)
    ├── integration/            # 2 files
    └── mock_data.iq            # Placeholder fixture (tests regenerate fixtures at runtime)
```

Deliberate deviations from earlier drafts of this document: there is **no** `RtlSdrSource.cpp` (roadmap), **no** `DspPipeline.cpp` (the DSP loop lives in `Orchestrator`), and the ADS-B decoder lives in `plugins/adsb/`, not `src/core/DecoderPlugins/`.

**CMake Requirements (current):**
- Root `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.20)`, `CXX_STANDARD 20`, `-Wall -Wextra -Wpedantic`, `find_package(Threads REQUIRED)`, `enable_testing()`.
- `src/CMakeLists.txt`: Executable `sdr_main` links `Threads::Threads` and `dl`; includes `../include`.
- `tests/CMakeLists.txt`: Fetches GTest 1.12.1 via `FetchContent`; builds `unit_tests` and `integration_tests`; registers with `gtest_discover_tests`.

## 7. Interfaces & Contracts (Formal)

**`ISource` Lifecycle:**
```
setCallback(cb) -> start() -> [running] -> stop() -> [stopped] -> ~ISource()
```
- `start()` called twice without `stop()` is a no-op (`m_running` guard).
- `stop()` called twice is a no-op (`joinable()` guard).

**`SpscRingBuffer` Contract:**
- `push()` never blocks; returns `false` on overflow, oversize (`len > capacity`), or wrap-splitting writes. Caller decides backpressure (currently: drop).
- `pop()` never blocks; returns 0 if empty; otherwise returns the contiguous run bounded by the end of storage.
- `consume()` must be called exactly once per `pop()` with `len <=` returned value.
- Destruction: Must only occur after all producers and consumers have finished (i.e., the ring buffer is idle).

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
