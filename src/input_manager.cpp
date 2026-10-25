#include "input_manager.hpp"
#include "device_profile.hpp"

#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace groovebox {

InputManager::InputManager() = default;

InputManager::~InputManager() {
    shutdown();
}

#if defined(__linux__)
namespace {

// Знаходить evdev-пристрій за підрядком імені (EVIOCGNAME), а не за жорстким номером eventN —
// номери змінюються між прошивками й консолями.
int openEvdevByName(const std::string& needle) {
    if (needle.empty()) return -1;
    std::string want = needle;
    for (auto& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < 32; ++i) {
        char path[32];
        std::snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = ::open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[128] = {0};
        if (::ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
            std::string n = name;
            for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (n.find(want) != std::string::npos) {
                std::printf("[INPUT] '%s' -> %s (%s)\n", needle.c_str(), path, name);
                return fd;
            }
        }
        ::close(fd);
    }
    std::printf("[INPUT] '%s' not found\n", needle.c_str());
    return -1;
}

} // namespace
#endif

bool InputManager::init() {
#if defined(__linux__)
    const auto& prof = activeProfile();
    fd_gamepad_ = openEvdevByName(prof.gamepad_name);
    fd_vol_ = openEvdevByName(prof.volume_name);
    fd_headphones_ = openEvdevByName(prof.headphones_name);
    if (fd_vol_ >= 0 && fd_vol_ == fd_gamepad_) fd_vol_ = -1;
    if (fd_headphones_ >= 0) {
        uint8_t sw_bits[8] = {0};
        if (::ioctl(fd_headphones_, EVIOCGSW(sizeof(sw_bits)), sw_bits) >= 0) {
            // SW_HEADPHONE_INSERT = 0x02
            state_.headphones_plugged = ((sw_bits[0] & (1u << 2)) != 0);
        }
    }
#endif
    return true;
}

void InputManager::shutdown() {
#if defined(__linux__)
    if (fd_gamepad_ >= 0) { ::close(fd_gamepad_); fd_gamepad_ = -1; }
    if (fd_vol_ >= 0) { ::close(fd_vol_); fd_vol_ = -1; }
    if (fd_headphones_ >= 0) { ::close(fd_headphones_); fd_headphones_ = -1; }
#endif
}

void InputManager::beginFrame() {
    state_.l1_pressed = false;
    state_.l2_pressed = false;
    state_.r2_pressed = false;
    state_.r1_pressed = false;
    state_.a_pressed = false;
    state_.b_pressed = false;
    state_.x_pressed = false;
    state_.y_pressed = false;
    state_.up_pressed = false;
    state_.down_pressed = false;
    state_.left_pressed = false;
    state_.right_pressed = false;
    state_.start_pressed = false;
    state_.select_pressed = false;
    state_.fn_pressed = false;
    state_.vol_up_pressed = false;
    state_.vol_down_pressed = false;
    state_.any_change = false;
}

void InputManager::updateRepeat(uint32_t now_ms) {
    auto checkRepeat = [&](bool held, bool& edge_field, uint32_t& start_ms, uint32_t& last_ms, const char* label) {
        if (held) {
            if (start_ms == 0) {
                start_ms = now_ms;
                last_ms = now_ms;
            } else if (now_ms - start_ms >= 250 && now_ms - last_ms >= 70) {
                edge_field = true;
                last_ms = now_ms;
                state_.any_change = true;
                state_.last_event_name = label;
            }
        } else {
            start_ms = 0;
            last_ms = 0;
        }
    };

    checkRepeat(state_.dpad_up, state_.up_pressed, dpad_up_start_ms_, dpad_up_last_ms_, "D-UP (REP)");
    checkRepeat(state_.dpad_down, state_.down_pressed, dpad_down_start_ms_, dpad_down_last_ms_, "D-DOWN (REP)");
    checkRepeat(state_.dpad_left, state_.left_pressed, dpad_left_start_ms_, dpad_left_last_ms_, "D-LEFT (REP)");
    checkRepeat(state_.dpad_right, state_.right_pressed, dpad_right_start_ms_, dpad_right_last_ms_, "D-RIGHT (REP)");
}

