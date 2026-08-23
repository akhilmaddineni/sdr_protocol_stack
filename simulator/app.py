import streamlit as st
import numpy as np
import matplotlib.pyplot as plt
import time

st.set_page_config(page_title="SDR Protocol Simulator", layout="wide")
st.title("SDR Protocol Stack — Interactive Simulator")

st.markdown("""
Play with the pipeline parameters in real time: chunk size, FIR taps, ring buffer capacity,
and mock IQ injection. This is a Python simulation of the C++ pipeline logic.
""")

col1, col2 = st.columns(2)

with col1:
    st.subheader("Ring Buffer")
    cap = st.slider("Capacity (samples)", 64, 4096, 1024)
    chunk = st.slider("Chunk size", 64, 2048, 512)
    pushes = st.slider("Push cycles", 1, 50, 10)

    # Simple SPSC simulation
    head = 0
    tail = 0
    buf = np.zeros(cap, dtype=complex)
    for i in range(pushes):
        start = head % cap
        if start + chunk > cap:
            break  # reject wrap per LLD
        buf[start:start+chunk] = np.random.randn(chunk) + 1j*np.random.randn(chunk)
        head += chunk
        tail = min(tail + chunk // 2, head)

    st.metric("Head", head)
    st.metric("Tail", tail)
    st.metric("Available", head - tail)
    st.metric("Used %", round((head - tail) / cap * 100, 1))

with col2:
    st.subheader("Mock IQ Samples")
    if st.button("Generate Mock IQ"):
        iq = np.random.randint(0, 256, 1024)
        st.line_chart(iq[:256])
        st.write(f"Samples: {len(iq)} bytes ({len(iq)//2} IQ pairs)")

st.subheader("FIR Filter (Interactive Taps)")
taps_raw = st.text_input("Taps (comma-separated floats)", "0.2, 0.4, 0.2")
try:
    taps = [float(x) for x in taps_raw.split(",")]
    st.write("Taps:", taps)
    # Simulate filter on random signal
    signal = np.random.randn(256) + 1j*np.random.randn(256)
    filtered = np.convolve(signal, taps, mode='same').astype(complex)
    st.line_chart(np.abs(filtered)[:128])
except Exception as e:
    st.error(f"Invalid taps: {e}")

st.subheader("Full Pipeline Output")
if st.button("Run Pipeline (Mock)"):
    with st.spinner("Running pipeline..."):
        time.sleep(0.5)
    st.success("Pipeline complete — 8 samples processed, 0 packets decoded (stub decoder)")
