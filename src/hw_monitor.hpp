#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace groovebox {

struct AudioCardInfo {
    int index = -1;
    std::string id;
    std::string name;
    bool is_usb = false;
    bool is_digitakt = false;
};

struct HardwareSnapshot {
    std::vector<AudioCardInfo> audio_cards;
    int active_audio_card = 0;
    std::string active_audio_name = "RK817 DAC (hw:0)";
    bool digitakt_connected = false;
    bool midi_digitakt_connected = false;

    std::string usb_role = "device";     // "device" або "host"
    std::string udc_state = "configured"; // "configured", "not attached", тощо
    bool headphones_plugged = false;

    float cpu_load_percent = 0.0f;
    int cpu_temp_c = 0;
    int ram_used_mb = 0;
    int ram_total_mb = 1024;
    uint32_t poll_counter = 0;
};

class HardwareMonitor {
public:
    HardwareMonitor();
    ~HardwareMonitor();

    void start();
    void stop();

    HardwareSnapshot getSnapshot() const;

    // Для тестування на Desktop або перемикання Mock Digitakt II
    void toggleMockDigitakt();

    // Перемикання ролі USB-порту на XU10 ("device" <-> "host") з керуванням 5V VBUS (OTG_SWITCH)
    bool requestUsbRoleSwitch(const std::string& target_role);
    void toggleUsbRole();

private:
    void pollLoop();
    void pollOnce();

    mutable std::mutex mutex_;
    HardwareSnapshot snapshot_;
    std::atomic<bool> running_{false};
    std::atomic<bool> mock_digitakt_{false};
    std::thread worker_;

    uint64_t prev_cpu_total_ = 0;
    uint64_t prev_cpu_idle_ = 0;
};

} // namespace groovebox
