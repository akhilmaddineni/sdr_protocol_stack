#include "core/Orchestrator.hpp"
#include <iostream>
#include <thread>
#include <algorithm>

namespace sdr {

Orchestrator::Orchestrator() : m_ring(std::make_unique<SpscRingBuffer<std::complex<float>>>(8192)) {}

Orchestrator::~Orchestrator() {
    stop();
}

void Orchestrator::set_source(std::unique_ptr<ISource> source) {
    m_source = std::move(source);
}

void Orchestrator::set_dsp(std::unique_ptr<DspBlock> dsp) {
    m_dsp = std::move(dsp);
}

bool Orchestrator::start() {
    if (m_running.exchange(true)) return false;
    if (!m_source) {
        std::cerr << "Orchestrator: no source set" << std::endl;
        m_running = false;
        return false;
    }

    m_plugins.load_plugins("plugins/");
    m_source->setCallback([this](const std::complex<float>* data, size_t len) {
        if (m_ring) m_ring->push(data, len);
    });

    m_source->start();
    m_dsp_thread = std::thread(&Orchestrator::dsp_loop, this);
    m_output_thread = std::thread(&Orchestrator::output_loop, this);
    return true;
}

void Orchestrator::stop() {
    if (!m_running.exchange(false)) return;
    if (m_source) m_source->stop();
    if (m_dsp_thread.joinable()) m_dsp_thread.join();
    if (m_output_thread.joinable()) m_output_thread.join();
}

bool Orchestrator::is_running() const {
    return m_running.load();
}

void Orchestrator::dsp_loop() {
    std::vector<std::complex<float>> out_buf(1024);
    while (m_running.load(std::memory_order_acquire)) {
        std::complex<float>* ptr = nullptr;
        size_t available = m_ring ? m_ring->pop(ptr) : 0;
        if (available > 0 && ptr && m_dsp) {
            size_t to_process = std::min(available, out_buf.size());
            m_dsp->process(ptr, out_buf.data(), to_process);
            if (m_ring) m_ring->consume(to_process);
        } else {
            std::this_thread::yield();
        }
    }
}

void Orchestrator::output_loop() {
    // Stub: in production, reads from decoder plugins and pushes JSON/stdout.
    while (m_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace sdr
