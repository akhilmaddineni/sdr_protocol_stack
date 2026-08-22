#pragma once

#include "ISource.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <fstream>
#include <vector>

namespace sdr {

class FileSource : public ISource {
public:
    explicit FileSource(const std::string& filepath, size_t chunkSize = 8192);
    ~FileSource() override;

    void start() override;
    void stop() override;
    void setCallback(DataCallback cb) override;

private:
    void readLoop();

    std::string m_filepath;
    size_t m_chunkSize;
    DataCallback m_callback;
    
    std::atomic<bool> m_running{false};
    std::thread m_workerThread;
};

} // namespace sdr
