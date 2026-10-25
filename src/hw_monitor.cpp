#include "hw_monitor.hpp"
#include "device_profile.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace groovebox {

namespace {

std::string readFirstLine(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
        line.pop_back();
    }
    return line;
}

bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }
    );
    return it != haystack.end();
}

} // namespace

HardwareMonitor::HardwareMonitor() {
    pollOnce();
}

HardwareMonitor::~HardwareMonitor() {
    stop();
}

void HardwareMonitor::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread(&HardwareMonitor::pollLoop, this);
}

void HardwareMonitor::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) {
        worker_.join();
    }
}

HardwareSnapshot HardwareMonitor::getSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void HardwareMonitor::toggleMockDigitakt() {
    bool next = !mock_digitakt_.load();
    mock_digitakt_.store(next);
    pollOnce();
}

bool HardwareMonitor::requestUsbRoleSwitch(const std::string& target_role) {
#if defined(__linux__)
    // Шляхи та команди VBUS беруться з профілю пристрою (profiles/<device>.ini).
    const auto& prof = activeProfile();
    if (prof.role_switch_path.empty()) {
        std::printf("[USB] Profile '%s' has no role switch, ignoring\n", prof.name.c_str());
        return false;
    }
    std::string cmd;
    if (target_role == "host") {
        // 1. Вимикаємо USB_MODE у конфігу ROCKNIX (щоб udev не повернув порт у cdc!),
        //    відв'язуємо UDC, переводимо порт OTG у режим Host і вмикаємо 5V VBUS
        cmd = "modprobe snd-usb-audio implicit_fb=1,1,1,1 lowlatency=0 2>/dev/null; modprobe snd-rawmidi 2>/dev/null; modprobe snd-seq-midi 2>/dev/null; ";
        if (!prof.gadget_conf_path.empty()) cmd += "echo 'USB_MODE=disabled' > " + prof.gadget_conf_path + " 2>/dev/null; ";
        cmd += "echo '' > /sys/kernel/config/usb_gadget/cdc/UDC 2>/dev/null; ";
        cmd += "echo host > " + prof.role_switch_path + " 2>/dev/null; ";
        if (!prof.vbus_on_cmd.empty()) cmd += prof.vbus_on_cmd + " 2>/dev/null; ";
    } else {
        // 2. Вимикаємо 5V VBUS на OTG, повертаємо USB_MODE=cdc і піднімаємо USB-SSH
        if (!prof.vbus_off_cmd.empty()) cmd += prof.vbus_off_cmd + " 2>/dev/null; ";
        if (!prof.gadget_conf_path.empty()) cmd += "echo 'USB_MODE=cdc' > " + prof.gadget_conf_path + " 2>/dev/null; ";
        cmd += "echo device > " + prof.role_switch_path + " 2>/dev/null; ";
        cmd += "/usr/bin/usbgadget start cdc 2>/dev/null; systemctl start sshd.service 2>/dev/null; ";
    }
    cmd = "(" + cmd + ") &";
    int rc = std::system(cmd.c_str());
    (void)rc;
#endif
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.usb_role = target_role;
    }
    return true;
}

void HardwareMonitor::toggleUsbRole() {
    std::string cur = getSnapshot().usb_role;
    if (cur == "host") {
        requestUsbRoleSwitch("device");
    } else {
        requestUsbRoleSwitch("host");
    }
}

