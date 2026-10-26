#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace groovebox {

struct MidiTelemetry {
    bool connected = false;
    int active_ports = 0;
    std::string port_name = "INTERNAL CLOCK";
    bool external_sync = false;
    float detected_bpm = 120.0f;
    uint32_t rx_clock_ticks = 0;
    uint32_t rx_notes = 0;
};

class MidiEngine {
public:
    using NoteCallback = std::function<void(int channel, int note, float velocity)>;
    using NoteOffCallback = std::function<void(int channel, int note)>;
    using ProgramChangeCallback = std::function<void(int channel, int program)>;
    using TransportCallback = std::function<void(bool playing, bool reset_to_start)>;
    using ClockStepCallback = std::function<void()>; // Викликається кожні 6 тіків 0xF8 (1/16 нота)
    using CcCallback = std::function<void(int channel, uint8_t cc, float norm_val)>;

    MidiEngine();
    ~MidiEngine();

    void start(
        NoteCallback on_note,
        TransportCallback on_transport,
        ClockStepCallback on_step16,
        CcCallback on_cc = nullptr,
        NoteOffCallback on_note_off = nullptr,
        ProgramChangeCallback on_prog = nullptr
    );
    void stop();

    MidiTelemetry getTelemetry() const;

private:
    void workerLoop();

    NoteCallback on_note_;
    NoteOffCallback on_note_off_;
    ProgramChangeCallback on_prog_;
    TransportCallback on_transport_;
    ClockStepCallback on_step16_;
    CcCallback on_cc_;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<int> active_ports_{0};
    std::atomic<bool> ext_sync_{false};
    std::atomic<float> pll_bpm_{120.0f};
    std::atomic<uint32_t> rx_clocks_{0};
    std::atomic<uint32_t> rx_notes_{0};
    std::thread worker_;
};

} // namespace groovebox
