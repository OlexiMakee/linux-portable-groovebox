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

        // Керування гучністю кнопками VOL+ / VOL-
        if (in.vol_up_pressed) {
            audio_engine.adjustMasterVolume(+0.05f);
            ui_dirty = true;
        }
        if (in.vol_down_pressed) {
            audio_engine.adjustMasterVolume(-0.05f);
            ui_dirty = true;
        }

        // 1. Перемикання 4 верхніх вкладок за бамперами L1, L2, R2, R1
        if (in.l1_pressed) { app.active_tab = groovebox::WorkspaceTab::kSeq; ui_dirty = true; }
        if (in.l2_pressed) { app.active_tab = groovebox::WorkspaceTab::kSynth; ui_dirty = true; }
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

        // 2. Гарячі комбінації клавіш із затиснутим SELECT:
        if (app.dev_shortcuts_enabled && in.btn_select && in.b_pressed) {
            // SELECT + B: PANIC / KILL SOUND (миттєве скидання всіх 16 голосів та delay)
            audio_engine.panic();
            ui_dirty = true;
        } else if (app.dev_shortcuts_enabled && in.btn_select && in.start_pressed) {
            // SELECT + START: STOP + REWIND + PANIC
            app.transport_playing = false;
            app.current_step = 0;
            audio_engine.panic();
            ui_dirty = true;
        } else if (app.dev_shortcuts_enabled && in.btn_select && in.a_pressed) {
            // SELECT + A: циклічне перемикання розміру буфера (96 -> 128 -> 256 -> 512 -> 1024 -> 2048)
            audio_engine.cycleBufferSize();
            ui_dirty = true;
        } else if (app.dev_shortcuts_enabled && in.btn_select && in.y_pressed) {
            // SELECT + Y: перемикання сторінки секвенсора (Page 1 <-> Page 2 для 128 кроків)
            app.seq_page = (app.seq_page == 0) ? 1 : 0;
            app.cursor_step = app.seq_page * groovebox::kStepsPerPage + (app.cursor_step % groovebox::kStepsPerPage);
            ui_dirty = true;
        } else if (app.dev_shortcuts_enabled && in.btn_select && in.x_pressed) {
            // SELECT + X: зміна активної довжини патерну (16 -> 32 -> 64 -> 128)
            uint32_t cur_len = app.pattern_length;
            uint32_t next_len = (cur_len == 16) ? 32 : ((cur_len == 32) ? 64 : ((cur_len == 64) ? 128 : 16));
            app.pattern_length = next_len;
            audio_engine.setPatternLength(next_len);
            ui_dirty = true;
        } else if (app.active_tab == groovebox::WorkspaceTab::kSeq && !in.btn_select) {
            // --- Step Inspector: Детекція Hold A (>200мс) vs короткий клік (<200мс) ---
            if (in.a_pressed) {
                app.a_press_time_ms = now_ms;
                app.a_held = true;
                app.a_long_press_triggered = false;
            }
            if (app.a_held && !app.a_long_press_triggered) {
                if (now_ms - app.a_press_time_ms >= 200) {
                    app.a_long_press_triggered = true;
                    app.step_inspector_open = true;
                    ui_dirty = true;
                }
            }
            if (app.a_held && !in.btn_a) {
                if (!app.a_long_press_triggered) {
                    // Короткий клік (<200мс): встановлення / зміна кроку (Off -> Note -> Param -> Off)
                    audio_engine.toggleStep(app.cursor_row, app.cursor_step);
                    ui_dirty = true;
                }
                app.a_held = false;
                app.step_inspector_open = false;
                ui_dirty = true;
            }

            if (!app.step_inspector_open) {
                if (in.b_pressed) {
                    // Кнопка B: Parameter Lock для фільтра
                    float lock_val = std::clamp(0.50f + in.lx * 0.45f, 0.05f, 0.95f);
                    audio_engine.lockStepCutoff(app.cursor_row, app.cursor_step, lock_val);
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
            if (app.active_tab == groovebox::WorkspaceTab::kMixFx) {
                auto tp = audio_engine.getTrackParams(app.selected_track);
                if (in.a_pressed) { tp.muted = !tp.muted; audio_engine.setTrackParams(app.selected_track, tp); ui_dirty = true; }
                if (in.b_pressed) { tp.delay_send = std::clamp(tp.delay_send + 0.10f, 0.0f, 1.0f); audio_engine.setTrackParams(app.selected_track, tp); ui_dirty = true; }
                if (in.x_pressed) { tp.delay_send = std::clamp(tp.delay_send - 0.10f, 0.0f, 1.0f); audio_engine.setTrackParams(app.selected_track, tp); ui_dirty = true; }
                if (in.y_pressed) {
                    float cur_fb = audio_engine.params().delay_feedback.load(std::memory_order_relaxed);
                    float next_fb = (cur_fb > 0.70f) ? 0.20f : (cur_fb + 0.15f);
                    audio_engine.params().delay_feedback.store(next_fb, std::memory_order_relaxed);
                    ui_dirty = true;
                }
            } else if (app.active_tab == groovebox::WorkspaceTab::kSys) {
                if (in.a_pressed) {
                    // Перемикання USB роутингу HOST <-> DEVICE
                    hw_monitor.toggleUsbRole();
                    ui_dirty = true;
                }
                if (in.b_pressed) {
                    audio_engine.panic();
                    ui_dirty = true;
                }
            } else {
                // Live Pad Triggers на вкладці SYNTH (вибирають доріжку і грають ноту наживо!)
                if (in.a_pressed) { app.selected_track = 0; audio_engine.triggerNote(0, 36, 0.95f); ui_dirty = true; }
                if (in.b_pressed) { app.selected_track = 1; audio_engine.triggerNote(1, 60, 0.90f); ui_dirty = true; }
                if (in.x_pressed) { app.selected_track = 2; audio_engine.triggerNote(2, 48, 0.95f); ui_dirty = true; }
                if (in.y_pressed) { app.selected_track = 3; audio_engine.triggerNote(3, 63, 0.85f); ui_dirty = true; }
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
            if (in.up_pressed)   { app.step_inspector_param = (app.step_inspector_param + 9) % 10; ui_dirty = true; }
            if (in.down_pressed) { app.step_inspector_param = (app.step_inspector_param + 1) % 10; ui_dirty = true; }

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
                    case 4: // SLIDE (TB-303 Constant-Time 65ms Glide: ON/OFF)
                        st.slide = !st.slide;
                        audio_engine.setStep(app.cursor_row, app.cursor_step, st);
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
                }
                ui_dirty = true;
            }
        } else if (app.active_tab == groovebox::WorkspaceTab::kSeq) {
            // [L1] SEQ: Up/Down = вибір рядка 1..16, Left/Right = навігація по кроках 1..64/128
            if (in.up_pressed) {
                int jump = in.btn_select ? 4 : 1; // З SELECT: стрибок на цілий трек (4 рядки)!
                app.cursor_row = (app.cursor_row + groovebox::kTotalRows - jump) % groovebox::kTotalRows;
                app.selected_track = app.cursor_row / groovebox::kVoicesPerTrack;
                ui_dirty = true;
            }
            if (in.down_pressed) {
                int jump = in.btn_select ? 4 : 1;
                app.cursor_row = (app.cursor_row + jump) % groovebox::kTotalRows;
                app.selected_track = app.cursor_row / groovebox::kVoicesPerTrack;
                ui_dirty = true;
            }
            if (in.left_pressed) {
                int jump = in.btn_select ? 16 : 1; // З SELECT: стрибок на такт (16 кроків)!
                if (app.cursor_step >= jump) app.cursor_step -= jump;
                else app.cursor_step = static_cast<int>(app.pattern_length - 1);
                app.seq_page = (app.cursor_step >= static_cast<int>(groovebox::kStepsPerPage)) ? 1 : 0;
                ui_dirty = true;
            }
            if (in.right_pressed) {
                int jump = in.btn_select ? 16 : 1;
                app.cursor_step = (app.cursor_step + jump) % app.pattern_length;
                app.seq_page = (app.cursor_step >= static_cast<int>(groovebox::kStepsPerPage)) ? 1 : 0;
                ui_dirty = true;
            }
        } else if (app.active_tab == groovebox::WorkspaceTab::kSynth) {
            // L2 SYNTH: Row 0 = вибір Engine Type, Rows 1..6 = 6 параметрів поточного движка
            if (in.up_pressed)   { app.selected_param = (app.selected_param + 6) % 7; ui_dirty = true; }
            if (in.down_pressed) { app.selected_param = (app.selected_param + 1) % 7; ui_dirty = true; }
            float delta = in.btn_select ? 0.15f : 0.05f;
            if (in.left_pressed) {
                if (app.selected_param == 0) audio_engine.cycleTrackEngine(app.selected_track, -1);
                else audio_engine.adjustTrackParam(app.selected_track, app.selected_param - 1, -delta);
                ui_dirty = true;
            }
            if (in.right_pressed) {
                if (app.selected_param == 0) audio_engine.cycleTrackEngine(app.selected_track, +1);
                else audio_engine.adjustTrackParam(app.selected_track, app.selected_param - 1, +delta);
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
            // [G] SYS: Up/Down = вибір доріжки, Left/Right = зміна BPM
            if (in.up_pressed)    { app.selected_track = (app.selected_track + 3) % 4; ui_dirty = true; }
            if (in.down_pressed)  { app.selected_track = (app.selected_track + 1) % 4; ui_dirty = true; }
            float bpm_step = in.btn_select ? 8.0f : 1.0f;
            if (in.left_pressed)  { app.bpm = std::max(60.0f, app.bpm - bpm_step); ui_dirty = true; }
            if (in.right_pressed) { app.bpm = std::min(200.0f, app.bpm + bpm_step); ui_dirty = true; }
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
