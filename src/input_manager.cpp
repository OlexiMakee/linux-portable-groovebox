#include "input_manager.hpp"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

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

bool InputManager::init() {
#if defined(__linux__)
    fd_gamepad_ = ::open("/dev/input/event3", O_RDONLY | O_NONBLOCK);
    fd_vol_ = ::open("/dev/input/event2", O_RDONLY | O_NONBLOCK);
    fd_headphones_ = ::open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
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
        bool dummy_edge = false;
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

    while (fd_gamepad_ >= 0 && ::read(fd_gamepad_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_KEY) {
            const bool down = (ev.value != 0);
            switch (ev.code) {
                // Фізичне маркування корпусу MagicX XU10:
                // 0x130=X, 0x131=A, 0x133=B, 0x134=Y
                case 0x130: setButton(state_.btn_x, state_.x_pressed, down, "BTN X"); break;
                case 0x131: setButton(state_.btn_a, state_.a_pressed, down, "BTN A"); break;
                case 0x133: setButton(state_.btn_b, state_.b_pressed, down, "BTN B"); break;
                case 0x134: setButton(state_.btn_y, state_.y_pressed, down, "BTN Y"); break;
                // b4(0x136)=L1, b5(0x137)=R1, b6(0x138)=R2, b7(0x139)=L2
                case 0x136: setButton(state_.l1, state_.l1_pressed, down, "L1"); break;
                case 0x137: setButton(state_.r1, state_.r1_pressed, down, "R1"); break;
                case 0x138: setButton(state_.r2, state_.r2_pressed, down, "R2"); break;
                case 0x139: setButton(state_.l2, state_.l2_pressed, down, "L2"); break;
                // b8(0x13a)=SELECT, b9(0x13b)=START, b10(0x13d)=L3, b11(0x13e)=R3
                case 0x13a: setButton(state_.btn_select, state_.select_pressed, down, "SELECT"); break;
                case 0x13b: setButton(state_.btn_start, state_.start_pressed, down, "START"); break;
                case 0x13d: setButton(state_.l3, dummy_edge, down, "L3"); break;
                case 0x13e: setButton(state_.r3, dummy_edge, down, "R3"); break;
                // D-Pad: 0x220=UP, 0x221=DOWN, 0x222=LEFT, 0x223=RIGHT
                case 0x220: setButton(state_.dpad_up, state_.up_pressed, down, "D-UP"); break;
                case 0x221: setButton(state_.dpad_down, state_.down_pressed, down, "D-DOWN"); break;
                case 0x222: setButton(state_.dpad_left, state_.left_pressed, down, "D-LEFT"); break;
                case 0x223: setButton(state_.dpad_right, state_.right_pressed, down, "D-RIGHT"); break;
                // Guide / Function key (0x2c0)
                case 0x2c0: setButton(state_.btn_fn, state_.fn_pressed, down, "FN (USB ROLE)"); break;
                default: break;
            }
        } else if (ev.type == EV_ABS) {
            auto norm = [](int v) -> float {
                if (std::abs(v) < 60) return 0.0f; // deadzone
                return std::clamp(static_cast<float>(v) / 1800.0f, -1.0f, 1.0f);
            };
            if (ev.code == ABS_X) {
                state_.raw_lx = ev.value;
                float n = norm(ev.value);
                if (std::fabs(n - state_.lx) > 0.01f) { state_.lx = n; state_.any_change = true; }
            } else if (ev.code == ABS_Y) {
                state_.raw_ly = ev.value;
                float n = norm(ev.value);
                if (std::fabs(n - state_.ly) > 0.01f) { state_.ly = n; state_.any_change = true; }
            } else if (ev.code == ABS_RX) {
                state_.raw_rx = ev.value;
                float n = norm(ev.value);
                if (std::fabs(n - state_.rx) > 0.01f) { state_.rx = n; state_.any_change = true; }
            } else if (ev.code == ABS_RY) {
                state_.raw_ry = ev.value;
                float n = norm(ev.value);
                if (std::fabs(n - state_.ry) > 0.01f) { state_.ry = n; state_.any_change = true; }
            }
        }
    }

    while (fd_vol_ >= 0 && ::read(fd_vol_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_KEY) {
            const bool down = (ev.value != 0);
            if (ev.code == 0x72) setButton(state_.vol_down, state_.vol_down_pressed, down, "VOL-");
            else if (ev.code == 0x73) setButton(state_.vol_up, state_.vol_up_pressed, down, "VOL+");
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
