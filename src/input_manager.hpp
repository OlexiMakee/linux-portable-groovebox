#pragma once

#include <cstdint>
#include <string>

union SDL_Event;

namespace groovebox {

struct InputState {
    // 4 верхні бампери зліва направо: L1, L2, R2, R1
    bool l1 = false;
    bool l2 = false;
    bool r2 = false;
    bool r1 = false;

    // Хрестовина (D-Pad)
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;

    // Ігрові кнопки ABXY
    bool btn_a = false;
    bool btn_b = false;
    bool btn_x = false;
    bool btn_y = false;

    // Центральні та системні кнопки
    bool btn_select = false;
    bool btn_start = false;
    bool btn_fn = false;
    bool l3 = false;
    bool r3 = false;
    bool vol_up = false;
    bool vol_down = false;

    // Осі аналогових стіків (-1.0f .. +1.0f) та сирі значення АЦП (-1800 .. +1800)
    float lx = 0.0f;
    float ly = 0.0f;
    float rx = 0.0f;
    float ry = 0.0f;
    int raw_lx = 0;
    int raw_ly = 0;
    int raw_rx = 0;
    int raw_ry = 0;

    // Edge-тригери (натиснуто саме в цьому кадрі)
    bool l1_pressed = false;
    bool l2_pressed = false;
    bool r2_pressed = false;
    bool r1_pressed = false;
    bool a_pressed = false;
    bool b_pressed = false;
    bool x_pressed = false;
    bool y_pressed = false;
    bool up_pressed = false;
    bool down_pressed = false;
    bool left_pressed = false;
    bool right_pressed = false;
    bool start_pressed = false;
    bool select_pressed = false;
    bool fn_pressed = false;
    bool vol_up_pressed = false;
    bool vol_down_pressed = false;

    bool headphones_plugged = false;
    std::string last_event_name = "READY";
    bool any_change = false;
};

class InputManager {
public:
    InputManager();
    ~InputManager();

    bool init();
    void shutdown();

    // Очищує edge-прапорці перед новим циклом опитування
    void beginFrame();

    // Оновлює автоповтор D-Pad при тривалому затисканні (затримка 250мс, повтор 70мс)
    void updateRepeat(uint32_t now_ms);

    // Обробка подій SDL2 (для macOS клавіатури / закриття вікна)
    void processSdlEvent(const SDL_Event& ev, bool& quit_requested);

    // Неблокуюче читання Linux evdev (/dev/input/event2, /dev/input/event3)
    void pollEvdev(bool& quit_requested);

    const InputState& state() const { return state_; }

private:
    void setButton(bool& field, bool& edge_field, bool pressed, const char* label);

    InputState state_;
    int fd_gamepad_ = -1;
    int fd_vol_ = -1;
    int fd_headphones_ = -1;

    // Таймінги автоповтору хрестовини (D-Pad auto-repeat)
    uint32_t dpad_up_start_ms_ = 0;
    uint32_t dpad_up_last_ms_ = 0;
    uint32_t dpad_down_start_ms_ = 0;
    uint32_t dpad_down_last_ms_ = 0;
    uint32_t dpad_left_start_ms_ = 0;
    uint32_t dpad_left_last_ms_ = 0;
    uint32_t dpad_right_start_ms_ = 0;
    uint32_t dpad_right_last_ms_ = 0;
};

} // namespace groovebox