void HardwareMonitor::pollLoop() {
    while (running_.load()) {
        pollOnce();
        for (int i = 0; i < 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

void HardwareMonitor::pollOnce() {
    HardwareSnapshot next;

    // 1. Parse /proc/asound/cards
    std::ifstream cards_file("/proc/asound/cards");
    if (cards_file.is_open()) {
        std::string line;
        while (std::getline(cards_file, line)) {
            if (line.size() > 4 && std::isdigit(static_cast<unsigned char>(line[1]))) {
                int idx = line[1] - '0';
                auto lb = line.find('[');
                auto rb = line.find(']');
                auto dash = line.find(" - ");
                std::string id = (lb != std::string::npos && rb != std::string::npos && rb > lb)
                                     ? line.substr(lb + 1, rb - lb - 1)
                                     : "card";
                while (!id.empty() && id.back() == ' ') id.pop_back();
                std::string name = (dash != std::string::npos) ? line.substr(dash + 3) : id;
                // Будь-яка карта, крім вбудованої (audio.internal_card у профілі), вважається зовнішнім Class-Compliant інтерфейсом
                bool is_internal = (idx == activeProfile().internal_card);
                bool is_usb = !is_internal;
                bool is_dt = containsIgnoreCase(name, "Digitakt") || containsIgnoreCase(id, "Digitakt") || containsIgnoreCase(name, "Elektron");
                next.audio_cards.push_back({idx, id, name, is_usb, is_dt});
            }
        }
    }

    if (next.audio_cards.empty()) {
        next.audio_cards.push_back({activeProfile().internal_card, "internal", "Internal DAC", false, false});
    }

    if (mock_digitakt_.load()) {
        next.audio_cards.push_back({1, "DigitaktII", "Elektron Digitakt II (Mock)", true, true});
    }

    // Визначаємо пріоритетний аудіовихід:
    // 1. Elektron Digitakt II (якщо підключений)
    // 2. Будь-який інший Class-Compliant USB Audio інтерфейс (48 000 Hz)
    // 3. Вбудований ЦАП (audio.internal_card / internal_rate з профілю)
    next.active_audio_card = activeProfile().internal_card;
    next.active_audio_name = "INTERNAL DAC (hw:" + std::to_string(activeProfile().internal_card) + ")";
    for (const auto& c : next.audio_cards) {
        if (c.is_digitakt) {
            next.active_audio_card = c.index;
            next.active_audio_name = "DIGITAKT II USB (48k)";
            next.digitakt_connected = true;
            next.midi_digitakt_connected = true;
            break;
        } else if (c.is_usb) {
            next.active_audio_card = c.index;
            next.active_audio_name = c.id + " USB (48k)";
            next.digitakt_connected = true;
        }
    }

    // 2. Перевірка /proc/asound/seq/clients та /dev/snd/midiC*D* для будь-яких Class-Compliant USB MIDI пристроїв
    std::ifstream seq_file("/proc/asound/seq/clients");
    if (seq_file.is_open()) {
        std::string line;
        while (std::getline(seq_file, line)) {
            if (containsIgnoreCase(line, "USB") || containsIgnoreCase(line, "Digitakt") || containsIgnoreCase(line, "Elektron") || containsIgnoreCase(line, "MIDI")) {
                if (!containsIgnoreCase(line, "System") && !containsIgnoreCase(line, "Midi Through")) {
                    next.midi_digitakt_connected = true;
                }
            }
        }
    }

    // 3. Стан USB OTG Role Switch та UDC (шляхи з профілю пристрою)
    const auto& prof = activeProfile();
    std::string role = prof.role_switch_path.empty() ? "" : readFirstLine(prof.role_switch_path.c_str());
    next.usb_role = role.empty() ? "n/a" : role;

    std::string udc = prof.udc_state_path.empty() ? "" : readFirstLine(prof.udc_state_path.c_str());
    next.udc_state = udc.empty() ? "n/a" : udc;

    // 4. CPU Load з /proc/stat
    std::ifstream stat_file("/proc/stat");
    if (stat_file.is_open()) {
        std::string cpu_label;
        uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        stat_file >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        uint64_t idle_all = idle + iowait;
        uint64_t total = user + nice + system + idle_all + irq + softirq + steal;
        if (prev_cpu_total_ > 0 && total > prev_cpu_total_) {
            uint64_t diff_total = total - prev_cpu_total_;
            uint64_t diff_idle = idle_all - prev_cpu_idle_;
            next.cpu_load_percent = 100.0f * static_cast<float>(diff_total - diff_idle) / static_cast<float>(diff_total);
        }
        prev_cpu_total_ = total;
        prev_cpu_idle_ = idle_all;
    } else {
        next.cpu_load_percent = 4.2f;
    }

    // 5. Температура SoC RK3326 (/sys/class/thermal/thermal_zone0/temp)
    std::string temp_str = readFirstLine("/sys/class/thermal/thermal_zone0/temp");
    if (!temp_str.empty()) {
        next.cpu_temp_c = std::atoi(temp_str.c_str()) / 1000;
    } else {
        next.cpu_temp_c = 42;
    }

    // 6. Використання RAM з /proc/meminfo
    std::ifstream mem_file("/proc/meminfo");
    if (mem_file.is_open()) {
        std::string key;
        uint64_t val_kb = 0;
        std::string unit;
        uint64_t total_kb = 0;
        uint64_t avail_kb = 0;
        while (mem_file >> key >> val_kb >> unit) {
            if (key == "MemTotal:") total_kb = val_kb;
            else if (key == "MemAvailable:") avail_kb = val_kb;
        }
        if (total_kb > 0) {
            next.ram_total_mb = static_cast<int>(total_kb / 1024);
            next.ram_used_mb = static_cast<int>((total_kb - avail_kb) / 1024);
        }
    } else {
        next.ram_total_mb = 1024;
        next.ram_used_mb = 188;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        next.poll_counter = snapshot_.poll_counter + 1;
        snapshot_ = std::move(next);
    }
}

} // namespace groovebox
