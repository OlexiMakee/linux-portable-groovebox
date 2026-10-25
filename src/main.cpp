#include "audio_engine.hpp"
#include "config.hpp"
#include "device_profile.hpp"
#include "hw_monitor.hpp"
#include "input_manager.hpp"
#include "midi_engine.hpp"
#include "ui_renderer.hpp"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace groovebox {
extern int g_stick_l_mode;
extern int g_stick_r_mode;
}

int main(int argc, char* argv[]) {
    // 0. Вимикаємо буферизацію виводу для миттєвого логування в /tmp/groovebox.log
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // 0. Завантажуємо профіль пристрою (profiles/<device>.ini)
    groovebox::loadActiveProfile();

    bool headless_smoke_test = false;
    int max_frames = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            headless_smoke_test = true;
            max_frames = 45;
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        }
    }

    groovebox::HardwareMonitor hw_monitor;
    hw_monitor.start();

    groovebox::InputManager input_mgr;
    input_mgr.init();

    if (headless_smoke_test) {
        auto snap = hw_monitor.getSnapshot();
        std::printf(
            "[XU10 SMOKE TEST] Active Audio: %s | USB Role: %s | UDC: %s | CPU: %.1f%% %dC | RAM: %d/%d MB\n",
            snap.active_audio_name.c_str(),
            snap.usb_role.c_str(),
            snap.udc_state.c_str(),
            snap.cpu_load_percent,
            snap.cpu_temp_c,
            snap.ram_used_mb,
            snap.ram_total_mb
        );
        hw_monitor.stop();
        return 0;
    }

    groovebox::UiRenderer ui;
    if (!ui.init()) {
        hw_monitor.stop();
        return 1;
    }

    auto init_hw = hw_monitor.getSnapshot();
    groovebox::AudioEngine audio_engine;
    audio_engine.start(
        init_hw.active_audio_card,
        init_hw.digitakt_connected,
        input_mgr.state().headphones_plugged
    );
    // Вітальний акорд при старті, щоб одразу було чутно роботу синта!
    audio_engine.triggerNote(0, 36, 0.85f);
    audio_engine.triggerNote(3, 60, 0.70f);

    groovebox::AppModel app;
    bool quit = false;
    bool ui_dirty = true;
    uint32_t last_render_ms = 0;
    uint32_t last_telemetry_poll = 0;
    uint32_t last_step_ms = SDL_GetTicks();
    int rendered_frames = 0;

    // Запуск MIDI-рушія з PLL-синхронізацією 24 PPQN для зовнішнього секвенсора (Digitakt II)
    groovebox::MidiEngine midi_engine;
    midi_engine.start(
        [&](int track, uint8_t note, float vel) {
            audio_engine.triggerNote(track, note, vel);
            ui_dirty = true;
        },
        [&](bool playing, bool reset_to_start) {
            app.transport_playing = playing;
            if (playing && reset_to_start) {
                app.current_step = 0;
                audio_engine.triggerSequencerStep(0);
            }
            ui_dirty = true;
        },
        [&]() {
            if (app.transport_playing) {
                app.current_step = (app.current_step + 1) % app.pattern_length;
                audio_engine.triggerSequencerStep(app.current_step);
                ui_dirty = true;
            }
        },
        [&](int channel, uint8_t cc, float norm_val) {
            auto tp = audio_engine.getTrackParams(channel);
            if (cc >= 16 && cc < 16 + groovebox::kParamsPerTrack) {
                tp.p[cc - 16] = norm_val;
                audio_engine.setTrackParams(channel, tp);
                ui_dirty = true;
            } else if (cc == 74) {
                tp.p[0] = norm_val; // Filter Cutoff
                audio_engine.setTrackParams(channel, tp);
                ui_dirty = true;
            } else if (cc == 71) {
                tp.p[1] = norm_val; // Filter Resonance / FM Index
                audio_engine.setTrackParams(channel, tp);
                ui_dirty = true;
            }
        }
    );

    while (!quit) {
        const uint32_t now_ms = SDL_GetTicks();
        input_mgr.beginFrame();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            input_mgr.processSdlEvent(ev, quit);
        }
        input_mgr.pollEvdev(quit);
        input_mgr.updateRepeat(now_ms);

        const auto& in = input_mgr.state();
        if (in.any_change) {
            ui_dirty = true;
        }

        // Керування фільтром та модуляцією з аналогових стіків у реальному часі (macro offsets):
        // Лівий стік X -> Cutoff Macro (-0.45..+0.45), Лівий стік Y -> Env Mod Macro (-0.45..+0.45)
        // Правий стік Y -> Resonance Macro (-0.45..+0.45), Правий стік X -> Timbre/FM Macro (-0.45..+0.45)
        audio_engine.params().cutoff_macro.store(std::clamp(in.lx * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().env_mod_macro.store(std::clamp(-in.ly * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().resonance_macro.store(std::clamp(-in.ry * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().fm_macro.store(std::clamp(in.rx * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);

        // Оновлення стану FILL mode для умовних тригерів Elektron (утримання L3)
        audio_engine.setFillMode(in.l3);

        // Керування гучністю кнопками VOL+ / VOL- або зміна темпу BPM через SELECT + VOL:
        if (in.btn_select && in.vol_up_pressed) {
            app.bpm = std::min(200.0f, app.bpm + 1.0f);
            ui_dirty = true;
        } else if (in.btn_select && in.vol_down_pressed) {
            app.bpm = std::max(60.0f, app.bpm - 1.0f);
            ui_dirty = true;
        } else if (in.vol_up_pressed) {
            audio_engine.adjustMasterVolume(+0.05f);
            ui_dirty = true;
        } else if (in.vol_down_pressed) {
            audio_engine.adjustMasterVolume(-0.05f);
            ui_dirty = true;
        }

        // 1. Перемикання вкладок: L1 перемикає між SEQ та SYNTH (DAW та Sound Design)
        if (in.l1_pressed) {
            if (app.active_tab == groovebox::WorkspaceTab::kSeq) {
                app.active_tab = groovebox::WorkspaceTab::kSynth;
            } else {
                app.active_tab = groovebox::WorkspaceTab::kSeq;
            }
            ui_dirty = true;
        }
        // L2 звільнено під майбутні модулі (L2 не дублює Synth!)
        if (in.r2_pressed) { app.active_tab = groovebox::WorkspaceTab::kMixFx; ui_dirty = true; }
        if (in.r1_pressed) { app.active_tab = groovebox::WorkspaceTab::kScope; ui_dirty = true; }

        // Бокова кнопка G (key.fn 0x2c0): відкриття/закриття системного меню [G] SYS!
        if (in.fn_pressed) {
            if (app.active_tab != groovebox::WorkspaceTab::kSys) {
                app.previous_tab = app.active_tab;
                app.active_tab = groovebox::WorkspaceTab::kSys;
            } else {
                app.active_tab = app.previous_tab;
            }
            ui_dirty = true;
        }

        // 2. Гарячі комбінації клавіш із затиснутим SELECT (Elektron Style):
        if (app.dev_shortcuts_enabled && in.btn_select && in.b_pressed) {
            // SELECT + B: CLEAR TRACK (повне очищення активної доріжки)
            audio_engine.clearRow(app.cursor_row);
            ui_dirty = true;
        } else if (app.dev_shortcuts_enabled && in.btn_select && in.a_pressed) {
            // SELECT + A: STEP PREVIEW (прослухати виділений крок із P-Lock)
            audio_engine.previewStep(app.cursor_row, app.cursor_step);
            ui_dirty = true;
        } else if (app.dev_shortcuts_enabled && in.btn_select && in.start_pressed) {
            // SELECT + START: RANDOMIZE TRACK (рандомізувати тригери доріжки)
            audio_engine.randomizeRow(app.cursor_row);
            ui_dirty = true;
        } else if (app.dev_shortcuts_enabled && in.btn_select && in.y_pressed) {
            // SELECT + Y: перемикання сторінки секвенсора (до 4 сторінок P1..P4 залежно від довжини рядка)
            uint32_t r_len = audio_engine.getRowLength(app.cursor_row);
            uint32_t max_pages = (r_len + groovebox::kStepsPerPage - 1) / groovebox::kStepsPerPage;
            if (max_pages > 1) {
                app.seq_page = (app.seq_page + 1) % max_pages;
                app.cursor_step = app.seq_page * groovebox::kStepsPerPage + (app.cursor_step % groovebox::kStepsPerPage);
                if (app.cursor_step >= static_cast<int>(r_len)) {
                    app.cursor_step = static_cast<int>(r_len - 1);
                }
            } else {
                app.seq_page = 0;
            }
            ui_dirty = true;
        } else if (app.dev_shortcuts_enabled && in.btn_select && in.x_pressed) {
            // SELECT + X: зміна довжини патерну поточного рядка (16 -> 24 -> 32 -> 48 -> 64 -> 72 -> 80 -> 96 -> 112 -> 128)
            uint32_t next_len = audio_engine.cycleRowLength(app.cursor_row);
            if (app.cursor_step >= static_cast<int>(next_len)) {
                app.cursor_step = static_cast<int>(next_len - 1);
            }
            uint32_t max_pages = (next_len + groovebox::kStepsPerPage - 1) / groovebox::kStepsPerPage;
            if (app.seq_page >= static_cast<int>(max_pages)) {
                app.seq_page = max_pages - 1;
            }
            ui_dirty = true;
        } else if (app.active_tab == groovebox::WorkspaceTab::kSeq && !in.btn_select) {
            // --- Step Inspector та P-Lock зі стіка: Детекція Hold A vs Tweak стіком vs короткий клік ---
            if (in.a_pressed) {
                app.a_press_time_ms = now_ms;
                app.a_held = true;
                app.a_long_press_triggered = false;
                app.stick_plock_tweaked = false;
                app.plock_cutoff_live = -1.0f;
            }
            if (app.a_held) {
                // Elektron P-Lock механіка: затиснув A і крутиш аналоговий стік -> живий твік і аудіо-прев'ю!
                if (std::fabs(in.lx) > 0.15f) {
                    app.stick_plock_tweaked = true;
                    app.plock_cutoff_live = std::clamp(0.50f + in.lx * 0.45f, 0.05f, 0.95f);
                    audio_engine.lockStepCutoff(app.cursor_row, app.cursor_step, app.plock_cutoff_live);
                    ui_dirty = true;
                }
                if (!app.a_long_press_triggered && (now_ms - app.a_press_time_ms >= 200)) {
                    app.a_long_press_triggered = true;
                    app.step_inspector_open = true;
                    ui_dirty = true;
                }
            }
            if (app.a_held && !in.btn_a) {
                if (app.stick_plock_tweaked) {
                    // Збереження положення стіка, на якому відпущено кнопку A (Elektron P-Lock Auto-Save)
                    if (app.plock_cutoff_live >= 0.0f) {
                        audio_engine.lockStepCutoff(app.cursor_row, app.cursor_step, app.plock_cutoff_live);
                    }
                    ui_dirty = true;
                } else if (!app.a_long_press_triggered) {
                    // Короткий клік (<200мс) без руху стіка: встановлення / зміна кроку (Off -> Note -> Param -> Off)
                    audio_engine.toggleStep(app.cursor_row, app.cursor_step);
                    ui_dirty = true;
                }
                app.a_held = false;
                app.step_inspector_open = false;
                app.stick_plock_tweaked = false;
                app.plock_cutoff_live = -1.0f;
                ui_dirty = true;
            }

            if (!app.step_inspector_open) {
                if (in.b_pressed) {
                    // Кнопка B: Видалити крок під курсором (Elektron Clear Step / Trig OFF)
                    audio_engine.clearStep(app.cursor_row, app.cursor_step);
                    ui_dirty = true;
                }
                if (in.x_pressed) {
                    // Кнопка X: нота -1 півтон
                    audio_engine.adjustStepNote(app.cursor_row, app.cursor_step, -1);
                    ui_dirty = true;
                }
                if (in.y_pressed) {
                    // Кнопка Y: нота +1 півтон
                    audio_engine.adjustStepNote(app.cursor_row, app.cursor_step, +1);
                    ui_dirty = true;
                }
            }
        } else if (!in.btn_select) {
            // Дії кнопок A, B, X, Y залежно від інших активних вкладок:
            if (app.active_tab == groovebox::WorkspaceTab::kSynth) {
                // Вкладка [L1] SYNTH:
                // X = ADD INSTRUMENT (додати 4 рядки)
                // Y = DEL INSTRUMENT (видалити 4 рядки)
                // A = PREVIEW SOUND (зіграти ноту активної доріжки)
                // B = RESET PARAM (скинути поточний вибраний параметр)
                if (in.x_pressed) {
                    int cur_rows = audio_engine.getActiveRowCount();
                    if (cur_rows < 16) {
                        audio_engine.setActiveRowCount(cur_rows + 4);
                        app.selected_track = cur_rows / 4;
                        ui_dirty = true;
                    }
                } else if (in.y_pressed) {
                    int cur_rows = audio_engine.getActiveRowCount();
                    if (cur_rows > 4) {
                        audio_engine.setActiveRowCount(cur_rows - 4);
                        if (app.selected_track >= (cur_rows - 4) / 4) {
                            app.selected_track = std::max(0, (cur_rows - 4) / 4 - 1);
                        }
                        ui_dirty = true;
                    }
                } else if (in.a_pressed) {
                    audio_engine.triggerNote(app.selected_track, 48, 0.90f);
                    ui_dirty = true;
                } else if (in.b_pressed) {
                    auto tp = audio_engine.getTrackParams(app.selected_track);
                    tp.p[0] = 0.5f; tp.p[1] = 0.3f; tp.p[2] = 0.5f;
                    audio_engine.setTrackParams(app.selected_track, tp);
                    ui_dirty = true;
                }
            } else if (app.active_tab == groovebox::WorkspaceTab::kMixFx) {
                auto tp = audio_engine.getTrackParams(app.selected_track);
                if (in.a_pressed) { tp.muted = !tp.muted; audio_engine.setTrackParams(app.selected_track, tp); ui_dirty = true; }
                if (in.b_pressed) { tp.delay_send = 0.0f; audio_engine.setTrackParams(app.selected_track, tp); ui_dirty = true; }
                if (in.x_pressed) { tp.delay_send = std::clamp(tp.delay_send - 0.10f, 0.0f, 1.0f); audio_engine.setTrackParams(app.selected_track, tp); ui_dirty = true; }
                if (in.y_pressed) {
                    float cur_fb = audio_engine.params().delay_feedback.load(std::memory_order_relaxed);
                    float next_fb = (cur_fb > 0.70f) ? 0.20f : (cur_fb + 0.15f);
                    audio_engine.params().delay_feedback.store(next_fb, std::memory_order_relaxed);
                    ui_dirty = true;
                }
            } else if (app.active_tab == groovebox::WorkspaceTab::kSys) {
                if (in.a_pressed) {
                    // Перемикання виділеного пункту списку системних налаштувань
                    if (app.sys_menu_cursor == 0) audio_engine.cycleBufferSize();
                    else if (app.sys_menu_cursor == 1) hw_monitor.toggleUsbRole();
                    ui_dirty = true;
                }
                if (in.b_pressed) {
                    app.active_tab = app.previous_tab;
                    ui_dirty = true;
                }
            }
        }

        // 4. Кнопка START: Play / Stop секвенсора (зупинка викликає Panic для миттєвої тиші)
        if (in.start_pressed && !in.btn_select) {
            app.transport_playing = !app.transport_playing;
            if (app.transport_playing) {
                app.current_step = 0;
                last_step_ms = now_ms;
                audio_engine.triggerSequencerStep(0);
            } else {
                audio_engine.panic();
            }
            ui_dirty = true;
        }

        // 5. Контекстна навігація на Хрестовині (D-Pad) для кожної вкладки:
        if (app.step_inspector_open) {
            // --- Керування у вікні Step Inspector (лівою рукою при затиснутій кнопці A) ---
            if (in.up_pressed)   { app.step_inspector_param = (app.step_inspector_param + 11) % 12; ui_dirty = true; }
            if (in.down_pressed) { app.step_inspector_param = (app.step_inspector_param + 1) % 12; ui_dirty = true; }

            int delta = 0;
            if (in.left_pressed) delta = in.btn_select ? -8 : -1;
            if (in.right_pressed) delta = in.btn_select ? +8 : +1;

            if (delta != 0) {
                auto st = audio_engine.getStep(app.cursor_row, app.cursor_step);
                float step_norm = (in.btn_select ? 8.0f : 1.0f) / 127.0f;
                switch (app.step_inspector_param) {
                    case 0: // TYPE: Note vs Param Trig vs Off
                        if (delta > 0) audio_engine.toggleStep(app.cursor_row, app.cursor_step);
                        break;
                    case 1: // NOTE (MIDI 0..127)
                        st.note = static_cast<uint8_t>(std::clamp(static_cast<int>(st.note) + delta, 0, 127));
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    case 2: // VELOCITY (0..127)
                        st.velocity = std::clamp(st.velocity + (delta > 0 ? step_norm : -step_norm), 0.05f, 1.0f);
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    case 3: // GATE LENGTH (10% .. 200%)
                        st.gate_len = std::clamp(st.gate_len + (delta > 0 ? 0.10f : -0.10f), 0.10f, 2.0f);
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    case 4: // SLIDE (TB-303 Constant-Time 65ms Glide: ON/OFF, вимкнено для барабанів)
                        if (audio_engine.getTrackParams(app.selected_track).engine != groovebox::EngineType::kDrumSyn) {
                            st.slide = !st.slide;
                            audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        }
                        break;
                    case 5: // ATTACK
                        st.attack = std::clamp(((st.attack >= 0.0f) ? st.attack : 0.005f) + (delta > 0 ? step_norm : -step_norm), 0.0f, 1.0f);
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    case 6: // DECAY
                        st.decay = std::clamp(((st.decay >= 0.0f) ? st.decay : 0.28f) + (delta > 0 ? step_norm : -step_norm), 0.01f, 1.0f);
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    case 7: // SUSTAIN
                        st.sustain = std::clamp(((st.sustain >= 0.0f) ? st.sustain : 0.0f) + (delta > 0 ? step_norm : -step_norm), 0.0f, 1.0f);
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    case 8: // RELEASE
                        st.release = std::clamp(((st.release >= 0.0f) ? st.release : 0.05f) + (delta > 0 ? step_norm : -step_norm), 0.01f, 1.0f);
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    case 9: // CUTOFF P-LOCK
                        st.has_plock = true;
                        st.plock_cutoff = std::clamp(((st.plock_cutoff >= 0.0f) ? st.plock_cutoff : 0.5f) + (delta > 0 ? step_norm : -step_norm), 0.05f, 0.95f);
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    case 10: { // PROBABILITY (0% .. 100%)
                        int p = static_cast<int>(st.probability) + (delta > 0 ? (in.btn_select ? 10 : 1) : (in.btn_select ? -10 : -1));
                        st.probability = static_cast<uint8_t>(std::clamp(p, 0, 100));
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    }
                    case 11: { // CONDITION (Elektron Trig Conditions)
                        int c = static_cast<int>(st.condition) + (delta > 0 ? 1 : -1) + static_cast<int>(groovebox::TrigCondition::kCount);
                        st.condition = static_cast<groovebox::TrigCondition>(c % static_cast<int>(groovebox::TrigCondition::kCount));
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
                        break;
                    }
                }
                ui_dirty = true;
            }
        } else if (app.active_tab == groovebox::WorkspaceTab::kSeq) {
            // [L1] SEQ: Up/Down = вибір активного рядка (0..7), Left/Right = навігація по кроках
            const int active_rows = audio_engine.getActiveRowCount();
            if (in.up_pressed) {
                int jump = in.btn_select ? 4 : 1;
                app.cursor_row = (app.cursor_row + active_rows - jump) % active_rows;
                app.selected_track = audio_engine.getRowTrack(app.cursor_row);
                uint32_t r_len = audio_engine.getRowLength(app.cursor_row);
                if (app.cursor_step >= static_cast<int>(r_len)) {
                    app.cursor_step = static_cast<int>(r_len - 1);
                }
                uint32_t max_p = (r_len + groovebox::kStepsPerPage - 1) / groovebox::kStepsPerPage;
                app.seq_page = app.cursor_step / groovebox::kStepsPerPage;
                if (app.seq_page >= static_cast<int>(max_p)) app.seq_page = max_p - 1;
                ui_dirty = true;
            }
            if (in.down_pressed) {
                int jump = in.btn_select ? 4 : 1;
                app.cursor_row = (app.cursor_row + jump) % active_rows;
                app.selected_track = audio_engine.getRowTrack(app.cursor_row);
                uint32_t r_len = audio_engine.getRowLength(app.cursor_row);
                if (app.cursor_step >= static_cast<int>(r_len)) {
                    app.cursor_step = static_cast<int>(r_len - 1);
                }
                uint32_t max_p = (r_len + groovebox::kStepsPerPage - 1) / groovebox::kStepsPerPage;
                app.seq_page = app.cursor_step / groovebox::kStepsPerPage;
                if (app.seq_page >= static_cast<int>(max_p)) app.seq_page = max_p - 1;
                ui_dirty = true;
            }
            uint32_t cur_r_len = audio_engine.getRowLength(app.cursor_row);
            if (in.left_pressed) {
                int jump = in.btn_select ? 16 : 1; // З SELECT: стрибок на такт (16 кроків)!
                if (app.cursor_step >= jump) app.cursor_step -= jump;
                else app.cursor_step = static_cast<int>(cur_r_len - 1);
                app.seq_page = app.cursor_step / groovebox::kStepsPerPage;
                ui_dirty = true;
            }
            if (in.right_pressed) {
                int jump = in.btn_select ? 16 : 1;
                app.cursor_step = (app.cursor_step + jump) % cur_r_len;
                app.seq_page = app.cursor_step / groovebox::kStepsPerPage;
                ui_dirty = true;
            }
        } else if (app.active_tab == groovebox::WorkspaceTab::kSynth) {
            // [L1] SYNTH: 6x4 Табличний навігатор
            // app.selected_param: 0..23 (row = selected_param / 4, col = selected_param % 4)
            int sel_row = std::clamp(app.selected_param / 4, 0, 5);
            int sel_col = std::clamp(app.selected_param % 4, 0, 3);

            if (in.up_pressed) {
                sel_row = (sel_row + 5) % 6;
                app.selected_param = sel_row * 4 + sel_col;
                ui_dirty = true;
            } else if (in.down_pressed) {
                sel_row = (sel_row + 1) % 6;
                app.selected_param = sel_row * 4 + sel_col;
                ui_dirty = true;
            }

            if (in.btn_select) {
                // При затиснутому SELECT стрілки Left/Right переміщують курсор по колонках (0..3)
                if (in.left_pressed) {
                    sel_col = (sel_col + 3) % 4;
                    app.selected_param = sel_row * 4 + sel_col;
                    ui_dirty = true;
                } else if (in.right_pressed) {
                    sel_col = (sel_col + 1) % 4;
                    app.selected_param = sel_row * 4 + sel_col;
                    ui_dirty = true;
                }
            } else if (in.left_pressed || in.right_pressed) {
                // Без SELECT стрілки Left/Right змінюють значення комірки під курсором!
                const int dir = in.right_pressed ? +1 : -1;
                const float delta = dir * 0.05f;

                if (sel_row == 0) {
                    // Category 0: INST (ENGINE, TRK, ADD, DEL)
                    if (sel_col == 0) {
                        audio_engine.cycleTrackEngine(app.selected_track, dir);
                    } else if (sel_col == 1) {
                        int trk_count = (audio_engine.getActiveRowCount() + 3) / 4;
                        app.selected_track = (app.selected_track + dir + trk_count) % trk_count;
                    } else if (sel_col == 2) {
                        // ADD
                        int cur_rows = audio_engine.getActiveRowCount();
                        if (cur_rows < 16) {
                            audio_engine.setActiveRowCount(cur_rows + 4);
                            app.selected_track = cur_rows / 4;
                        }
                    } else if (sel_col == 3) {
                        // DEL
                        int cur_rows = audio_engine.getActiveRowCount();
                        if (cur_rows > 4) {
                            audio_engine.setActiveRowCount(cur_rows - 4);
                            if (app.selected_track >= (cur_rows - 4) / 4) {
                                app.selected_track = std::max(0, (cur_rows - 4) / 4 - 1);
                            }
                        }
                    }
                } else if (sel_row == 1) {
                    // Category 1: FILTER (CUTOFF, RESO, ENV MOD, TYPE)
                    if (sel_col < 3) {
                        audio_engine.adjustTrackParam(app.selected_track, sel_col, delta);
                    }
                } else if (sel_row == 2) {
                    // Category 2: OSC / FM (P2, P3, P4, P5)
                    audio_engine.adjustTrackParam(app.selected_track, sel_col + 2, delta);
                } else if (sel_row == 3) {
                    // Category 3: ADSR
                    auto tp = audio_engine.getTrackParams(app.selected_track);
                    if (sel_col == 0) tp.attack = std::clamp(tp.attack + dir * 0.02f, 0.001f, 0.8f);
                    else if (sel_col == 1) tp.decay = std::clamp(tp.decay + dir * 0.05f, 0.01f, 1.5f);
                    else if (sel_col == 2) tp.sustain = std::clamp(tp.sustain + delta, 0.0f, 1.0f);
                    else if (sel_col == 3) tp.release = std::clamp(tp.release + dir * 0.05f, 0.01f, 1.8f);
                    audio_engine.setTrackParams(app.selected_track, tp);
                } else if (sel_row == 4) {
                    // Category 4: MOD / LFO (пресети модуляцій)
                } else if (sel_row == 5) {
                    // Category 5: SENDS & STICKS
                    auto tp = audio_engine.getTrackParams(app.selected_track);
                    if (sel_col == 0) {
                        tp.delay_send = std::clamp(tp.delay_send + delta, 0.0f, 1.0f);
                        audio_engine.setTrackParams(app.selected_track, tp);
                    } else if (sel_col == 1) {
                        tp.pan = std::clamp(tp.pan + dir * 0.1f, -1.0f, 1.0f);
                        audio_engine.setTrackParams(app.selected_track, tp);
                    } else if (sel_col == 2) {
                        groovebox::g_stick_l_mode = (groovebox::g_stick_l_mode + dir + 4) % 4;
                    } else if (sel_col == 3) {
                        groovebox::g_stick_r_mode = (groovebox::g_stick_r_mode + dir + 4) % 4;
                    }
                }
                ui_dirty = true;
            }
        } else if (app.active_tab == groovebox::WorkspaceTab::kMixFx) {
            // R2 MIX/FX: Up/Down = вибір доріжки, Left/Right = гучність доріжки
            if (in.up_pressed)    { app.selected_track = (app.selected_track + 3) % 4; ui_dirty = true; }
            if (in.down_pressed)  { app.selected_track = (app.selected_track + 1) % 4; ui_dirty = true; }
            float delta = in.btn_select ? 0.15f : 0.05f;
            if (in.left_pressed) {
                auto tp = audio_engine.getTrackParams(app.selected_track);
                tp.level = std::clamp(tp.level - delta, 0.0f, 1.0f);
                audio_engine.setTrackParams(app.selected_track, tp);
                ui_dirty = true;
            }
            if (in.right_pressed) {
                auto tp = audio_engine.getTrackParams(app.selected_track);
                tp.level = std::clamp(tp.level + delta, 0.0f, 1.0f);
                audio_engine.setTrackParams(app.selected_track, tp);
                ui_dirty = true;
            }
        } else if (app.active_tab == groovebox::WorkspaceTab::kSys) {
            // [G] SYS: Elektron Settings вертикальний список
            if (in.up_pressed)   { app.sys_menu_cursor = (app.sys_menu_cursor + 4) % 5; ui_dirty = true; }
            if (in.down_pressed) { app.sys_menu_cursor = (app.sys_menu_cursor + 1) % 5; ui_dirty = true; }
            if (in.left_pressed || in.right_pressed || in.a_pressed) {
                if (app.sys_menu_cursor == 0) {
                    audio_engine.cycleBufferSize();
                    ui_dirty = true;
                } else if (app.sys_menu_cursor == 1) {
                    hw_monitor.toggleUsbRole();
                    ui_dirty = true;
                } else if (app.sys_menu_cursor == 2) {
                    audio_engine.panic();
                    ui_dirty = true;
                }
            }
        }

        auto midi_tel = midi_engine.getTelemetry();
        if (midi_tel.external_sync) {
            app.bpm = midi_tel.detected_bpm;
        } else {
            // Внутрішній генератор 64/128-крокового секвенсора (коли немає зовнішнього MIDI Clock)
            const uint32_t step_interval_ms = static_cast<uint32_t>(60000.0f / (app.bpm * 4.0f));
            if (app.transport_playing && (now_ms - last_step_ms >= step_interval_ms)) {
                last_step_ms = now_ms;
                app.current_step = (app.current_step + 1) % app.pattern_length;
                audio_engine.triggerSequencerStep(app.current_step);
                ui_dirty = true;
            }
        }

        auto hw = hw_monitor.getSnapshot();
        if (hw.poll_counter != last_telemetry_poll) {
            last_telemetry_poll = hw.poll_counter;
            audio_engine.updateRouting(hw.active_audio_card, hw.digitakt_connected, in.headphones_plugged);
            ui_dirty = true;
        } else {
            // Миттєва реакція на підключення/відключення навушників
            audio_engine.updateRouting(hw.active_audio_card, hw.digitakt_connected, in.headphones_plugged);
        }

        auto audio_tel = audio_engine.getTelemetry();
        const bool sound_active = (audio_tel.vu_left > 0.01f || audio_tel.vu_right > 0.01f);

        // Адаптивний FPS:
        // - Коли грає звук або рухаються стіки -> 24 FPS (41 мс) для плавного осцилографа і VU-метра
        // - У стані спокою -> тільки по dirty-прапорцю (0 зайвих кадрів!)
        const bool scope_active = app.transport_playing || sound_active ||
                                  (std::fabs(in.lx) > 0.02f || std::fabs(in.ly) > 0.02f ||
                                   std::fabs(in.rx) > 0.02f || std::fabs(in.ry) > 0.02f);

        const uint32_t min_frame_interval_ms = scope_active
                                                   ? (1000 / groovebox::kTargetFpsScope)
                                                   : (1000 / groovebox::kTargetFpsUi);

        if (scope_active && (now_ms - last_render_ms >= min_frame_interval_ms)) {
            ui_dirty = true;
        }

        if (ui_dirty && (now_ms - last_render_ms >= min_frame_interval_ms)) {
            ui.render(app, hw, in, audio_engine, audio_tel, midi_tel, audio_engine.params().master_volume.load());
            last_render_ms = now_ms;
            ui_dirty = false;
            ++rendered_frames;
            if (max_frames > 0 && rendered_frames >= max_frames) {
                quit = true;
            }
        } else {
            SDL_Delay(4);
        }
    }

    midi_engine.stop();
    audio_engine.stop();
    ui.shutdown();
    input_mgr.shutdown();
    hw_monitor.stop();
    return 0;
}
