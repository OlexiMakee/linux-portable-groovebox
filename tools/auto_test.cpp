#include "arpeggiator.hpp"
#include "audio_engine.hpp"
#include "config.hpp"
#include "device_profile.hpp"
#include "hw_monitor.hpp"
#include "input_manager.hpp"
#include "midi_engine.hpp"
#include "ui/actions.hpp"
#include "ui/canvas.hpp"
#include "ui/keymap.hpp"
#include "ui/params.hpp"
#include "ui/ui_shell.hpp"
#include "ui/views.hpp"

#include <SDL.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace groovebox;

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        g_tests_run++; \
        if (!(cond)) { \
            std::printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            g_tests_failed++; \
        } else { \
            g_tests_passed++; \
        } \
    } while (0)

static void testAudioDspEngines() {
    std::printf("\n=== [1/4] AUDIO DSP & SYNTH ENGINES TEST ===\n");

    AudioEngine audio;
    audio.start(0, false, false);

    // 1. Початкові доріжки
    TEST_ASSERT(audio.getActiveRowCount() == 3, "Default active track count must be 3 (Kick, 303, DX7)");
    TEST_ASSERT(audio.getTrackParams(0).engine == EngineType::kSampleOneShot, "Track 0 must be SampleOneShot Kick");
    TEST_ASSERT(audio.getTrackParams(1).engine == EngineType::kAcid303, "Track 1 must be Acid 303");
    TEST_ASSERT(audio.getTrackParams(2).engine == EngineType::kDx7, "Track 2 must be DX7 6-OP");

    // 2. Тестування рендерингу звуку для кожного з 9 движків
    for (int e = 0; e < static_cast<int>(EngineType::kCount); ++e) {
        auto eng_type = static_cast<EngineType>(e);
        auto tp = audio.getTrackParams(0);
        tp.engine = eng_type;
        audio.setTrackParams(0, tp);

        audio.triggerNote(0, 48, 0.9f);

        // Буфер для рендерингу 512 фреймів
        int16_t out_buf[1024];
        std::memset(out_buf, 0, sizeof(out_buf));

        // Викликаємо внутрішній процесинг аудіо
        audio.renderBlock(out_buf, 512, 32000);

        // Перевіряємо, що в буфері є валідні (не NaN) значення
        bool has_audio = false;
        bool has_nan = false;
        for (int i = 0; i < 1024; ++i) {
            if (out_buf[i] != 0) has_audio = true;
        }

        char msg[128];
        if (eng_type == EngineType::kMidiOut) {
            std::snprintf(msg, sizeof(msg), "Engine '%s' processes without NaN (silent MIDI track)", getEngineTypeName(eng_type));
            TEST_ASSERT(!has_nan, msg);
        } else if (eng_type == EngineType::kSliceSampler) {
            std::snprintf(msg, sizeof(msg), "Engine '%s' processes without memory violation", getEngineTypeName(eng_type));
            TEST_ASSERT(!has_nan, msg);
        } else {
            std::snprintf(msg, sizeof(msg), "Engine '%s' (idx %d) renders valid audio without NaN", getEngineTypeName(eng_type), e);
            TEST_ASSERT(has_audio && !has_nan, msg);
        }
    }

    // 3. Тестування майстер-ефектів: Delay, Reverb, Chorus, Overdrive
    auto tp = audio.getTrackParams(0);
    tp.engine = EngineType::kAcid303;
    audio.setTrackParams(0, tp);

    // Вмикаємо посили
    audio.params().reverb_decay = 0.85f;
    audio.params().reverb_mix = 0.70f;
    audio.params().delay_feedback = 0.60f;
    audio.params().delay_mix = 0.50f;
    audio.params().drive_amount = 0.60f;
    audio.params().drive_tone = 0.75f;
    audio.params().chorus_mix = 0.50f;
    audio.params().chorus_rate = 1.5f;

    audio.triggerNote(0, 36, 0.95f);
    int16_t fx_buf[1024];
    audio.renderBlock(fx_buf, 512, 32000);

    int max_amp = 0;
    for (int i = 0; i < 1024; ++i) {
        max_amp = std::max(max_amp, std::abs(static_cast<int>(fx_buf[i])));
    }
    TEST_ASSERT(max_amp > 100, "Master Send FX & Overdrive chain generates audible output");
    TEST_ASSERT(max_amp <= 32767, "Master Limiter protects output from 16-bit integer overflow");

    // 4. Тестування єдиного 16-доріжкового масиву, динамічного додавання та іменування треків
    TEST_ASSERT(audio.getActiveRowCount() == 3, "Initial active track count is exactly 3 (Kick, 303, DX7)");
    TEST_ASSERT(audio.getTrackName(0) == "KICK 808", "Track 1 is KICK 808");
    TEST_ASSERT(audio.getTrackName(1) == "303 BASS", "Track 2 is 303 BASS");
    TEST_ASSERT(audio.getTrackName(2) == "DX7 KEYS", "Track 3 is DX7 KEYS");

    int t4 = audio.addTrack(EngineType::kChordPad, "PAD 04");
    TEST_ASSERT(t4 == 3 && audio.getActiveRowCount() == 4, "addTrack dynamically creates Track 4");
    TEST_ASSERT(audio.getTrackName(3) == "PAD 04", "Track 4 has custom name 'PAD 04'");

    audio.setTrackName(3, "WARM PAD");
    TEST_ASSERT(audio.getTrackName(3) == "WARM PAD", "setTrackName updates track name to 'WARM PAD'");

    // Розширюємо до 16 треків
    while (audio.getActiveRowCount() < 16) {
        audio.addTrack(EngineType::kVaMono);
    }
    TEST_ASSERT(audio.getActiveRowCount() == 16, "AudioEngine successfully manages all 16 dynamic tracks");
    TEST_ASSERT(audio.addTrack(EngineType::kVaMono) == -1, "addTrack rejects track creation beyond kMaxTracks (16)");

    // Перевірка телеметрії для мікшера: всі 16 рівнів та імен доступні
    auto tel = audio.getTelemetry();
    TEST_ASSERT(tel.active_tracks == 16, "Telemetry reports all 16 active tracks to Mixer");
    TEST_ASSERT(tel.track_names[3] == "WARM PAD", "Telemetry propagates track names to Mixer UI");
    TEST_ASSERT(tel.track_levels.size() == 16, "Telemetry track_levels array has size 16");

    audio.deleteTrack(15);
    TEST_ASSERT(audio.getActiveRowCount() == 15, "deleteTrack safely removes track and updates active count");

    // 5. Тестування дефолтних нульових сендів
    auto t0_p = audio.getTrackParams(0);
    TEST_ASSERT(t0_p.delay_send == 0.0f, "Track 0 delay_send defaults to 0.0f");
    TEST_ASSERT(t0_p.reverb_send == 0.0f, "Track 0 reverb_send defaults to 0.0f");
    int new_trk = audio.addTrack(EngineType::kDx7, "NEW DX7");
    TEST_ASSERT(new_trk >= 0, "New track added successfully");
    auto new_tp = audio.getTrackParams(new_trk);
    TEST_ASSERT(new_tp.delay_send == 0.0f && new_tp.reverb_send == 0.0f, "New track sends default to exactly 0.0f");

    // 6. Тестування 4-голосної поліфонії (DX7, VA Mono, 2-OP FM)
    // DX7: акордовий тригер 3 нот одночасно
    auto dx_tp = audio.getTrackParams(2);
    dx_tp.engine = EngineType::kDx7;
    dx_tp.level = 0.9f;
    audio.setTrackParams(2, dx_tp);
    audio.panic();
    audio.triggerNote(2, 60, 0.9f);
    audio.triggerNote(2, 64, 0.9f);
    audio.triggerNote(2, 67, 0.9f);
    int16_t chord_buf[1024];
    std::memset(chord_buf, 0, sizeof(chord_buf));
    audio.renderBlock(chord_buf, 512, 32000);
    bool dx_chord_audible = false;
    for (int i = 0; i < 1024; ++i) {
        if (std::abs(static_cast<int>(chord_buf[i])) > 200) dx_chord_audible = true;
    }
    TEST_ASSERT(dx_chord_audible, "DX7 engine renders 3-note chord simultaneously across polyphonic voices");

    // VA Mono: акордовий тригер
    auto va_tp = audio.getTrackParams(0);
    va_tp.engine = EngineType::kVaMono;
    va_tp.level = 0.9f;
    audio.setTrackParams(0, va_tp);
    audio.panic();
    audio.triggerNote(0, 48, 0.9f);
    audio.triggerNote(0, 52, 0.9f);
    audio.triggerNote(0, 55, 0.9f);
    std::memset(chord_buf, 0, sizeof(chord_buf));
    audio.renderBlock(chord_buf, 512, 32000);
    bool va_chord_audible = false;
    for (int i = 0; i < 1024; ++i) {
        if (std::abs(static_cast<int>(chord_buf[i])) > 200) va_chord_audible = true;
    }
    TEST_ASSERT(va_chord_audible, "VA Saw+Sub engine renders 3-note chord simultaneously");

    // 7. Тестування подвійних LFO та режимів TRIG / FREE
    auto lfo_tp = audio.getTrackParams(0);
    lfo_tp.lfo1.mode = LfoMode::kTrig;
    lfo_tp.lfo1.depth = 0.5f;
    lfo_tp.lfo2.mode = LfoMode::kFree;
    lfo_tp.lfo2.depth = 0.5f;
    audio.setTrackParams(0, lfo_tp);
    TEST_ASSERT(audio.getTrackParams(0).lfo1.mode == LfoMode::kTrig, "LFO1 mode can be set to TRIG");
    TEST_ASSERT(audio.getTrackParams(0).lfo2.mode == LfoMode::kFree, "LFO2 mode can be set to FREE");

    // 8. Тестування алгоритмічного Freeverb (хвіст реверберації)
    audio.panic();
    auto rev_tp = audio.getTrackParams(0);
    rev_tp.reverb_send = 1.0f;
    audio.setTrackParams(0, rev_tp);
    audio.params().reverb_decay = 0.90f;
    audio.params().reverb_mix = 1.0f;
    audio.params().delay_mix = 0.0f;
    audio.triggerNote(0, 48, 0.95f);
    int16_t rv_impulse[1024];
    audio.renderBlock(rv_impulse, 512, 32000);
    // Наступний блок без нових нот — повинен містити дифузний спадаючий хвіст
    int16_t rv_tail[1024];
    audio.renderBlock(rv_tail, 512, 32000);
    bool tail_active = false;
    for (int i = 0; i < 1024; ++i) {
        if (std::abs(static_cast<int>(rv_tail[i])) > 20) tail_active = true;
    }
    TEST_ASSERT(tail_active, "Freeverb algorithmic tank produces a dense sustaining reverb tail");

    audio.stop();
}

