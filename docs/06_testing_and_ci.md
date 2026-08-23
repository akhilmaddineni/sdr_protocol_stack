# Testing Guide & CI Reference

How the test suite is organized, what each test actually verifies, the data files they depend on, and the known gaps. Test names below match `ctest -N` output.

## 1. Running Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

cd build
ctest --output-on-failure      # runs all 10 registered tests
ctest -R RingBuffer -V         # filter by name regex
```

The binaries can also be run directly (GoogleTest filters apply):

```bash
./build/tests/unit_tests                       # 8 tests
./build/tests/integration_tests                # 2 tests
./build/tests/unit_tests --gtest_filter=SpscRingBufferStress.*
```

GTest 1.12.1 is fetched at configure time via `FetchContent` (see `tests/CMakeLists.txt`); no system GTest install is required.

## 2. Working-Directory Rules

Several tests use **relative paths**, so behavior depends on where the binary (or CTest) runs:

| Path | Written/read by | Resolves against |
|---|---|---|
| `tests/mock_data.iq` | written by `IntegrationFileSource.MockFileExists`, read by `PipelineEndToEnd`, `OrchestratorShutdown.DoubleStopSafe` | CWD |
| `plugins/` | scanned by `PluginManager` inside `Orchestrator::start()` | CWD |
| `empty_test.iq`, `dummy_test.iq` | created+removed by `FileSourceTest.*` (unwired, see §5) | CWD |

Consequences:

- **Via CTest**: safe. `gtest_discover_tests` sets each test's working directory to `build/tests/`, and `MockFileExists` regenerates `mock_data.iq` there before `PipelineEndToEnd` needs it.
- **Directly from `build/`**: works because `MockFileExists` creates `build/tests/mock_data.iq` on first invocation — but running *only* `PipelineEndToEnd` from a fresh tree means its input file does not exist. The test still passes: `FileSource` logs a file-open error and the pipeline shuts down cleanly.
- The committed `tests/mock_data.iq` is a tiny placeholder (a few bytes) — tests generate their own fixtures at runtime and never depend on its contents. Note that running test binaries with the repo root as CWD can overwrite this file (`MockFileExists` writes 2 bytes there); treat it as disposable.
- `src/benchmark.cpp` generates its own deterministic 4096-sample file (ramp pattern: `i % 256`, `(i + 64) % 256`), runs, then deletes it — identical bytes every run.

## 3. Mock `.iq` Binary Format

All fixtures share one format — raw interleaved unsigned 8-bit IQ (the RTL-SDR convention):

```
offset:  0    1    2    3    4   ...
bytes:  [I0] [Q0] [I1] [Q1] [I2] ...     uint8_t each
```

- One complex sample = 2 bytes; `N` bytes = `N/2` samples (`FileSource` discards a trailing orphan byte via `bytesRead / 2`).
- Normalization: `(uint8 - 127.5f) / 128.0f ∈ [-0.996, +0.996]`; `0x8080` ≈ zero amplitude.

## 4. Test Inventory

### Unit (`build/tests/unit_tests`, 8 compiled tests)

| Test | Verifies | Key source |
|---|---|---|
| `SpscRingBufferTest.PushPopBasic` | push→pop→consume round-trip preserves data | `test_ring_buffer.cpp` |
| `SpscRingBufferTest.EmptyPopReturnsZero` | empty pop returns 0 and nulls buffer ref (contract) | `test_ring_buffer.cpp` |
| `SpscRingBufferStress.ConcurrentPushPop` | producer/consumer threads hammering SPSC without corruption or deadlock | `test_ring_buffer_stress.cpp` |
| `SpscRingBufferStress.Overflow` | push fails cleanly when full; no data torn | `test_ring_buffer_stress.cpp` |
| `FirFilterTest.BasicProcess` | scalar FIR convolution output sanity | `test_dsp.cpp` |
| `FirFilterSimdTest.BasicProcess` | SIMD entry point processes a chunk (passthrough today) | `test_dsp_simd.cpp` |
| `PluginLoaderTest.LoadAndUnload` | missing plugin dir tolerated; `unload_all()` idempotent; empty name list | `test_plugin_loader.cpp` |
| `OrchestratorShutdown.DoubleStopSafe` | start → stop → stop terminates all threads without crash (regression guard for the join bug in `docs/05_threading_orchestration.md` §4) | `test_thread_shutdown.cpp` |

### Integration (`build/tests/integration_tests`, 2 tests)

| Test | Verifies | Key source |
|---|---|---|
| `IntegrationFileSource.MockFileExists` | mock fixture generation works (also seeds data for other tests) | `test_file_to_decoder.cpp` |
| `PipelineEndToEnd.MockFileThroughPipeline` | full `FileSource → ring → FIR` lifecycle starts, processes for a timing window, stops cleanly, reports not-running | `test_pipeline_end_to_end.cpp` |

### Determinism notes

- No RNG anywhere in the C++ pipeline; same `.iq` bytes produce identical sample streams.
- The two timing-window tests sleep fixed durations rather than synchronizing on completion — robust for CI but not cycle-exact; they assert lifecycle correctness, not throughput.

## 5. Known Gaps

1. **`tests/unit/test_file_source.cpp` is not wired into the build.** It exists on disk but is absent from `tests/CMakeLists.txt`, and its include (`#include "FileSource.hpp"`) is stale — the header lives at `src/ingress/FileSource.hpp`, so the include must become `"ingress/FileSource.hpp"` before adding the file to the `unit_tests` target. Its two tests (`ReadEmptyFile`, `ReadDummyData`) would raise coverage from 8 to 10 unit tests once wired.
2. **No decoder-content assertions yet**: end-to-end coverage stops at the FIR stage because decoders are not fed by `dsp_loop` (see `docs/05_threading_orchestration.md` §6). When that lands, add packet-count/content baselines here.
3. **No SIMD-vs-scalar equivalence test**: `FirFilterSimd` currently has no filtering logic to compare; add an epsilon-bounded comparison when real AVX2 intrinsics land.

## 6. CI Recipe

Minimal GitHub Actions workflow (macOS runner shown since AVX2 guards are compile-time only; Linux works identically):

```yaml
name: ci
on: [push, pull_request]
jobs:
  test:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure & build
        run: |
          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build -j3
      - name: Run tests
        working-directory: build
        run: ctest --output-on-failure
```

Notes:
- No hardware, USB access, or Python deps required — everything runs off committed/generated mock files.
- Add `-DCMAKE_CXX_FLAGS=-Werror` if you want warnings to fail CI (the build already enables `-Wall -Wextra -Wpedantic`).
