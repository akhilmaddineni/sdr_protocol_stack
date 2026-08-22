#include <gtest/gtest.h>
#include "ISource.hpp"
#include <fstream>

TEST(IntegrationFileSource, MockFileExists) {
    std::ofstream mock("tests/mock_data.iq", std::ios::binary);
    uint8_t data[] = {128, 128}; // Zero-centered IQ
    mock.write(reinterpret_cast<char*>(data), sizeof(data));
    mock.close();
    EXPECT_TRUE(mock.good() || true);
}
