#include <iostream>
#include "core/Orchestrator.hpp"
#include "ingress/FileSource.hpp"
#include "dsp/FirFilter.hpp"

int main() {
    std::cout << "SDR Protocol Stack Initialization" << std::endl;

    sdr::Orchestrator orchestrator;
    auto source = std::make_unique<sdr::FileSource>("mock_data.iq", 1024);
    auto dsp = std::make_unique<sdr::FirFilter>(std::vector<float>{0.2f, 0.4f, 0.2f});

    orchestrator.set_source(std::move(source));
    orchestrator.set_dsp(std::move(dsp));

    if (orchestrator.start()) {
        std::cout << "Pipeline running. Press Enter to stop." << std::endl;
        std::cin.get();
        orchestrator.stop();
    }

    return 0;
}
