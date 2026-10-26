#pragma once

#include "audio_engine.hpp"
#include "config.hpp"
#include "hw_monitor.hpp"
#include "input_manager.hpp"
#include "midi_engine.hpp"

#include <array>
#include <cstdint>

namespace groovebox {

class UiShell;

// Ідентифікатори вікон (views). Назви — у viewName(); порядок вкладок задається в layout.cpp
enum class ViewId : uint8_t {
    kSeq = 0,  // SEQ     — матричний секвенсор
    kSynth,    // SYNTH   — таблиця параметрів інструмента обраного треку
    kFx,       // FX      — send-ефекти та вихідна шина
    kMix,      // MIX     — мікшер треків + MASTER
    kScope,    // SCOPE   — осцилограф + VU
    kSys,      // SYS     — системні налаштування
    kInspect,  // INSPECT — інспектор кроку (popup поверх SEQ, поки затиснута A)
    kCount
};

inline constexpr int kViewCount = static_cast<int>(ViewId::kCount);

inline constexpr const char* kViewNames[kViewCount] = {
    "SEQ", "SYNTH", "FX", "MIX", "SCOPE", "SYS", "INSPECT"
};

inline constexpr const char* viewName(ViewId v) {
    return kViewNames[static_cast<int>(v)];
}

// Стан застосунку (модель) — без жодної логіки рендеру чи вводу
struct AppModel {
    ViewId active_view = ViewId::kSeq;
    ViewId previous_view = ViewId::kSeq; // для повернення з SYS (toggle_back)

    bool transport_playing = false;
    float bpm = 120.0f;
    uint32_t current_step = 0;
    uint32_t pattern_length = 64;
    uint32_t last_step_ms = 0;

    int selected_track = 0; // 0..3
    int cursor_row = 0;     // 0..15 (+ слот [+])
    int cursor_step = 0;    // 0..127
    int seq_page = 0;

    // Курсор (рядок/колонка/скрол) для кожного табличного вікна
    std::array<int, kViewCount> grid_row{};
    std::array<int, kViewCount> grid_col{};
    std::array<int, kViewCount> grid_scroll{};

    // Інспектор кроку + P-Lock зі стіка (поки затиснута A)
    bool step_inspector_open = false;
    bool stick_plock_tweaked = false;
    float plock_cutoff_live = -1.0f;

    // Короткий статус останньої дії (для HUD)
    std::string last_action = "READY";
};

// Контекст, який отримують усі Param / Action / View (DIP: залежність від абстракції контексту)
struct UiContext {
    AppModel& app;
    AudioEngine& audio;
    HardwareMonitor* hw_mon;          // може бути nullptr (render_tour)
    const InputState& in;
    const HardwareSnapshot& hw;
    const AudioEngineTelemetry& tel;
    const MidiTelemetry& midi;
    uint32_t now_ms = 0;
    UiShell* shell = nullptr;
};

} // namespace groovebox