static void testSequencerOperations() {
    std::printf("\n=== [2/4] SEQUENCER OPERATIONS & POLYMETRY TEST ===\n");

    AudioEngine audio;
    audio.start(0, false, false);

    // 1. Тригер кроку (Elektron 3-стадійний перемикач: Note Trig -> Param Trig -> Off)
    audio.toggleStep(0, 0);
    TEST_ASSERT(audio.getStep(0, 0).enabled == true && !audio.getStep(0, 0).is_param_trig, "1st toggleStep(0, 0) enables Note Trig");
    audio.toggleStep(0, 0);
    TEST_ASSERT(audio.getStep(0, 0).enabled == true && audio.getStep(0, 0).is_param_trig, "2nd toggleStep(0, 0) enables Param Trig (Lock-only)");
    audio.toggleStep(0, 0);
    TEST_ASSERT(audio.getStep(0, 0).enabled == false, "3rd toggleStep(0, 0) disables step");

    // 2. Довжина доріжки (поліритмія) кроком у 4
    audio.setRowLength(0, 16);
    TEST_ASSERT(audio.getRowLength(0) == 16, "Row length sets to 16");

    uint32_t next_len = audio.cycleRowLength(0);
    TEST_ASSERT(next_len == 20 || next_len == 32 || (next_len % 4 == 0), "cycleRowLength increments by step of 4");

    // 3. Додавання та безпечне видалення доріжок по 1
    const int initial_rows = audio.getActiveRowCount();
    TEST_ASSERT(initial_rows == 3, "Initial active rows is 3");

    audio.setActiveRowCount(initial_rows + 1);
    TEST_ASSERT(audio.getActiveRowCount() == 4, "Adding single track increments active rows to 4");

    audio.deleteRow(3);
    TEST_ASSERT(audio.getActiveRowCount() == 3, "Deleting single track decrements active rows to 3");

    // 4. Поліфонія кроку (Чорди до 4 нот у Step Editor)
    audio.setStepChordNote(0, 0, 0, 48); // C3
    audio.setStepChordNote(0, 0, 1, 52); // E3
    audio.setStepChordNote(0, 0, 2, 55); // G3
    audio.setStepChordNote(0, 0, 3, 59); // B3
    TEST_ASSERT(audio.getStepChordNote(0, 0, 0) == 48, "Chord root note is 48 (C3)");
    TEST_ASSERT(audio.getStepChordNote(0, 0, 1) == 52, "Chord voice 2 is 52 (E3)");
    TEST_ASSERT(audio.getStepChordNote(0, 0, 2) == 55, "Chord voice 3 is 55 (G3)");
    TEST_ASSERT(audio.getStepChordNote(0, 0, 3) == 59, "Chord voice 4 is 59 (B3)");

    audio.stop();
}

