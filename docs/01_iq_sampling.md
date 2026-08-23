# First Principles: IQ Sampling & Normalization

## Analog-to-Digital Conversion (ADC)

A real-valued RF signal is sampled by an ADC at rate `Fs`. For a carrier frequency `Fc`, the Nyquist criterion requires `Fs > 2*(Fc + BW/2)` to avoid aliasing. In direct-conversion (zero-IF) receivers, the signal is mixed down to DC (baseband) before sampling, producing two quadrature channels: In-phase (`I`) and Quadrature (`Q`).

## Complex Baseband Representation

A complex number `z = I + jQ` represents the amplitude and phase of the baseband signal. The real part (`I`) captures the cosine component; the imaginary part (`Q`) captures the sine component. Together they encode both magnitude (`|z|`) and phase (`arg(z)`), which is impossible with a single real-valued sample.

## Normalization

RTL-SDR and most SDR dongles output unsigned 8-bit integers (`uint8_t`) for `I` and `Q`. Before DSP processing, these must be normalized to floating-point `[-1.0, 1.0]` to prevent integer overflow during multiplication:

```cpp
float normalize(uint8_t sample) {
    return (static_cast<float>(sample) - 127.5f) / 128.0f;
}
```

Why `-127.5` and `/128`? A `uint8_t` ranges from `0` to `255`. The midpoint (`127.5`) represents zero amplitude; the scale (`128`) maps the full range to `[-1.0, 1.0]` with headroom for negative peaks.

## Interleaved Binary Format

Raw `.iq` files store interleaved samples: `[I0][Q0][I1][Q1][...]`. Each pair forms one complex sample (`std::complex<float>`). The `FileSource` component reads chunks of `uint8_t` pairs and converts them to `std::complex<float>` before pushing to the ring buffer.

```mermaid
flowchart LR
    A[RF Antenna] --> B[Mixer / Zero-IF]
    B --> C[ADC (uint8 I + Q)]
    C --> D[Normalize to float [-1,1]]
    D --> E[Interleaved binary .iq]
    E --> F[FileSource readLoop]
    F --> G[std::complex<float> array]
    G --> H[SpscRingBuffer push]
```