void InputManager::setButton(bool& field, bool& edge_field, bool pressed, const char* label) {
    if (field != pressed) {
        field = pressed;
        state_.any_change = true;
        if (pressed) {
            edge_field = true;
            state_.last_event_name = label;
        }
    }
}

void InputManager::processSdlEvent(const SDL_Event& ev, bool& quit_requested) {
    if (ev.type == SDL_QUIT) {
        quit_requested = true;
        return;
    }
    if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
        if (ev.key.repeat != 0) return;
        const bool down = (ev.type == SDL_KEYDOWN);
        switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE:
                if (down) quit_requested = true;
                break;
            // Верхній ряд вкладок на клавіатурі Mac: 1=L1, 2=L2, 3=R2, 4=R1
            case SDLK_1: setButton(state_.l1, state_.l1_pressed, down, "L1"); break;
            case SDLK_2: setButton(state_.l2, state_.l2_pressed, down, "L2"); break;
            case SDLK_3: setButton(state_.r2, state_.r2_pressed, down, "R2"); break;
            case SDLK_4: setButton(state_.r1, state_.r1_pressed, down, "R1"); break;
            // D-Pad: Стрілки
            case SDLK_UP:    setButton(state_.dpad_up, state_.up_pressed, down, "D-UP"); break;
            case SDLK_DOWN:  setButton(state_.dpad_down, state_.down_pressed, down, "D-DOWN"); break;
            case SDLK_LEFT:  setButton(state_.dpad_left, state_.left_pressed, down, "D-LEFT"); break;
            case SDLK_RIGHT: setButton(state_.dpad_right, state_.right_pressed, down, "D-RIGHT"); break;
            // ABXY: Z=B, X=A, A=Y, S=X
            case SDLK_x: setButton(state_.btn_a, state_.a_pressed, down, "BTN A"); break;
            case SDLK_z: setButton(state_.btn_b, state_.b_pressed, down, "BTN B"); break;
            case SDLK_s: setButton(state_.btn_x, state_.x_pressed, down, "BTN X"); break;
            case SDLK_a: setButton(state_.btn_y, state_.y_pressed, down, "BTN Y"); break;
            // Select / Start: Tab / Return (Space)
            case SDLK_TAB:
            case SDLK_BACKSPACE:
                setButton(state_.btn_select, state_.select_pressed, down, "SELECT");
                break;
            case SDLK_RETURN:
            case SDLK_SPACE:
                setButton(state_.btn_start, state_.start_pressed, down, "START");
                break;
            case SDLK_f:
                setButton(state_.btn_fn, state_.fn_pressed, down, "FN (USB ROLE)");
                break;
            case SDLK_EQUALS:
                setButton(state_.vol_up, state_.vol_up_pressed, down, "VOL+");
                break;
            case SDLK_MINUS:
                setButton(state_.vol_down, state_.vol_down_pressed, down, "VOL-");
                break;
            default:
                break;
        }
    }
}

