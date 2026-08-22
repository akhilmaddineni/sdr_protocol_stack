#include <gtest/gtest.h>
#include "core/Orchestrator.hpp"
#include "ingress/FileSource.hpp"
#include "dsp/FirFilter.hpp"
#include <thread>
#include <chrono>

TEST(PipelineEndToEnd, MockFileThroughPipeline) {
    sdr::Orchestrator orch;
    auto source = std::make_unique<sdr::FileSource>("tests/mock_data.iq", 1024);
    auto dsp = std::make_unique<sdr::FirFilter>(std::vector<float>{0.2f, 0.4f, 0.2f});
    orch.set_source(std::move(source));
    orch.set_dsp(std::move(dsp));
    EXPECT_TRUE(orch.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    orch.stop();
    EXPECT_FALSE(orch.is_running());
}
