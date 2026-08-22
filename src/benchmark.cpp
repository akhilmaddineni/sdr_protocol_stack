#include <iostream>
#include <fstream>
#include <cstdio>
#include "core/Orchestrator.hpp"
#include "ingress/FileSource.hpp"
#include "dsp/FirFilter.hpp"
#include <chrono>

int main() {
    std::cout << "Mock IQ Pipeline Benchmark" << std::endl;

    std::ofstream mock("tests/mock_data.iq", std::ios::binary);
    for (int i = 0; i < 4096; ++i) {
        uint8_t iq[2] = {static_cast<uint8_t>(i % 256), static_cast<uint8_t>((i + 64) % 256)};
        mock.write(reinterpret_cast<char*>(iq), 2);
    }
    mock.close();

    sdr::Orchestrator orchestrator;
    auto source = std::make_unique<sdr::FileSource>("tests/mock_data.iq", 2048);
    auto dsp = std::make_unique<sdr::FirFilter>(std::vector<float>{0.2f, 0.4f, 0.2f});

    orchestrator.set_source(std::move(source));
    orchestrator.set_dsp(std::move(dsp));

    auto start = std::chrono::high_resolution_clock::now();
    if (orchestrator.start()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        orchestrator.stop();
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Pipeline benchmark completed in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms" << std::endl;

    std::remove("tests/mock_data.iq");
    return 0;
}
