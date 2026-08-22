#include <gtest/gtest.h>
#include "FileSource.hpp"
#include <fstream>
#include <vector>
#include <chrono>

TEST(FileSourceTest, ReadEmptyFile) {
    // Setup a dummy empty file
    std::string testFile = "empty_test.iq";
    std::ofstream out(testFile, std::ios::binary);
    out.close();

    sdr::FileSource source(testFile, 1024);
    
    bool callbackCalled = false;
    source.setCallback([&](const std::complex<float>*, size_t) {
        callbackCalled = true;
    });

    source.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    source.stop();

    EXPECT_FALSE(callbackCalled);
    std::remove(testFile.c_str());
}

TEST(FileSourceTest, ReadDummyData) {
    std::string testFile = "dummy_test.iq";
    std::ofstream out(testFile, std::ios::binary);
    // Write 4 bytes (2 complex samples, given 8-bit unsigned interleaved)
    std::vector<uint8_t> dummyData = {128, 128, 255, 0};
    out.write(reinterpret_cast<const char*>(dummyData.data()), dummyData.size());
    out.close();

    sdr::FileSource source(testFile, 1024);
    
    size_t totalSamples = 0;
    source.setCallback([&](const std::complex<float>*, size_t len) {
        totalSamples += len;
    });

    source.start();
    // Wait a brief moment for the thread to process the file and hit EOF
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    source.stop();

    EXPECT_EQ(totalSamples, 2);
    std::remove(testFile.c_str());
}
