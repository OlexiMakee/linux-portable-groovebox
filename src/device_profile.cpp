#include "device_profile.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <dirent.h>
#include <unistd.h>
#endif

namespace groovebox {

namespace {

DeviceProfile g_profile;

constexpr const char* kBtnKeys[static_cast<int>(Btn::Count)] = {
    "a", "b", "x", "y",
    "l1", "l2", "r1", "r2",
    "select", "start", "fn", "l3", "r3",
    "up", "down", "left", "right",
    "vol_up", "vol_down",
};

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int toInt(const std::string& v, int fallback) {
    if (v.empty()) return fallback;
    char* end = nullptr;
    long r = std::strtol(v.c_str(), &end, 0); // accepts 0x.. hex
    return (end && end != v.c_str()) ? static_cast<int>(r) : fallback;
}

using IniMap = std::map<std::string, std::string>; // "section.key" -> value

bool parseIni(const std::string& path, IniMap& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line, section;
    while (std::getline(f, line)) {
        auto hash = line.find_first_of(";#");
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[section + "." + lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
    }
    return true;
}

void applyIni(const IniMap& m, DeviceProfile& p, const std::string& path) {
    auto get = [&](const char* k, const std::string& def) {
        auto it = m.find(k);
        return it != m.end() ? it->second : def;
    };
    auto geti = [&](const char* k, int def) { return toInt(get(k, ""), def); };

    p.source = path;
    p.name = get("device.name", p.name);
    p.match = get("device.match", p.match);

    p.display_width = std::max(320, geti("display.width", p.display_width));
    p.display_height = std::max(240, geti("display.height", p.display_height));
    p.integer_scale = geti("display.integer_scale", p.integer_scale ? 1 : 0) != 0;

    p.data_dir = get("storage.data_dir", p.data_dir);

    p.gamepad_name = get("input.gamepad", p.gamepad_name);
    p.volume_name = get("input.volume", p.volume_name);
    p.headphones_name = get("input.headphones", p.headphones_name);
    p.has_analog_sticks = geti("input.has_analog_sticks", p.has_analog_sticks ? 1 : 0) != 0;
    p.axis_max = std::max(1, geti("input.axis_max", p.axis_max));
    p.axis_deadzone = geti("input.axis_deadzone", p.axis_deadzone);
    p.invert_ly = geti("input.invert_ly", p.invert_ly ? 1 : 0) != 0;
    p.invert_ry = geti("input.invert_ry", p.invert_ry ? 1 : 0) != 0;
    for (int i = 0; i < static_cast<int>(Btn::Count); ++i) {
        std::string k = std::string("input.key.") + kBtnKeys[i];
        p.keys[i] = geti(k.c_str(), p.keys[i]);
    }

    p.role_switch_path = get("usb.role_switch", p.role_switch_path);
    p.udc_state_path = get("usb.udc_state", p.udc_state_path);
    p.vbus_on_cmd = get("usb.vbus_on", p.vbus_on_cmd);
    p.vbus_off_cmd = get("usb.vbus_off", p.vbus_off_cmd);
    p.gadget_conf_path = get("usb.gadget_conf", p.gadget_conf_path);

    p.internal_card = geti("audio.internal_card", p.internal_card);
    p.internal_rate = geti("audio.internal_rate", p.internal_rate);
    p.usb_rate = geti("audio.usb_rate", p.usb_rate);
    p.mux_control = get("audio.mux_control", p.mux_control);
    p.mux_hp = get("audio.mux_hp", p.mux_hp);
    p.mux_spk = get("audio.mux_spk", p.mux_spk);
    p.volume_control = get("audio.volume_control", p.volume_control);
    p.volume_min = geti("audio.volume_min", p.volume_min);
    p.volume_max = geti("audio.volume_max", p.volume_max);
}

std::string readDeviceModel() {
    std::ifstream f("/proc/device-tree/model", std::ios::binary);
    if (!f.is_open()) return {};
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    return trim(s);
}

std::vector<std::string> profileDirs() {
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("GROOVEBOX_PROFILE_DIR")) dirs.emplace_back(env);
#if defined(__linux__)
    char exe[512] = {0};
    ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        std::string p(exe, static_cast<size_t>(n));
        auto slash = p.rfind('/');
        if (slash != std::string::npos) dirs.push_back(p.substr(0, slash) + "/profiles");
    }
#endif
    dirs.emplace_back("profiles");
    return dirs;
}

std::vector<std::string> listIni(const std::string& dir) {
    std::vector<std::string> files;
#if defined(__linux__) || defined(__APPLE__)
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n[0] != '.' && n.compare(n.size() - 4, 4, ".ini") == 0) {
                files.push_back(dir + "/" + n);
            }
        }
        ::closedir(d);
    }
#endif
    std::sort(files.begin(), files.end());
    return files;
}

bool matchesAnyToken(const std::string& haystack_lower, const std::string& pattern_lower) {
    if (haystack_lower.empty() || pattern_lower.empty()) return false;
    size_t start = 0;
    while (start < pattern_lower.size()) {
        size_t sep = pattern_lower.find('|', start);
        if (sep == std::string::npos) sep = pattern_lower.size();
        std::string token = trim(pattern_lower.substr(start, sep - start));
        if (!token.empty() && haystack_lower.find(token) != std::string::npos) {
            return true;
        }
        start = sep + 1;
    }
    return false;
}

} // namespace

const char* btnKey(Btn b) {
    int i = static_cast<int>(b);
    return (i >= 0 && i < static_cast<int>(Btn::Count)) ? kBtnKeys[i] : "?";
}

const DeviceProfile& loadActiveProfile() {
    g_profile = DeviceProfile{};
    const char* env_prof = std::getenv("GROOVEBOX_PROFILE");
    const std::string forced_prof = env_prof ? lower(trim(env_prof)) : "";
    const std::string model = lower(readDeviceModel());

    std::string generic_path;
    IniMap generic_ini;
    for (const auto& dir : profileDirs()) {
        for (const auto& path : listIni(dir)) {
            IniMap ini;
            if (!parseIni(path, ini)) continue;
            const std::string path_low = lower(path);
            auto it_name = ini.find("device.name");
            const std::string name_low = it_name != ini.end() ? lower(it_name->second) : "";
            if (!forced_prof.empty() &&
                (path_low.find(forced_prof) != std::string::npos || name_low.find(forced_prof) != std::string::npos)) {
                applyIni(ini, g_profile, path);
                std::printf("[PROFILE] Forced '%s' via GROOVEBOX_PROFILE (%s)\n", g_profile.name.c_str(), path.c_str());
                return g_profile;
            }
            auto it = ini.find("device.match");
            const std::string match = it != ini.end() ? lower(it->second) : "";
            if (forced_prof.empty() && matchesAnyToken(model, match)) {
                applyIni(ini, g_profile, path);
                std::printf("[PROFILE] '%s' matched model '%s' (%s)\n", g_profile.name.c_str(), model.c_str(), path.c_str());
                return g_profile;
            }
            if (generic_path.empty() && path.size() >= 11 && path.compare(path.size() - 11, 11, "generic.ini") == 0) {
                generic_path = path;
                generic_ini = std::move(ini);
            }
        }
    }
    if (!generic_path.empty()) {
        applyIni(generic_ini, g_profile, generic_path);
    }
    std::printf("[PROFILE] No match for model '%s', using '%s' (%s)\n", model.c_str(), g_profile.name.c_str(), g_profile.source.c_str());
    return g_profile;
}

const DeviceProfile& activeProfile() {
    return g_profile;
}

} // namespace groovebox
