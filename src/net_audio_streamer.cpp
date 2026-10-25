#include "net_audio_streamer.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>

namespace groovebox {

static const char* kHtmlPage =
"<!DOCTYPE html>\n"
"<html><head><meta charset='utf-8'><title>XU10 Groovebox Audio Gadget</title>\n"
"<style>\n"
"body { background: #0c0d10; color: #e6e6e6; font-family: -apple-system, monospace; text-align: center; padding: 40px; margin: 0; }\n"
".card { background: #181920; border: 1px solid #2a2c38; border-radius: 8px; max-width: 520px; margin: 0 auto; padding: 30px; box-shadow: 0 8px 24px rgba(0,0,0,0.5); }\n"
"h1 { color: #ff8800; font-size: 20px; letter-spacing: 2px; margin-bottom: 8px; }\n"
".sub { color: #888; font-size: 12px; margin-bottom: 24px; }\n"
"button { background: #00ddff; color: #000; border: none; padding: 14px 28px; font-size: 14px; font-weight: bold; border-radius: 4px; cursor: pointer; letter-spacing: 1px; transition: 0.2s; }\n"
"button:hover { background: #33e6ff; box-shadow: 0 0 12px rgba(0,221,255,0.4); }\n"
".status { margin-top: 20px; font-size: 13px; color: #00ff66; }\n"
".meter { background: #111; height: 12px; border-radius: 6px; margin: 24px 0; overflow: hidden; border: 1px solid #333; }\n"
".meter-fill { background: linear-gradient(90deg, #00ff66, #ffdd00, #ff3300); height: 100%; width: 0%; transition: width 0.05s; }\n"
"</style></head><body>\n"
"<div class='card'>\n"
"  <h1>XU10 GROOVEBOX</h1>\n"
"  <div class='sub'>USB CLASS-COMPLIANT NETWORK AUDIO STREAMER (32.0 kHz / 16-bit STEREO)</div>\n"
"  <button id='btn' onclick='toggleAudio()'>CONNECT AUDIO GADGET</button>\n"
"  <div class='meter'><div class='meter-fill' id='meter'></div></div>\n"
"  <div class='status' id='status'>Status: Ready. Click button to connect.</div>\n"
"</div>\n"
"<script>\n"
"let audioCtx = null, isPlaying = false, eventSource = null;\n"
"async function toggleAudio() {\n"
"  const btn = document.getElementById('btn');\n"
"  const st = document.getElementById('status');\n"
"  if (!isPlaying) {\n"
"    audioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 32000 });\n"
"    await audioCtx.resume();\n"
"    st.innerText = 'Connecting to stream...';\n"
"    const resp = await fetch('/audio');\n"
"    const reader = resp.body.getReader();\n"
"    isPlaying = true;\n"
"    btn.innerText = 'DISCONNECT AUDIO';\n"
"    btn.style.background = '#ff4444';\n"
"    st.innerText = 'STREAMING LIVE (Direct Bit-Perfect 32kHz)';\n"
"    let nextTime = audioCtx.currentTime + 0.05;\n"
"    while (isPlaying) {\n"
"      const { value, done } = await reader.read();\n"
"      if (done) break;\n"
"      const int16 = new Int16Array(value.buffer, value.byteOffset, value.byteLength / 2);\n"
"      const frames = int16.length / 2;\n"
"      if (frames <= 0) continue;\n"
"      const buf = audioCtx.createBuffer(2, frames, 32000);\n"
"      const left = buf.getChannelData(0);\n"
"      const right = buf.getChannelData(1);\n"
"      let peak = 0;\n"
"      for (let i = 0; i < frames; ++i) {\n"
"        left[i] = int16[i * 2] / 32768.0;\n"
"        right[i] = int16[i * 2 + 1] / 32768.0;\n"
"        const mag = Math.max(Math.abs(left[i]), Math.abs(right[i]));\n"
"        if (mag > peak) peak = mag;\n"
"      }\n"
"      document.getElementById('meter').style.width = Math.min(100, Math.floor(peak * 120)) + '%';\n"
"      const src = audioCtx.createBufferSource();\n"
"      src.buffer = buf;\n"
"      src.connect(audioCtx.destination);\n"
"      if (nextTime < audioCtx.currentTime) nextTime = audioCtx.currentTime + 0.01;\n"
"      src.start(nextTime);\n"
"      nextTime += buf.duration;\n"
"    }\n"
"  } else {\n"
"    isPlaying = false;\n"
"    if (audioCtx) audioCtx.close();\n"
"    btn.innerText = 'CONNECT AUDIO GADGET';\n"
"    btn.style.background = '#00ddff';\n"
"    st.innerText = 'Disconnected.';\n"
"    document.getElementById('meter').style.width = '0%';\n"
"  }\n"
"}\n"
"</script></body></html>\n";

NetAudioStreamer::NetAudioStreamer() {
    ring_buf_.resize(kRingCapacity * 2, 0);
}

NetAudioStreamer::~NetAudioStreamer() {
    stop();
}

void NetAudioStreamer::start(uint16_t port, uint32_t sample_rate) {
    if (running_.load(std::memory_order_relaxed)) return;
    port_ = port;
    sample_rate_ = sample_rate;
    running_.store(true);

    server_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock_ >= 0) {
        int opt = 1;
        setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        if (bind(server_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            listen(server_sock_, 8);
        }
    }

    // UDP raw PCM stream socket
    udp_sock_ = socket(AF_INET, SOCK_DGRAM, 0);

    server_thread_ = std::thread(&NetAudioStreamer::serverThreadLoop, this);
}

void NetAudioStreamer::stop() {
    if (!running_.load(std::memory_order_relaxed)) return;
    running_.store(false);

    if (server_sock_ >= 0) {
        shutdown(server_sock_, SHUT_RDWR);
        close(server_sock_);
        server_sock_ = -1;
    }

    if (udp_sock_ >= 0) {
        close(udp_sock_);
        udp_sock_ = -1;
    }

    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        for (int fd : client_fds_) {
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        client_fds_.clear();
        client_count_.store(0);
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void NetAudioStreamer::pushAudioBlock(const int16_t* interleaved_pcm, uint32_t frames) {
    if (!running_.load(std::memory_order_relaxed)) return;

    size_t byte_count = frames * 2 * sizeof(int16_t);

    // 1. Broadcast to UDP client (10.1.1.1:8001 for DAW / sox / BlackHole)
    if (udp_sock_ >= 0) {
        sockaddr_in host_addr{};
        host_addr.sin_family = AF_INET;
        host_addr.sin_port = htons(8001);
        inet_pton(AF_INET, "10.1.1.1", &host_addr.sin_addr);
        sendto(udp_sock_, interleaved_pcm, byte_count, MSG_DONTWAIT,
               reinterpret_cast<sockaddr*>(&host_addr), sizeof(host_addr));
    }

    // 2. Broadcast to connected HTTP clients
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fds_.empty()) return;

    for (auto it = client_fds_.begin(); it != client_fds_.end();) {
        int fd = *it;
        ssize_t sent = send(fd, interleaved_pcm, byte_count, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent < 0) {
            close(fd);
            it = client_fds_.erase(it);
        } else {
            ++it;
        }
    }
    client_count_.store(static_cast<int>(client_fds_.size()), std::memory_order_relaxed);
}

void NetAudioStreamer::serverThreadLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        if (server_sock_ < 0) break;

        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_sock_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (client_fd < 0) {
            if (running_.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            continue;
        }

        // Set non-blocking or timeout
        timeval tv{.tv_sec = 1, .tv_usec = 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char req_buf[1024];
        ssize_t r = recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);
        if (r > 0) {
            req_buf[r] = '\0';
            if (std::strstr(req_buf, "GET /audio") != nullptr || std::strstr(req_buf, "GET /stream") != nullptr) {
                // Stream audio endpoint
                std::string header =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: audio/x-raw\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Cache-Control: no-cache\r\n"
                    "Connection: close\r\n\r\n";
                send(client_fd, header.c_str(), header.length(), MSG_NOSIGNAL);

                int nodelay = 1;
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

                std::lock_guard<std::mutex> lock(client_mutex_);
                client_fds_.push_back(client_fd);
                client_count_.store(static_cast<int>(client_fds_.size()), std::memory_order_relaxed);
            } else {
                // Serve HTML WebAudio client
                std::string body = kHtmlPage;
                std::string resp =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    "Content-Length: " + std::to_string(body.length()) + "\r\n"
                    "Connection: close\r\n\r\n" + body;
                send(client_fd, resp.c_str(), resp.length(), MSG_NOSIGNAL);
                close(client_fd);
            }
        } else {
            close(client_fd);
        }
    }
}

} // namespace groovebox
