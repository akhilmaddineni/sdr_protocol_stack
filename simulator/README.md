# SDR Simulator (Streamlit)

Interactive web UI to experiment with the pipeline parameters without compiling C++ — a Python model of the C++ pipeline logic, useful for building intuition about ring-buffer fill levels and FIR tap choices.

## Run

```bash
python3 -m venv .venv && source .venv/bin/activate   # optional but recommended
pip install -r requirements.txt
streamlit run app.py
```

Then open the printed localhost URL.

## Parameters

| Control | Range (default) | Models |
|---|---|---|
| Ring buffer capacity | 64–4096 (1024) | `SpscRingBuffer` capacity — must be power-of-two in C++ |
| Chunk size | 64–2048 (512) | `FileSource` chunk / `push()` length |
| Push cycles | 1–50 (10) | number of ingress callback invocations |
| FIR taps | comma-separated floats (`0.2, 0.4, 0.2`) | `FirFilter` taps vector |
| Generate Mock IQ | button | uint8-style IQ byte stream preview |
| Full pipeline simulation | button | end-to-end: mock IQ → normalize → ring → FIR |

## What Each Panel Shows

- **Ring Buffer panel**: head/tail counters and used-percentage after N pushes with a consumer draining at half rate — mirrors monotonic-index + bitmask wrap behavior.
- **Mock IQ panel**: raw byte-level view of what an interleaved `.iq` capture looks like before normalization.
- **FIR panel**: applies your taps to a test signal and plots output — same convolution semantics as the scalar `FirFilter::process`.

## Fidelity Notes (simulator vs C++)

- The simulator drains while pushing; the real `push()` **rejects** chunks that would split across the wrap boundary or overflow (`docs/02_ring_buffer.md` §5–6) — expect the C++ pipeline to drop more under the same settings.
- No threading or memory-ordering effects are modeled; latency/throughput numbers here are illustrative only.
- Normalization math matches `FileSource`: `(uint8 − 127.5) / 128`.
