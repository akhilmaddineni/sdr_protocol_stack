#include "FileSource.hpp"
#include <iostream>
#include <stdexcept>

namespace sdr {

FileSource::FileSource(const std::string& filepath, size_t chunkSize)
    : m_filepath(filepath), m_chunkSize(chunkSize) {
}

FileSource::~FileSource() {
    stop();
}

void FileSource::setCallback(DataCallback cb) {
    m_callback = std::move(cb);
}

void FileSource::start() {
    if (m_running.exchange(true)) return;
    m_workerThread = std::thread(&FileSource::readLoop, this);
}

void FileSource::stop() {
    if (m_running.exchange(false)) {
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }
}

void FileSource::readLoop() {
    std::ifstream file(m_filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "FileSource Error: Failed to open file " << m_filepath << std::endl;
        m_running = false;
        return;
    }

    // Assume 8-bit unsigned interleaved IQ data (standard for RTL-SDR)
    std::vector<uint8_t> buffer(m_chunkSize * 2); 
    std::vector<std::complex<float>> floatBuffer(m_chunkSize);

    while (m_running) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        std::streamsize bytesRead = file.gcount();
        
        if (bytesRead <= 0) {
            // Reached EOF or error
            break;
        }

        size_t samplesRead = bytesRead / 2;

        for (size_t i = 0; i < samplesRead; ++i) {
            // Normalize to [-1.0, 1.0]
            float i_val = (static_cast<float>(buffer[2 * i]) - 127.5f) / 128.0f;
            float q_val = (static_cast<float>(buffer[2 * i + 1]) - 127.5f) / 128.0f;
            floatBuffer[i] = std::complex<float>(i_val, q_val);
        }

        if (m_callback) {
            m_callback(floatBuffer.data(), samplesRead);
        }
        
        // Prevent pure spinning if consumed too quickly; yield to other threads
        std::this_thread::yield(); 
    }

    m_running = false;
}

} // namespace sdr
