#pragma once

#include "arpeggiator.hpp"
#include "audio_engine.hpp"
#include "config.hpp"
#include "hw_monitor.hpp"
#include "input_manager.hpp"
#include "midi_engine.hpp"
#include "pattern_manager.hpp"
#include "sampler_engine.hpp"

#include <array>
#include <cstdint>

namespace groovebox {

class UiShell;

// Ідентифікатори вікон (views). Назви — у viewName(); порядок вкладок задається в layout.cpp
enum class ViewId : uint8_t {
    kSeq = 0,  // SEQ     — матричний секвенсор
    kSynth,    // SYNTH   — таблиця параметрів інструмента обраного треку
    kPerf,     // PERF    — жива гра (педи, октави, макроси)
    kFx,       // FX      — send-ефекти та вихідна шина
    kMix,      // MIX     — мікшер треків + MASTER
    kSample,   // SAMPLE  — станція семплювання, візуалізація хвилі та лупер
    kSys,      // SYS     — системні налаштування
    kArp,      // ARP     — параметри подвійного арпеджиратора та евклідового генератора
    kInspect,  // INSPECT — інспектор кроку (popup поверх SEQ, поки затиснута A)
    kCount
};

inline constexpr ViewId kScope = ViewId::kSample;

inline constexpr int kViewCount = static_cast<int>(ViewId::kCount);

inline constexpr const char* kViewNames[kViewCount] = {
    "SEQ", "SYNTH", "PERF", "FX", "MIX", "SAMPLE", "SYS", "ARP", "INSPECT"
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
    int seq_scroll_step = 0;// плавний горизонтальний скрол секвенсера по кроку
    bool rec_mode = false;  // Режим розгортання доріжки (Piano Roll / Drum Lanes)
    int rec_cursor_lane = 0;// 0..7 (Drum Lanes) або 0..11 (Piano Roll Semitones)
    int rec_octave = 3;     // Октава піанорола 1..6 (C1..C6)

    // Режим перформансу (жива гра на педах/інструментах)
    int perf_octave = 3;    // Октава живої гри C2..C6

    // Подвійний послідовний арпеджиратор та процедурний генератор
    Arpeggiator arpeggiator;

    // Elektron 8-банківська система патернів та менеджер проєктів
    PatternManager pattern_mgr;

    // Семплер, лупер та слайсер
    SamplerEngine sampler;
    float sample_trim_start = 0.0f; // 0.0 .. 1.0 (Digitakt Trim Start)
    float sample_trim_end = 1.0f;   // 0.0 .. 1.0 (Digitakt Trim End)
    int sample_zoom = 1;            // 1x .. 8x
    int sample_target_track = 0;    // 0..3 (призначення семплу на трек)

    // Курсор (рядок/колонка/скрол) для кожного табличного вікна
    std::array<int, kViewCount> grid_row{};
    std::array<int, kViewCount> grid_col{};
    std::array<int, kViewCount> grid_scroll{};

    // Інспектор кроку + P-Lock зі стіка (поки затиснута A)
    bool step_inspector_open = false;
    bool stick_plock_tweaked = false;
    float plock_cutoff_live = -1.0f;

    // Модальне вікно арпеджиатора та випадаюче меню вибору рушія
    bool arp_modal_open = false;
    bool engine_menu_open = false;
    int engine_menu_sel = 0;

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
