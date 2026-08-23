# Benchmarks: Status, Methodology, and Profiling How-To

Honest state of performance measurement in this repo, how to run what exists, and how to extend it. Read this before quoting any SIMD speedup numbers.

## 1. Current State (read this first)

| Claim in older docs | Reality |
|---|---|
| "SIMD FIR 2–4x throughput" | **Not yet measurable.** `FirFilterSimd::process` is a passthrough copy (`src/dsp/FirFilterSimd.cpp`); the header has AVX2 guards but no intrinsics. |
| "`./src/benchmark` build target" | `src/benchmark.cpp` exists but has **no CMake target**; build it manually (below). |
| "< 64 MB footprint, zero hot-path allocations" | Design targets, not asserted by any test today (§4 shows how to verify). |

## 2. Building & Running the Existing Benchmark

`benchmark.cpp` generates a deterministic 4096-sample mock file (`tests/mock_data.iq`, deleted afterwards), runs the full pipeline for a 50 ms window, and prints wall-clock time:

```bash
# From repo root — manual build (no CMake target yet):
c++ -std=c++20 -O2 -I include \
    src/benchmark.cpp src/ingress/FileSource.cpp src/buffer/SpscRingBuffer.cpp \
    src/dsp/FirFilter.cpp src/core/Orchestrator.cpp src/core/PluginManager.cpp \
    -o /tmp/opencode/sdr_bench -pthread -ldl

/tmp/opencode/sdr_bench
```

Expected output:

```
Mock IQ Pipeline Benchmark
PluginManager: scanning plugins/
Pipeline benchmark completed in <~50-60> ms
```

Interpretation: the reported number is dominated by the fixed 50 ms sleep window plus shutdown join time — it demonstrates lifecycle overhead, not sample throughput.

## 3. What to Measure Once SIMD Lands

The meaningful metric for an SDR pipeline is **sustained samples/second through the DSP stage**, not wall time. Recommended methodology:

1. **Throughput counter**: wrap `FirFilter::process` calls with a samples-processed accumulator; report `(samples / elapsed)` after N seconds of continuous feed.
2. **Median-of-N runs**: run ≥ 30 iterations; report median and p95. Single-run timings on laptops are noise-dominated (DVFS, thermal).
3. **Fixed work per run**: feed from memory (mock file loop), never from disk I/O timing.
4. **Scalar vs SIMD equivalence gate**: before benchmarking SIMD, add the epsilon test (`docs/06_testing_and_ci.md` §5) proving outputs match scalar within `1e-4`.
5. **CPU counters** (Linux): `perf stat -e cycles,instructions,cache-misses,branch-misses ./bench`; on macOS use Instruments' Time Profiler / Counters template.

Suggested harness additions: `tests/perf/bench_fir.cpp` looping both filter classes over pre-aligned buffers, printing MSamples/s.

## 4. Verifying the Memory Targets

- **Zero hot-path allocations**: run under a heap profiler and assert no allocations between pipeline start and stop:
  - Linux: `heaptrack ./sdr_main` or valgrind massif
  - macOS: leaks/malloc stack logging — `MallocStackLogging=1 leaks ./sdr_main` at exit
- **< 64 MB footprint**: ring buffer is fixed at 8192 × 16 B = 128 KB; everything else is taps + plugin code + runtime. A simple `/usr/bin/time -l ./sdr_main` (macOS) or `/usr/bin/time -v` (Linux) reports peak RSS.

## 5. Known Performance Characteristics

- **Idle strategy**: DSP/output threads `yield()`-spin when starved → low latency, nonzero CPU at idle. Measure *useful* throughput under saturation only.
- **Chunking asymmetry**: `push()` rejects wrap-splitting writes while `pop()` returns end-of-storage-bounded contiguous runs (`docs/02_ring_buffer.md` §5) — worst-case push pattern matters when sizing chunk vs capacity.
- **Normalization cost**: uint8→float conversion happens once at ingress (`FileSource`); the ring buffer carries `std::complex<float>` end-to-end with no further conversion (LLD §8 contract).
