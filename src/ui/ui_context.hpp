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
    kSeq = 0,       // SEQ     — матричний секвенсор
    kSynth,         // SYNTH   — таблиця параметрів інструмента обраного треку
    kPerf,          // PERF    — жива гра (педи, октави, макроси)
    kFx,            // FX      — send-ефекти та вихідна шина
    kMix,           // MIX     — мікшер треків + MASTER
    kSample,        // SAMPLE  — станція семплювання, візуалізація хвилі та лупер
    kSys,           // SYS     — системні налаштування
    kArp,           // ARP     — параметри подвійного арпеджиратора
    kInspect,       // INSPECT — інспектор кроку
    kProjectModal,  // PROJECT — модальне вікно Save As / Load Project
    kSampleBrowser, // BROWSER — пул семплів та файлова система
    kEngineMenu,    // ENGINE  — меню вибору типу звукового рушія
    kModPicker,     // MOD TARGET — контекстне меню вибору таргету модуляції
    kCount
};

inline constexpr ViewId kScope = ViewId::kSample;

inline constexpr int kViewCount = static_cast<int>(ViewId::kCount);

inline constexpr const char* kViewNames[kViewCount] = {
    "SEQ", "SYNTH", "PERF", "FX", "MIX", "SAMPLE", "SYS", "ARP", "INSPECT", "PROJECT", "BROWSER", "ENGINE", "MOD_TGT"
};

enum class SeqSubmode : uint8_t {
    kMatrix = 0,    // 1. Matrix Sequencer (16-track matrix overview: son<SEQ>rol)
    kPianoRoll,     // 2. Piano Roll / Note Editor (seq<ROL>son)
    kSong           // 3. Song Mode / Pattern Arranger (rol<SON>seq)
};

enum class SynthSubmode : uint8_t {
    kSynth = 0,     // 1. Synth Sound Engine Parameters (fx<SYN>mod)
    kMod,           // 2. Modulation Matrix: 4 LFOs, 2 ADSRs, Sticks (syn<MOD>fx)
    kFx             // 3. Per-Track FX Chain (mod<FX>syn)
};

enum class MixSubmode : uint8_t {
    kMix = 0,       // 1. 16-channel Mixer & Bus (mstr<MIX>snd)
    kSend,          // 2. Send FX Tracks: Delay, Reverb, Chorus (mix<SND>mstr)
    kMaster         // 3. Mastering Bus & EQ (snd<MSTR>mix)
};

struct SongStep {
    uint8_t bank = 0;       // 0..7 (A..H)
    uint8_t pattern = 0;    // 0..15 (01..16)
    uint8_t repeats = 1;    // 1..16 (x1..x16)
    uint16_t length = 0;    // 0 = PAT (native pattern length), 1..256 steps override
    uint16_t bpm = 0;       // 0 = PAT (project BPM), 30..300 BPM override
    uint16_t mute_mask = 0; // Bitmask: bit t set = Track t muted on this row
    bool enabled = true;
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
    SeqSubmode seq_submode = SeqSubmode::kMatrix; // Цикл L1: SEQ -> Piano Roll (ROL) -> Song Mode (SON)
    SynthSubmode synth_submode = SynthSubmode::kSynth; // Цикл L2: SYN -> MOD -> FX
    MixSubmode mix_submode = MixSubmode::kMix; // Цикл R2: MIX -> SND -> MSTR
    int song_rows_count = 1; // За замовчуванням створено 1 рядок патерна
    int song_cursor_row = 0;
    int song_col = 0; // 0 = PAT, 1 = REP, 2 = LEN, 3 = BPM, 4 = MUTES, 5 = STATUS
    int song_mute_cursor_track = 0; // 0..15 для вибору треку в колонці MUTES
    int song_play_step = 0;
    int song_play_repeat = 0;
    std::array<SongStep, 16> song_chain = []() {
        std::array<SongStep, 16> sc{};
        sc[0].bank = 0;
        sc[0].pattern = 0;
        sc[0].repeats = 1;
        sc[0].enabled = true;
        for (int i = 1; i < 16; ++i) {
            sc[i].bank = 0;
            sc[i].pattern = i;
            sc[i].repeats = 1;
            sc[i].enabled = false;
        }
        return sc;
    }();
    int rec_cursor_lane = 0;// 0..7 (Drum Lanes) або 0..11 (Piano Roll Semitones)
    int rec_octave = 3;     // Октава піанорола 1..8 (C1..C8)
    uint32_t voice_limit_alert_until_ms = 0; // Показ червоного попередження "VOICE LIMIT: MAX 4"

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
    bool engine_menu_is_add = false;
    int engine_menu_sel = 0;

    // Семпл-браузер (доступ до файлової системи)
    bool sample_browser_open = false;
    int sample_browser_target_track = 0;
    std::string sample_browser_dir;
    std::vector<std::string> sample_browser_items;
    int sample_browser_sel = 0;
    uint32_t modal_cooldown_until_ms = 0; // кулдаун після закриття модалок проти випадкового витоку кнопок A/B

    // Короткий статус останньої дії (для HUD)
    std::string last_action = "READY";

    // Буфер обміну тригера (STEP.COPY / STEP.PASTE)
    bool has_clipboard_step = false;
    SequencerStep clipboard_step{};

    // Elektron Digitakt Temp Save / Recall (SELECT + G / SELECT + START)
    bool has_temp_snapshot = false;
    PatternData temp_snapshot{};
    float temp_snapshot_bpm = 120.0f;

    // Модальне вікно управління проєктами (Save As / Load Project)
    bool project_modal_open = false;
    bool project_modal_is_save = false; // true = Save As, false = Load
    std::vector<std::string> project_files;
    int project_modal_sel = 0;

    // Контекстне меню вибору таргету модуляції (Button A на DEST / STK)
    bool mod_picker_open = false;
    int mod_picker_sel = 0;
    std::string mod_picker_param_id;
};

inline bool isModalView(ViewId v) {
    return v == ViewId::kProjectModal || v == ViewId::kSampleBrowser ||
           v == ViewId::kEngineMenu || v == ViewId::kInspect || v == ViewId::kArp ||
           v == ViewId::kModPicker;
}

inline ViewId activeViewId(const AppModel& app) {
    if (app.mod_picker_open) return ViewId::kModPicker;
    if (app.project_modal_open) return ViewId::kProjectModal;
    if (app.sample_browser_open) return ViewId::kSampleBrowser;
    if (app.engine_menu_open) return ViewId::kEngineMenu;
    if (app.step_inspector_open) return ViewId::kInspect;
    if (app.arp_modal_open) return ViewId::kArp;
    return app.active_view;
}

inline void closeAllModals(AppModel& app) {
    app.mod_picker_open = false;
    app.project_modal_open = false;
    app.sample_browser_open = false;
    app.engine_menu_open = false;
    app.step_inspector_open = false;
    app.arp_modal_open = false;
}

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
