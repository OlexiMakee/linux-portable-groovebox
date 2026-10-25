#include "midi_engine.hpp"

#include <array>
#include <chrono>
#include <cstdio>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace groovebox {

MidiEngine::MidiEngine() = default;

MidiEngine::~MidiEngine() {
    stop();
}

void MidiEngine::start(
    NoteCallback on_note,
    TransportCallback on_transport,
    ClockStepCallback on_step16,
    CcCallback on_cc
) {
    if (running_.exchange(true)) return;
    on_note_ = std::move(on_note);
    on_transport_ = std::move(on_transport);
    on_step16_ = std::move(on_step16);
    on_cc_ = std::move(on_cc);
    worker_ = std::thread(&MidiEngine::workerLoop, this);
}

void MidiEngine::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) {
        worker_.join();
    }
}

MidiTelemetry MidiEngine::getTelemetry() const {
    MidiTelemetry t;
    t.connected = connected_.load(std::memory_order_relaxed);
    t.active_ports = active_ports_.load(std::memory_order_relaxed);
    t.external_sync = ext_sync_.load(std::memory_order_relaxed);
    if (t.connected) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "USB MIDI (%d PRT 24PPQN)", t.active_ports);
        t.port_name = buf;
    } else {
        t.port_name = "INTERNAL CLK";
    }
    t.detected_bpm = pll_bpm_.load(std::memory_order_relaxed);
    t.rx_clock_ticks = rx_clocks_.load(std::memory_order_relaxed);
    t.rx_notes = rx_notes_.load(std::memory_order_relaxed);
    return t;
}

void MidiEngine::workerLoop() {
#if defined(__linux__)
    struct MidiPortState {
        int fd = -1;
        uint8_t status_byte = 0;
        uint8_t data_buf[2] = {0, 0};
        int data_idx = 0;
    };

    // Підтримка до 8 одночасно підключених Class-Compliant USB MIDI пристроїв (через USB-хаб!)
    constexpr int kMaxPorts = 8;
    std::array<MidiPortState, kMaxPorts> ports{};

    uint32_t clock_subdiv = 0;
    auto last_clock_tp = std::chrono::steady_clock::now();
    auto last_rescan_tp = std::chrono::steady_clock::now() - std::chrono::seconds(5);

    while (running_.load(std::memory_order_relaxed)) {
        auto now_tp = std::chrono::steady_clock::now();

        // Кожні 500 мс скануємо /dev/snd/midiC*D0 на появу нових Class-Compliant USB MIDI девайсів
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - last_rescan_tp).count() >= 500) {
            last_rescan_tp = now_tp;
            int open_count = 0;
            for (int c = 0; c < kMaxPorts; ++c) {
                if (ports[c].fd < 0) {
                    char path[32];
                    std::snprintf(path, sizeof(path), "/dev/snd/midiC%dD0", c);
                    int fd = ::open(path, O_RDONLY | O_NONBLOCK);
                    if (fd >= 0) {
                        ports[c].fd = fd;
                        ports[c].status_byte = 0;
                        ports[c].data_idx = 0;
                    }
                }
                if (ports[c].fd >= 0) {
                    ++open_count;
                }
            }
            active_ports_.store(open_count, std::memory_order_relaxed);
            connected_.store(open_count > 0, std::memory_order_relaxed);
            if (open_count == 0) {
                ext_sync_.store(false, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }

        bool any_bytes_read = false;

        for (int c = 0; c < kMaxPorts; ++c) {
            auto& p = ports[c];
            if (p.fd < 0) continue;

            uint8_t byte = 0;
            ssize_t n = ::read(p.fd, &byte, 1);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                // Пристрій відключено
                ::close(p.fd);
                p.fd = -1;
                continue;
            }
            if (n == 0) continue;

            any_bytes_read = true;

            // 1. System Real-Time Messages (0xF8..0xFF) — обробляються миттєво без скидання Running Status!
            if (byte >= 0xF8) {
                if (byte == 0xF8) {
                    // MIDI Clock Tick (24 PPQN) -> PLL фільтр оцінки BPM
                    auto now = std::chrono::steady_clock::now();
                    float dt_sec = std::chrono::duration<float>(now - last_clock_tp).count();
                    last_clock_tp = now;
                    if (dt_sec > 0.005f && dt_sec < 0.25f) {
                        float inst_bpm = 60.0f / (dt_sec * 24.0f);
                        float cur_bpm = pll_bpm_.load(std::memory_order_relaxed);
                        // One-pole low-pass PLL filter для усунення USB-джитеру
                        pll_bpm_.store(cur_bpm * 0.90f + inst_bpm * 0.10f, std::memory_order_relaxed);
                        ext_sync_.store(true, std::memory_order_relaxed);
                    }
                    rx_clocks_.fetch_add(1, std::memory_order_relaxed);
                    // Кожні 6 тіків (24 PPQN / 4 = 6 тіків на 1/16 ноту) просуваємо секвенсор
                    if (++clock_subdiv >= 6) {
                        clock_subdiv = 0;
                        if (on_step16_) on_step16_();
                    }
                } else if (byte == 0xFA) {
                    // MIDI Start -> Скидаємо секвенсор на крок 0 і запускаємо Play!
                    clock_subdiv = 0;
                    ext_sync_.store(true, std::memory_order_relaxed);
                    if (on_transport_) on_transport_(true, true);
                } else if (byte == 0xFB) {
                    // MIDI Continue -> Продовжуємо Play з поточного кроку
                    ext_sync_.store(true, std::memory_order_relaxed);
                    if (on_transport_) on_transport_(true, false);
                } else if (byte == 0xFC) {
                    // MIDI Stop -> Зупиняємо секвенсор
                    if (on_transport_) on_transport_(false, false);
                }
                continue;
            }

            // 2. System Common (0xF0..0xF7) — ігноруємо дані SysEx до наступного статусу
            if (byte >= 0xF0) {
                p.status_byte = 0;
                p.data_idx = 0;
                continue;
            }

            // 3. Channel Voice Messages (0x80..0xEF)
            if (byte & 0x80) {
                p.status_byte = byte;
                p.data_idx = 0;
                continue;
            }

            if (p.status_byte == 0) continue;
            p.data_buf[p.data_idx++] = byte;

            uint8_t msg_type = p.status_byte & 0xF0;
            uint8_t channel = p.status_byte & 0x0F; // 0..15

            if (msg_type == 0xC0 || msg_type == 0xD0) {
                p.data_idx = 0; // 1-byte messages
            } else if (p.data_idx >= 2) {
                p.data_idx = 0;
                if (msg_type == 0x90 && p.data_buf[1] > 0) {
                    // Note On на каналах 1..4 -> тригерить відповідну доріжку 0..3
                    if (channel < 4 && on_note_) {
                        rx_notes_.fetch_add(1, std::memory_order_relaxed);
                        on_note_(channel, p.data_buf[0], static_cast<float>(p.data_buf[1]) / 127.0f);
                    }
                } else if (msg_type == 0xB0 && on_cc_) {
                    // Control Change (CC) на каналах 1..4
                    if (channel < 4) {
                        on_cc_(channel, p.data_buf[0], static_cast<float>(p.data_buf[1]) / 127.0f);
                    }
                }
            }
        }

        if (!any_bytes_read) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    for (auto& p : ports) {
        if (p.fd >= 0) {
            ::close(p.fd);
            p.fd = -1;
        }
    }
#else
    while (running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif
}

} // namespace groovebox
