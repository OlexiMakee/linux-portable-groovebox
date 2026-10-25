#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace groovebox {

class AudioEngine;

class NetAudioStreamer {
public:
    NetAudioStreamer();
    ~NetAudioStreamer();

    void start(uint16_t port = 8000, uint32_t sample_rate = 32000);
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_relaxed); }
    int activeClientsCount() const { return client_count_.load(std::memory_order_relaxed); }

    // Push stereo 16-bit PCM block from audio rendering loop
    void pushAudioBlock(const int16_t* interleaved_pcm, uint32_t frames);

private:
    void serverThreadLoop();
    void handleClient(int client_fd);

    std::atomic<bool> running_{false};
    std::atomic<int> client_count_{0};
    uint16_t port_{8000};
    uint32_t sample_rate_{32000};

    int server_sock_{-1};
    int udp_sock_{-1};
    std::thread server_thread_;

    std::mutex client_mutex_;
    std::vector<int> client_fds_;

    // Ring buffer for broadcast
    static constexpr size_t kRingCapacity = 16384;
    std::vector<int16_t> ring_buf_;
    std::atomic<size_t> write_pos_{0};
};

} // namespace groovebox
