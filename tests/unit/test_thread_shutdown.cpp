#include <gtest/gtest.h>
#include "core/Orchestrator.hpp"
#include "ingress/FileSource.hpp"
#include "dsp/FirFilter.hpp"

TEST(OrchestratorShutdown, DoubleStopSafe) {
    sdr::Orchestrator orch;
    auto source = std::make_unique<sdr::FileSource>("tests/mock_data.iq", 512);
    auto dsp = std::make_unique<sdr::FirFilter>(std::vector<float>{0.5f});
    orch.set_source(std::move(source));
    orch.set_dsp(std::move(dsp));
    orch.start();
    orch.stop();
    orch.stop(); // double stop must be safe
}
