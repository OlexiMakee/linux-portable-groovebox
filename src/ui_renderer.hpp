#pragma once

#include "audio_engine.hpp"
#include "config.hpp"
#include "hw_monitor.hpp"
#include "input_manager.hpp"
#include "midi_engine.hpp"

#include <string>

struct SDL_Window;
struct SDL_Renderer;

namespace groovebox {

struct AppModel {
    WorkspaceTab active_tab = WorkspaceTab::kSeq;
    WorkspaceTab previous_tab = WorkspaceTab::kSeq;
    bool high_res_mode = true; // true = 320x240 (основний режим з високою щільністю інформації)
    bool dev_shortcuts_enabled = kDefaultDevShortcuts;
    bool transport_playing = false;
    float bpm = 120.0f;
    uint32_t current_step = 0; // 0..127
    float phase = 0.0f;
    int selected_track = 0;    // 0..3 (TRK 1..4)
    int selected_param = 0;    // 0 = ENGINE TYPE, 1..6 = параметри p[0..5]
    int cursor_row = 0;        // 0..15 (16 рядків: 4 треки x 4 голоси)
    int cursor_step = 0;       // 0..127 (курсор кроку)
    int seq_page = 0;          // 0 = кроки 0..63, 1 = кроки 64..127
    uint32_t pattern_length = 64; // 16, 32, 64, 128
    std::array<uint8_t, kTotalRows> row_lengths{}; // Поліметрія (1..128 кроків для кожного рядка)

    // Step Inspector (Hold Trig / Button A)
    bool step_inspector_open = false;
    int step_inspector_param = 0; // 0..9 (TYPE, NOTE, VEL, GATE, SLIDE, ATK, DEC, SUS, REL, CUTOFF)
    uint32_t a_press_time_ms = 0;
    bool a_held = false;
    bool a_long_press_triggered = false;
    bool stick_plock_tweaked = false;
    float plock_cutoff_live = -1.0f;
    int sys_menu_cursor = 0; // Курсор вертикального списку меню налаштувань [G] SYS
};

class UiRenderer {
public:
    UiRenderer();
    ~UiRenderer();

    bool init();
    void shutdown();

    void applyLogicalResolution(bool high_res);

    void render(
        const AppModel& app,
        const HardwareSnapshot& hw,
        const InputState& input,
        const AudioEngine& audio_engine,
        const AudioEngineTelemetry& audio,
        const MidiTelemetry& midi,
        float master_vol
    );

private:
    void setColor(ColorRGB c);
    void fillRect(int x, int y, int w, int h, ColorRGB c);
    void drawRect(int x, int y, int w, int h, ColorRGB c);
    void drawLine(int x1, int y1, int x2, int y2, ColorRGB c);
    void drawChar(int x, int y, char ch, ColorRGB c, int scale = 1);
    void drawText(int x, int y, const std::string& text, ColorRGB c, int scale = 1);

    void renderTopBar(const AppModel& app, const InputState& input, int w, int s);
    void renderWorkspace(
        const AppModel& app,
        const HardwareSnapshot& hw,
        const InputState& input,
        const AudioEngine& audio_engine,
        const AudioEngineTelemetry& audio,
        const MidiTelemetry& midi,
        float master_vol,
        int w,
        int h,
        int s
    );
    void renderBottomHud(const AppModel& app, const HardwareSnapshot& hw, const InputState& input, int w, int h, int s);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    int logical_w_ = kLowResWidth;
    int logical_h_ = kLowResHeight;
};

} // namespace groovebox
