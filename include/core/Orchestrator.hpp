#pragma once
#include "ISource.hpp"
#include "buffer/SpscRingBuffer.hpp"
#include "dsp/DspBlock.hpp"
#include "core/PluginManager.hpp"
#include <thread>
#include <atomic>
#include <memory>

namespace sdr {

class Orchestrator {
public:
    Orchestrator();
    ~Orchestrator();

    void set_source(std::unique_ptr<ISource> source);
    void set_dsp(std::unique_ptr<DspBlock> dsp);

    bool start();
    void stop();
    bool is_running() const;

private:
    std::unique_ptr<ISource> m_source;
    std::unique_ptr<DspBlock> m_dsp;
    std::unique_ptr<SpscRingBuffer<std::complex<float>>> m_ring;
    PluginManager m_plugins;

    std::atomic<bool> m_running{false};
    std::thread m_dsp_thread;
    std::thread m_output_thread;

    void dsp_loop();
    void output_loop();
};

} // namespace sdr