void InputManager::pollEvdev(bool& quit_requested) {
#if defined(__linux__)
    struct input_event ev {};
    bool dummy_edge = false;

    const auto& prof = activeProfile();

    // Коди кнопок беруться з профілю пристрою (profiles/<device>.ini), а не з коду.
    auto handleKey = [&](int code, bool down) {
        if (code <= 0) return;
        if      (code == prof.key(Btn::A))       setButton(state_.btn_a, state_.a_pressed, down, "BTN A");
        else if (code == prof.key(Btn::B))       setButton(state_.btn_b, state_.b_pressed, down, "BTN B");
        else if (code == prof.key(Btn::X))       setButton(state_.btn_x, state_.x_pressed, down, "BTN X");
        else if (code == prof.key(Btn::Y))       setButton(state_.btn_y, state_.y_pressed, down, "BTN Y");
        else if (code == prof.key(Btn::L1))      setButton(state_.l1, state_.l1_pressed, down, "L1");
        else if (code == prof.key(Btn::L2))      setButton(state_.l2, state_.l2_pressed, down, "L2");
        else if (code == prof.key(Btn::R1))      setButton(state_.r1, state_.r1_pressed, down, "R1");
        else if (code == prof.key(Btn::R2))      setButton(state_.r2, state_.r2_pressed, down, "R2");
        else if (code == prof.key(Btn::Select))  setButton(state_.btn_select, state_.select_pressed, down, "SELECT");
        else if (code == prof.key(Btn::Start))   setButton(state_.btn_start, state_.start_pressed, down, "START");
        else if (code == prof.key(Btn::Fn))      setButton(state_.btn_fn, state_.fn_pressed, down, "FN (USB ROLE)");
        else if (code == prof.key(Btn::L3))      setButton(state_.l3, dummy_edge, down, "L3");
        else if (code == prof.key(Btn::R3))      setButton(state_.r3, dummy_edge, down, "R3");
        else if (code == prof.key(Btn::Up))      setButton(state_.dpad_up, state_.up_pressed, down, "D-UP");
        else if (code == prof.key(Btn::Down))    setButton(state_.dpad_down, state_.down_pressed, down, "D-DOWN");
        else if (code == prof.key(Btn::Left))    setButton(state_.dpad_left, state_.left_pressed, down, "D-LEFT");
        else if (code == prof.key(Btn::Right))   setButton(state_.dpad_right, state_.right_pressed, down, "D-RIGHT");
        else if (code == prof.key(Btn::VolUp))   setButton(state_.vol_up, state_.vol_up_pressed, down, "VOL+");
        else if (code == prof.key(Btn::VolDown)) setButton(state_.vol_down, state_.vol_down_pressed, down, "VOL-");
    };

    const float axis_max = static_cast<float>(prof.axis_max);
    const int deadzone = prof.axis_deadzone;
    auto norm = [&](int v) -> float {
        if (std::abs(v) < deadzone) return 0.0f;
        return std::clamp(static_cast<float>(v) / axis_max, -1.0f, 1.0f);
    };

    while (fd_gamepad_ >= 0 && ::read(fd_gamepad_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_KEY) {
            handleKey(ev.code, ev.value != 0);
        } else if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) {
                state_.raw_lx = ev.value;
                float n = norm(ev.value);
                if (std::fabs(n - state_.lx) > 0.01f) { state_.lx = n; state_.any_change = true; }
            } else if (ev.code == ABS_Y) {
                state_.raw_ly = ev.value;
                float n = norm(ev.value) * (prof.invert_ly ? -1.0f : 1.0f);
                if (std::fabs(n - state_.ly) > 0.01f) { state_.ly = n; state_.any_change = true; }
            } else if (ev.code == ABS_RX) {
                state_.raw_rx = ev.value;
                float n = norm(ev.value);
                if (std::fabs(n - state_.rx) > 0.01f) { state_.rx = n; state_.any_change = true; }
            } else if (ev.code == ABS_RY) {
                state_.raw_ry = ev.value;
                float n = norm(ev.value) * (prof.invert_ry ? -1.0f : 1.0f);
                if (std::fabs(n - state_.ry) > 0.01f) { state_.ry = n; state_.any_change = true; }
            }
        }
    }

    while (fd_vol_ >= 0 && ::read(fd_vol_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_KEY) {
            handleKey(ev.code, ev.value != 0);
        }
    }

    while (fd_headphones_ >= 0 && ::read(fd_headphones_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_SW && ev.code == SW_HEADPHONE_INSERT) {
            state_.headphones_plugged = (ev.value != 0);
            state_.last_event_name = state_.headphones_plugged ? "HP PLUGGED" : "HP UNPLUGGED";
            state_.any_change = true;
        }
    }

    // Вихід за одночасним затисканням SELECT + START + L1 + R1
    if (state_.btn_select && state_.btn_start && state_.l1 && state_.r1) {
        quit_requested = true;
    }
#else
    (void)quit_requested;
#endif
}

} // namespace groovebox