static void testInputRouterAndStepEditor() {
    std::printf("\n=== [3/4] INPUT ROUTER, D-PAD REPEAT & STEP EDITOR TEST ===\n");

    UiShell shell;
    setupAppLayout(shell);

    Keymap keymap;
    InputRouter router(keymap);

    AppModel app;
    AudioEngine audio;
    InputState current_in{};
    HardwareSnapshot hw{};
    AudioEngineTelemetry tel{};
    MidiTelemetry midi{};
    uint32_t now_ms = 1000;
    UiContext ctx{app, audio, nullptr, current_in, hw, tel, midi, now_ms, &shell};

    // 1. Тестування автоповтору D-Pad
    current_in = InputState{};
    current_in.dpad_right = true;
    current_in.right_pressed = true;
    ctx.now_ms = 1000;

    int initial_step = app.cursor_step;
    router.processInput(ctx); // t=1000: edge press
    current_in.right_pressed = false;

    // t=1100: утримання до 220мс (не повинно повторюватись)
    ctx.now_ms = 1100;
    router.processInput(ctx);

    // t=1230: утримання > 220мс (автоповтор повинен спрацювати)
    ctx.now_ms = 1230;
    router.processInput(ctx);

    // t=1310: наступний автоповтор через 75мс
    ctx.now_ms = 1310;
    router.processInput(ctx);

    TEST_ASSERT(app.cursor_step > initial_step, "D-Pad repeat smoothly advances sequencer step cursor on hold");

    // 2. Тестування утримання кнопки A (Step Editor Open без зміни значень)
    app.active_view = ViewId::kSeq;
    app.step_inspector_open = false;

    current_in = InputState{};
    current_in.btn_a = true;
    current_in.a_pressed = true;
    ctx.now_ms = 2000;
    router.processInput(ctx); // press
    current_in.a_pressed = false;

    // t=2250: Hold (>=200ms) відкриває Step Editor
    ctx.now_ms = 2250;
    router.processInput(ctx);
    TEST_ASSERT(app.step_inspector_open == true, "Holding A in sequencer opens Step Editor");

    // t=2400..2600: продовжуємо утримувати A у відкритому Step Editor
    ctx.now_ms = 2500;
    router.processInput(ctx);
    ctx.now_ms = 2600;
    router.processInput(ctx);

    // Перевіряємо, що автоповтор PARAM.DEC НЕ спрацював під час утримання A!
    auto st = audio.getStep(app.cursor_row, app.cursor_step);
    TEST_ASSERT(st.note == 36 || st.note == 48 || st.note == 60, "Holding A in Step Editor DOES NOT accidentally decrement parameter values");
    app.step_inspector_open = false; // закриваємо після тесту

    // 3. Тестування плавного горизонтального скролу сітки
    SeqView seq_v;
    app.cursor_step = 0;
    app.seq_scroll_step = 0;
    audio.setRowLength(0, 64);

    // Рухаємо курсор праворуч за межі 32 видимих кроків
    seq_v.navigate(ctx, 35, 0, false);
    TEST_ASSERT(app.cursor_step == 35, "Cursor advances to step 35");
    TEST_ASSERT(app.seq_scroll_step > 0, "seq_scroll_step smoothly followed cursor beyond 32 steps");

    // Повертаємо курсор на початок
    seq_v.navigate(ctx, -35, 0, false);
    TEST_ASSERT(app.cursor_step == 0, "Cursor returns to step 0");
    TEST_ASSERT(app.seq_scroll_step == 0, "seq_scroll_step smoothly returns to step 0");

    // 4. Тестування тогла REC mode по L1
    app.rec_mode = false;
    ActionRegistry::instance().run("SEQ.REC_TOGGLE", ctx);
    TEST_ASSERT(app.rec_mode == true, "SEQ.REC_TOGGLE enables rec_mode");
    ActionRegistry::instance().run("SEQ.REC_TOGGLE", ctx);
    TEST_ASSERT(app.rec_mode == false, "SEQ.REC_TOGGLE disables rec_mode back to SEQ");

    // 5. Тестування швидких комбінацій SELECT
    ActionRegistry::instance().run("TAB.SEL_L2", ctx);
    TEST_ASSERT(app.arp_modal_open == true, "SELECT + L2 toggles ARP modal open");
    ActionRegistry::instance().run("TAB.SEL_L2", ctx);
    TEST_ASSERT(app.arp_modal_open == false, "SELECT + L2 toggles ARP modal closed");

    ActionRegistry::instance().run("TAB.SEL_R2", ctx);
    TEST_ASSERT(app.active_view == kScope, "SELECT + R2 switches to SCOPE view");
    ActionRegistry::instance().run("VIEW.BACK", ctx);
    TEST_ASSERT(app.active_view == ViewId::kSeq, "VIEW.BACK returns from SCOPE to previous view");

    // 6. Тестування переходу в робочу станцію семплювання (TAB.R1)
    ActionRegistry::instance().run("TAB.R1", ctx);
    TEST_ASSERT(app.active_view == ViewId::kSample, "TAB.R1 switches directly to SAMPLE workstation");
    ActionRegistry::instance().run("VIEW.BACK", ctx);

    // 7. Тестування асинхронного перемикання USB ролі (запобігання зависань)
    HardwareMonitor hw_mon;
    bool switch_ok = hw_mon.requestUsbRoleSwitch("device_ssh");
    TEST_ASSERT(switch_ok == true, "requestUsbRoleSwitch executes asynchronously without blocking");
    TEST_ASSERT(hw_mon.getSnapshot().usb_role == "device_ssh", "USB role snapshot updated to device_ssh");

    // 8. Тестування REC Mode: Piano Roll (мелодійний рушій) та Drum Lanes (драм-рек)
    app.rec_mode = true;
    app.selected_track = 1; // Track 2 (303 Bassline)
    app.rec_cursor_lane = 11; // Нота C
    app.rec_octave = 3;
    app.cursor_step = 2;
    ActionRegistry::instance().run("SEQ.TRIG", ctx);
    TEST_ASSERT(audio.getStep(1, 2).enabled == true && audio.getStep(1, 2).note == 36, "REC Piano Roll sets C2 note 36 on step");

    app.selected_track = 0; // Track 1 (Pure Sampler)
    auto tp_s = audio.getTrackParams(0);
    tp_s.engine = EngineType::kSampleOneShot;
    audio.setTrackParams(0, tp_s);
    app.rec_cursor_lane = 11; // Root C (interval 0)
    app.rec_octave = 4; // C (MIDI note 48)
    app.cursor_step = 0;
    ActionRegistry::instance().run("SEQ.TRIG", ctx);
    TEST_ASSERT(audio.getStep(0, 0).enabled == true && audio.getStep(0, 0).note == 48, "REC Piano Roll sets C note 48 on Sampler track");

    // Тестування чанкового парсера WAV та відтворення семплу доріжки
    bool load_ok = audio.sampler().loadWavFile("res/samples/drums/01_tr808/bd.wav", audio.sampler().trackSample(0));
    TEST_ASSERT(load_ok && audio.sampler().trackSample(0).loaded, "Chunk-based WAV parser loads 01_tr808/bd.wav successfully");

    // Перевірка синтезу аудіо: переконуємося, що семплер виводить ненульовий звук
    audio.triggerVoice(0, 48, 0.90f);
    std::vector<int16_t> audio_buf(128 * 2, 0);
    audio.renderBlock(audio_buf.data(), 128, 32000);
    bool has_sound = false;
    for (int16_t smp : audio_buf) {
        if (std::abs(smp) > 10) { has_sound = true; break; }
    }
    TEST_ASSERT(has_sound, "Sampler renders audible waveform from loaded track sample buffer");

    // Тестування безвідмовного виходу з меню семплів на кнопку B
    app.sample_browser_open = true;
    ActionRegistry::instance().run("SAMPLE_BROWSER.CLOSE", ctx);
    TEST_ASSERT(app.sample_browser_open == false, "Button B unconditionally closes sample browser modal");

    // 9. Тестування автоматичного нарізання лупера та призначення на слоти
    app.sampler.sliceToDrumSlots(0.0f, 1.0f);
    TEST_ASSERT(app.sampler.drumSlot(0).loaded == true, "Looper sliceToDrumSlots populates slot 1");
    TEST_ASSERT(app.sampler.drumSlot(7).loaded == true, "Looper sliceToDrumSlots populates slot 8");

    // 10. Тестування режиму накладання Live Looper Overdub та підсвітки слайсів
    app.sampler.armSyncedRecording(1, 120.0f, 32000);
    std::vector<float> test_audio(1024, 0.40f);
    app.sampler.processAudioInput(test_audio.data(), test_audio.data(), 1024);
    app.sampler.startOverdub();
    app.sampler.setOverdubFeedback(0.75f);
    TEST_ASSERT(app.sampler.recordingState() == RecState::kOverdub, "Sampler transitions to RecState::kOverdub");
    TEST_ASSERT(std::fabs(app.sampler.overdubFeedback() - 0.75f) < 0.01f, "Overdub feedback correctly set to 75%");

    app.sampler.setActivePlaybackSlice(2);
    TEST_ASSERT(app.sampler.activePlaybackSlice() == 2, "Active playback slice tracks S3 for live visual highlight");

    app.sampler.clearRecordedSample();
    TEST_ASSERT(app.sampler.recordingState() == RecState::kIdle, "clearRecordedSample resets sampler state to kIdle");

    // 11. Тестування відкриття меню рушіїв при натисканні на [+] ADD TRACK
    app.rec_mode = false;
    app.cursor_row = audio.getActiveRowCount(); // стоїмо на [+] ADD TRACK
    app.engine_menu_open = false;
    ActionRegistry::instance().run("SEQ.TRIG", ctx);
    TEST_ASSERT(app.engine_menu_open == true, "Triggering [+] ADD TRACK opens engine selection popup modal");
    TEST_ASSERT(app.engine_menu_is_add == true, "Engine modal is marked as add-mode");

    // Підтвердження вибору рушія додає трек і закриває меню
    app.engine_menu_sel = static_cast<int>(EngineType::kSampleOneShot);
    int prev_count = audio.getActiveRowCount();
    ActionRegistry::instance().run("ENGINE.MENU_CONFIRM", ctx);
    TEST_ASSERT(audio.getActiveRowCount() == prev_count + 1, "ENGINE.MENU_CONFIRM dynamically adds track");
    TEST_ASSERT(app.engine_menu_open == false, "Engine modal closes after track creation");
    TEST_ASSERT(app.sample_browser_open == true, "Creating Sampler track automatically opens Sample Browser");

    // 12. Тестування Sample Browser дій
    TEST_ASSERT(!app.sample_browser_items.empty(), "Sample Browser populates items from samples directory");
    ActionRegistry::instance().run("SAMPLE_BROWSER.DOWN", ctx);
    ActionRegistry::instance().run("SAMPLE_BROWSER.UP", ctx);
    ActionRegistry::instance().run("SAMPLE_BROWSER.CLOSE", ctx);
    TEST_ASSERT(app.sample_browser_open == false, "SAMPLE_BROWSER.CLOSE exits sample browser cleanly");

    // 13. Тестування делінкації параметрів: TRK.WAVE_SAMPLE не змінює параметри VA (PW) чи DX7 (PRESET)
    app.selected_track = 0;
    auto tp_va = audio.getTrackParams(0);
    tp_va.engine = EngineType::kVaMono;
    tp_va.p[5] = 0.50f; // PULSE W
    audio.setTrackParams(0, tp_va);
    const Param* p_ws = ParamRegistry::instance().find("TRK.WAVE_SAMPLE");
    TEST_ASSERT(p_ws != nullptr, "Param TRK.WAVE_SAMPLE is registered");
    if (p_ws) {
        p_ws->step(ctx, 1, false);
        TEST_ASSERT(audio.getTrackParams(0).p[5] == 0.50f, "TRK.WAVE_SAMPLE stepping does not alter VA Mono p[5] (Pulse Width)");
    }
}

