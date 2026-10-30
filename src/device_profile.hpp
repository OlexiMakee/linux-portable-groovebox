#pragma once

// Device profiles: everything that differs between Linux handhelds lives here,
// so porting to a new console = adding one profiles/<device>.ini file (no rebuild).

#include <array>
#include <string>

namespace groovebox {

enum class Btn : int {
    A, B, X, Y,
    L1, L2, R1, R2,
    Select, Start, Fn, L3, R3,
    Up, Down, Left, Right,
    VolUp, VolDown,
    Count
};

const char* btnKey(Btn b); // INI key suffix, e.g. "a", "select", "vol_up"

struct DeviceProfile {
    // [device]
    std::string name = "Generic Linux Handheld";
    std::string match;         // substring(s) of /proc/device-tree/model (pipe-separated, case-insensitive)
    std::string source = "built-in defaults";

    // [display] — logical canvas is 320x240 (4:3); physical output scales automatically (640x480, 1024x768, etc.)
    int display_width = 640;
    int display_height = 480;
    bool integer_scale = false;

    // [storage] — base data directory on SD card for samples, loops, dx7, projects, recordings
    std::string data_dir = "/storage/roms/groovebox";

    // [input] — evdev devices are found by name substring (EVIOCGNAME, pipe-separated), not by eventN index
    std::string gamepad_name = "Gamepad|joypad|trimui-input|retrogame_joypad|Xbox";
    std::string volume_name = "vol|gpio-keys";
    std::string headphones_name = "Headphones|headphone|jack";
    bool has_analog_sticks = true;
    int axis_max = 1800;
    int axis_deadzone = 60;
    bool invert_ly = false;
    bool invert_ry = false;
    std::array<int, static_cast<int>(Btn::Count)> keys{};

    // [usb] — empty strings disable the feature on devices that lack it
    std::string role_switch_path;
    std::string udc_state_path;
    std::string vbus_on_cmd;
    std::string vbus_off_cmd;
    std::string gadget_conf_path = "/storage/.cache/usbgadget/usbgadget.conf";

    // [audio]
    int internal_card = 0;
    int internal_rate = 48000;
    int usb_rate = 48000;
    std::string mux_control;   // e.g. "Playback Mux"; empty = no HP/SPK switching
    std::string mux_hp = "HP";
    std::string mux_spk = "SPK";
    std::string volume_control; // e.g. "Master Playback Volume"; empty = software volume only
    int volume_min = 0;
    int volume_max = 255;

    int key(Btn b) const { return keys[static_cast<int>(b)]; }
};

// Loads the profile matching this device (call once at startup, before any subsystem init).
// Search order: $GROOVEBOX_PROFILE_DIR, <exe dir>/profiles, ./profiles.
// Falls back to "generic.ini" and then to built-in defaults.
const DeviceProfile& loadActiveProfile();
const DeviceProfile& activeProfile();

} // namespace groovebox
