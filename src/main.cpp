#include "audio_engine.hpp"
#include "config.hpp"
#include "device_profile.hpp"
#include "hw_monitor.hpp"
#include "input_manager.hpp"
#include "midi_engine.hpp"
#include "ui/ui_shell.hpp"

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

    groovebox::UiShell ui;
    groovebox::setupAppLayout(ui);

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
    audio_engine.setSampler(&app.sampler);
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
                tp.p[0] = norm_val;
                audio_engine.setTrackParams(channel, tp);
                ui_dirty = true;
            } else if (cc == 71) {
                tp.p[1] = norm_val;
                audio_engine.setTrackParams(channel, tp);
                ui_dirty = true;
            }
        },
        [&](int track, int note) {
            audio_engine.noteOff(track, note);
            ui_dirty = true;
        },
        [&](int, int) {
            // MIDI Program Change 0..127 (квантоване перемикання патернів Elektron A01..H16)
            ui_dirty = true;
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
        audio_engine.params().cutoff_macro.store(std::clamp(in.lx * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().env_mod_macro.store(std::clamp(-in.ly * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().resonance_macro.store(std::clamp(-in.ry * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().fm_macro.store(std::clamp(in.rx * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().stick_lx.store(std::clamp(in.lx * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().stick_ly.store(std::clamp(-in.ly * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().stick_rx.store(std::clamp(in.rx * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);
        audio_engine.params().stick_ry.store(std::clamp(-in.ry * 0.45f, -0.45f, 0.45f), std::memory_order_relaxed);

        // Оновлення стану FILL mode для умовних тригерів Elektron (утримання L3)
        audio_engine.setFillMode(in.l3);

        auto hw = hw_monitor.getSnapshot();
        auto audio_tel = audio_engine.getTelemetry();
        auto midi_tel = midi_engine.getTelemetry();

        // 1. Диспетчеризація вводу через InputRouter / Keymap (SOLID: нуль if-else каскадів!)
        groovebox::UiContext ctx{
            app,
            audio_engine,
            &hw_monitor,
            in,
            hw,
            audio_tel,
            midi_tel,
            now_ms,
            &ui
        };
        ui.handleInput(ctx);

        // 2. Секвенсорний таймінг
        if (midi_tel.external_sync) {
            app.bpm = midi_tel.detected_bpm;
        } else {
            const uint32_t step_interval_ms = static_cast<uint32_t>(60000.0f / (app.bpm * 4.0f));
            if (app.transport_playing && (now_ms - last_step_ms >= step_interval_ms)) {
                last_step_ms = now_ms;
                app.current_step = (app.current_step + 1) % app.pattern_length;
                audio_engine.triggerSequencerStep(app.current_step);
                ui_dirty = true;
            }
        }

        // 3. Телеметрія та роутинг
        if (hw.poll_counter != last_telemetry_poll) {
            last_telemetry_poll = hw.poll_counter;
            audio_engine.updateRouting(hw.active_audio_card, hw.digitakt_connected, in.headphones_plugged);
            ui_dirty = true;
        } else {
            audio_engine.updateRouting(hw.active_audio_card, hw.digitakt_connected, in.headphones_plugged);
        }

        const bool sound_active = (audio_tel.vu_left > 0.01f || audio_tel.vu_right > 0.01f);
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
            ui.render(app, audio_engine, &hw_monitor, in, hw, audio_tel, midi_tel, now_ms);
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