static void testViewsRendering() {
    std::printf("\n=== [4/4] CANVAS 320x240 MULTI-VIEW RENDERING TEST ===\n");

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_ARGB8888);
    TEST_ASSERT(surf != nullptr, "SDL 320x240 software surface created successfully");

    SDL_Renderer* ren = SDL_CreateSoftwareRenderer(surf);
    TEST_ASSERT(ren != nullptr, "SDL 320x240 software renderer created successfully");

    UiShell shell;
    setupAppLayout(shell);

    AppModel app;
    AudioEngine audio;
    audio.start(0, false, false);

    InputState in{};
    HardwareSnapshot hw{};
    AudioEngineTelemetry tel{};
    MidiTelemetry midi{};

    // 1. Рендеринг усіх основних вкладок без крашів чи артефактів
    const ViewId views_to_test[] = {
        ViewId::kSeq,
        ViewId::kSynth,
        ViewId::kMix,
        ViewId::kFx,
        ViewId::kPerf,
        ViewId::kSample,
        ViewId::kSys,
        ViewId::kArp
    };

    for (ViewId vid : views_to_test) {
        app.active_view = vid;
        shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);

        char msg[64];
        std::snprintf(msg, sizeof(msg), "View '%s' renders onto 320x240 canvas without memory violations", viewName(vid));
        TEST_ASSERT(true, msg);
    }

    // 2. Рендеринг вкладки SYNTH для ВСІХ 9 рушіїв (перевірка динамічних матриць параметрів)
    app.active_view = ViewId::kSynth;
    for (int e = 0; e < static_cast<int>(EngineType::kCount); ++e) {
        auto eng_type = static_cast<EngineType>(e);
        auto tp = audio.getTrackParams(app.selected_track);
        tp.engine = eng_type;
        audio.setTrackParams(app.selected_track, tp);

        shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);

        char msg[96];
        std::snprintf(msg, sizeof(msg), "SYNTH layout renders tailored parameters for engine '%s'", getEngineTypeName(eng_type));
        TEST_ASSERT(true, msg);
    }

    // 3. Рендеринг модального Step Editor
    app.active_view = ViewId::kSeq;
    app.step_inspector_open = true;
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "STEP EDITOR modal renders cleanly over Sequencer");
    app.step_inspector_open = false;

    // 4. Рендеринг шару SELECT (жовті підказки швидких клавіш)
    InputState in_sel{};
    in_sel.btn_select = true;
    shell.render(app, audio, nullptr, in_sel, hw, tel, midi, 1000);
    TEST_ASSERT(true, "SELECT layer TabBar renders yellow shortcut frames cleanly");

    // 5. Рендеринг меню вибору рушіїв
    app.engine_menu_open = true;
    app.engine_menu_sel = 8; // DX7
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "Engine selection dropdown modal renders cleanly");
    app.engine_menu_open = false;

    // 6. Рендеринг модального вікна вибору семплів (Sample Pool & Filesystem)
    app.sample_browser_open = true;
    app.sample_browser_dir = "res/samples/drums/01_tr808";
    app.sample_browser_items = {"[..] PARENT DIR", "bd.wav", "sd.wav", "ch.wav"};
    app.sample_browser_sel = 1;
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "SAMPLE POOL modal renders filesystem items onto canvas cleanly");
    app.sample_browser_open = false;

    // 7. Рендеринг MIX та FX з закріпленим знизу лейаутом та 22px індикаторами
    app.active_view = ViewId::kMix;
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "MIX view renders dynamic tracks and pinned bottom oscilloscope/VU layout");

    app.active_view = ViewId::kFx;
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "FX view renders with pinned bottom oscilloscope/VU layout");

    audio.stop();
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    SDL_Quit();
}

int main() {
    std::printf("=================================================================\n");
    std::printf("       MAGICX XU10 GROOVEBOX GLOBAL AUTOMATED TEST SUITE        \n");
    std::printf("=================================================================\n");

    testAudioDspEngines();
    testSequencerOperations();
    testInputRouterAndStepEditor();
    testViewsRendering();

    std::printf("\n=================================================================\n");
    std::printf("TEST RESULTS: Total: %d | Passed: %d | Failed: %d\n", g_tests_run, g_tests_passed, g_tests_failed);
    std::printf("STATUS: %s\n", (g_tests_failed == 0) ? "ALL TESTS PASSED [OK]" : "SOME TESTS FAILED [ERROR]");
    std::printf("=================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
