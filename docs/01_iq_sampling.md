# First Principles: IQ Sampling, Complex Baseband, and Normalization

## 1. RF Signal Representation

A real-valued sinusoid at carrier frequency `Fc` can be written:

```
s(t) = A(t) * cos(2*pi*Fc*t + phi(t))
```

Where `A(t)` is the time-varying amplitude (the signal of interest) and `phi(t)` is the phase. In a direct-conversion (zero-IF) receiver, the RF signal is mixed with two local oscillators: one at `Fc` with 0° phase (`I`), and one at `Fc` with 90° phase (`Q`). This produces two baseband signals:

```
I(t) = A(t) * cos(phi(t))
Q(t) = A(t) * sin(phi(t))
```

Together, `I` and `Q` form a complex baseband representation:

```
z(t) = I(t) + j*Q(t) = A(t) * exp(j*phi(t))
```

This complex representation preserves both magnitude (`|z| = A`) and phase (`arg(z) = phi`), which is impossible with a single real-valued sample stream. Without `Q`, positive and negative frequencies become indistinguishable (spectral ambiguity).

```mermaid
flowchart LR
    A[RF Signal A*cos(2πFc·t + φ)] --> B[Mixer 0°]
    A --> C[Mixer 90°]
    B --> D[I(t) = A·cos(φ)]
    C --> E[Q(t) = A·sin(φ)]
    D --> F[Complex Baseband I + jQ]
    E --> F
```

## 2. Analog-to-Digital Conversion (ADC)

Real-world ADCs sample `I` and `Q` independently at rate `Fs`. The Nyquist-Shannon sampling theorem requires:

```
Fs > 2 * (Fc + BW/2)
```

Where `BW` is the signal bandwidth. In practice, `Fs` is often `2.048 MHz`, `3.2 MHz`, or `20 MHz` for RTL-SDR dongles. Each sample is quantized to a discrete level. Most consumer SDR devices use 8-bit unsigned integers (`uint8_t`), giving 256 quantization levels.

The quantization error (noise floor) for an `N`-bit ADC is approximately:

```
SNR ≈ 6.02*N + 1.76 dB  (for a full-scale sinusoid)
```

For `N = 8`: `SNR ≈ 49.9 dB`. This limits the dynamic range before DSP processing.

## 3. Binary Interleaved Format

Raw `.iq` captures store `I` and `Q` samples interleaved as bytes:

```
Byte sequence: [I0][Q0][I1][Q1][I2][Q2]...
```

Each pair is one complex sample (`std::complex<float>`). The `FileSource` reads chunks of `uint8_t` pairs and converts them to float arrays before pushing into the ring buffer.

Reference code (`src/ingress/FileSource.cpp`):

```cpp
std::vector<uint8_t> buffer(m_chunkSize * 2);
std::vector<std::complex<float>> floatBuffer(m_chunkSize);

file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
std::streamsize bytesRead = file.gcount();
size_t samplesRead = bytesRead / 2;

for (size_t i = 0; i < samplesRead; ++i) {
    float i_val = (static_cast<float>(buffer[2*i]) - 127.5f) / 128.0f;
    float q_val = (static_cast<float>(buffer[2*i + 1]) - 127.5f) / 128.0f;
    floatBuffer[i] = std::complex<float>(i_val, q_val);
}
```

## 4. Normalization Mathematics

A `uint8_t` ranges from `0` to `255`. The midpoint (`127.5`) represents zero amplitude. The scale factor (`128`) maps the full range to `[-1.0, 1.0]` with headroom:

```
normalized = (uint8_value - 127.5) / 128.0
```

Proof of range:
- `uint8 = 0` → `(0 - 127.5) / 128 = -0.9961` (close to -1.0)
- `uint8 = 255` → `(255 - 127.5) / 128 = +0.9961` (close to +1.0)
- `uint8 = 127.5` → `0` (exact zero)

This avoids clipping (hard saturation at ±1) and prevents integer overflow during FIR convolution (`sum of products` can easily exceed 255).

## 5. Spectral Ambiguity Without Q

If only `I` is sampled, a signal at `+Fc` and `-Fc` produce identical samples (`cos` is even). The `Q` channel (`sin`) breaks this symmetry (`sin` is odd), allowing the DSP pipeline to distinguish positive and negative frequencies — critical for demodulation and protocol decoding.

```mermaid
flowchart TD
    A[Real RF Signal] --> B[Mixer: 0° → I]
    A --> C[Mixer: 90° → Q]
    B --> D[ADC → uint8 I]
    C --> E[ADC → uint8 Q]
    D --> F[Normalize (uint8 - 127.5)/128]
    E --> F
    F --> G[Complex float array]
    G --> H[Interleaved .iq file]
    H --> I[FileSource readLoop]
    I --> J[SpscRingBuffer]
```
