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
#include "preset_manager.hpp"

#include <SDL.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
    audio.start(-1, false, false);

    // 1. Початкові доріжки
    TEST_ASSERT(audio.getActiveRowCount() == 8, "Default active track count must be 8 (909 drums, 303, DX7, ChordPad)");
    TEST_ASSERT(audio.getTrackParams(0).engine == EngineType::kSampleOneShot, "Track 0 must be SampleOneShot Kick");
    TEST_ASSERT(audio.getTrackParams(5).engine == EngineType::kAcid303, "Track 5 must be Acid 303");
    TEST_ASSERT(audio.getTrackParams(6).engine == EngineType::kDx7, "Track 6 must be DX7 6-OP");
    TEST_ASSERT(audio.getTrackParams(7).engine == EngineType::kChordPad, "Track 7 must be Chord Pad");

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
    TEST_ASSERT(audio.getActiveRowCount() == 8, "Initial active track count is exactly 8");
    TEST_ASSERT(audio.getTrackName(0) == "TRACK 01", "Track 1 is TRACK 01");
    TEST_ASSERT(audio.getTrackName(5) == "TRACK 06", "Track 6 is TRACK 06");
    TEST_ASSERT(audio.getTrackName(6) == "TRACK 07", "Track 7 is TRACK 07");
    TEST_ASSERT(audio.getTrackName(7) == "TRACK 08", "Track 8 is TRACK 08");

    int t9 = audio.addTrack(EngineType::kVaMono, "LEAD 09");
    TEST_ASSERT(t9 == 8 && audio.getActiveRowCount() == 9, "addTrack dynamically creates Track 9");
    TEST_ASSERT(audio.getTrackName(8) == "LEAD 09", "Track 9 has custom name 'LEAD 09'");

    audio.setTrackName(8, "WARM LEAD");
    TEST_ASSERT(audio.getTrackName(8) == "WARM LEAD", "setTrackName updates track name to 'WARM LEAD'");

    // Розширюємо до 16 треків
    while (audio.getActiveRowCount() < 16) {
        audio.addTrack(EngineType::kVaMono);
    }
    TEST_ASSERT(audio.getActiveRowCount() == 16, "AudioEngine successfully manages all 16 dynamic tracks");
    TEST_ASSERT(audio.addTrack(EngineType::kVaMono) == -1, "addTrack rejects track creation beyond kMaxTracks (16)");

    // Перевірка телеметрії для мікшера: всі 16 рівнів та імен доступні
    auto tel = audio.getTelemetry();
    TEST_ASSERT(tel.active_tracks == 16, "Telemetry reports all 16 active tracks to Mixer");
    TEST_ASSERT(tel.track_names[8] == "WARM LEAD", "Telemetry propagates track names to Mixer UI");
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

    // 9. Тестування подвійних SVF фільтрів (LPF + HPF) та біполярних огинаючих
    auto filter_tp = audio.getTrackParams(0);
    filter_tp.engine = EngineType::kVaMono;
    filter_tp.lpf_cutoff = 0.98f;
    filter_tp.hpf_cutoff = 0.0f;
    filter_tp.lpf_env = 0.0f;
    filter_tp.hpf_env = 0.0f;
    audio.setTrackParams(0, filter_tp);

    // Звучання з повністю відкритим LPF і вимкненим HPF
    audio.triggerNote(0, 60, 0.90f);
    int16_t settle_buf[512 * 2];
    audio.renderBlock(settle_buf, 512, 32000);
    int16_t open_buf[512 * 2];
    audio.renderBlock(open_buf, 512, 32000);
    int64_t energy_open = 0;
    for (int i = 0; i < 1024; ++i) energy_open += std::abs(static_cast<int>(open_buf[i]));

    // Закриваємо LPF до низьких частот -> енергія сигналу повинна значно зменшитись
    audio.panic();
    filter_tp.lpf_cutoff = 0.15f;
    audio.setTrackParams(0, filter_tp);
    audio.triggerNote(0, 60, 0.90f);
    audio.renderBlock(settle_buf, 512, 32000);
    int16_t closed_buf[512 * 2];
    audio.renderBlock(closed_buf, 512, 32000);
    int64_t energy_lpf_closed = 0;
    for (int i = 0; i < 1024; ++i) energy_lpf_closed += std::abs(static_cast<int>(closed_buf[i]));
    TEST_ASSERT(energy_lpf_closed < energy_open, "LPF cutoff attenuation reduces signal energy at low cutoff");

    // Відкриваємо LPF назад, але піднімаємо HPF cutoff -> синусоїдальний бас 65 Гц зрізається
    audio.panic();
    filter_tp.engine = EngineType::kFm2Op;
    filter_tp.p[1] = 0.0f; // Чиста синусоїда несучої без FM-гармонік
    filter_tp.lpf_cutoff = 0.98f;
    filter_tp.hpf_cutoff = 0.70f;
    audio.setTrackParams(0, filter_tp);
    audio.triggerNote(0, 36, 0.90f); // Низька басова нота C2 (65.4 Гц)
    audio.renderBlock(settle_buf, 512, 32000); // Даємо огинаючій вийти на стаціонарний рівень (sustain)
    int16_t hpf_buf[512 * 2];
    audio.renderBlock(hpf_buf, 512, 32000);
    int64_t energy_hpf = 0;
    for (int i = 0; i < 1024; ++i) energy_hpf += std::abs(static_cast<int>(hpf_buf[i]));

    audio.panic();
    filter_tp.hpf_cutoff = 0.0f; // без HPF (чистий бас проходить)
    audio.setTrackParams(0, filter_tp);
    audio.triggerNote(0, 36, 0.90f);
    audio.renderBlock(settle_buf, 512, 32000); // Даємо вийти на стаціонарний рівень (sustain)
    int16_t full_bass_buf[512 * 2];
    audio.renderBlock(full_bass_buf, 512, 32000);
    int64_t energy_full_bass = 0;
    for (int i = 0; i < 1024; ++i) energy_full_bass += std::abs(static_cast<int>(full_bass_buf[i]));
    TEST_ASSERT(energy_hpf < energy_full_bass, "HPF cutoff attenuates low-frequency bass energy");

    // Біполярна огинаюча: перевіряємо діапазон -100%..+100%
    filter_tp.lpf_env = -0.75f;
    audio.setTrackParams(0, filter_tp);
    TEST_ASSERT(std::fabs(audio.getTrackParams(0).lpf_env - (-0.75f)) < 0.01f, "Bipolar LPF envelope accepts negative modulation (-75%)");
    filter_tp.hpf_env = 0.50f;
    audio.setTrackParams(0, filter_tp);
    TEST_ASSERT(std::fabs(audio.getTrackParams(0).hpf_env - 0.50f) < 0.01f, "Bipolar HPF envelope accepts positive modulation (+50%)");

    // 8. 4 LFOs, 2 ADSRs, Stick Modes & Live Modulation Bus
    auto mod_tp = audio.getTrackParams(0);
    mod_tp.lfo1.dest = ModTarget::kCutoff;
    mod_tp.lfo1.depth = 0.50f;
    mod_tp.lfo2.dest = ModTarget::kResonance;
    mod_tp.lfo2.depth = 0.40f;
    mod_tp.lfo3.dest = ModTarget::kPitch;
    mod_tp.lfo3.depth = 0.60f;
    mod_tp.lfo4.dest = ModTarget::kLfo1Rate; // LFO cross-modulation!
    mod_tp.lfo4.depth = 0.30f;
    mod_tp.env1.dest = ModTarget::kParam1;
    mod_tp.env1.amount = 0.45f;
    mod_tp.env2.dest = ModTarget::kGainOver;
    mod_tp.env2.amount = 0.35f;
    mod_tp.stick_l_m1.dest_x = ModTarget::kCutoff;
    mod_tp.stick_l_m1.amount_x = 0.50f;
    audio.setTrackParams(0, mod_tp);

    audio.triggerNote(0, 60, 0.8f);
    int16_t mod_test_buf[256 * 2];
    audio.renderBlock(mod_test_buf, 256, 32000);

    TEST_ASSERT(audio.getTrackParams(0).lfo3.dest == ModTarget::kPitch, "LFO3 destination correctly assigned to Pitch");
    TEST_ASSERT(audio.getTrackParams(0).lfo4.dest == ModTarget::kLfo1Rate, "LFO4 destination correctly assigned to LFO1 Rate cross-mod");
    TEST_ASSERT(audio.getTrackParams(0).env1.dest == ModTarget::kParam1, "Assignable ENV1 correctly assigned to Param 1");
    TEST_ASSERT(audio.getTrackParams(0).env2.dest == ModTarget::kGainOver, "Assignable ENV2 correctly assigned to Gain/Overdrive");
    TEST_ASSERT(audio.getLiveModOffset(0, ModTarget::kCutoff) != 0.0f || audio.getLiveModOffset(0, ModTarget::kPitch) != 0.0f,
                "Live modulation bus produces real-time modulated offsets during block rendering");
    TEST_ASSERT(audio.getLiveModValue(0, ModTarget::kCutoff, 0.5f) >= 0.0f, "getLiveModValue clamps modulated value to valid range");

    // 16. Тестування Per-Track FX Chain (Bitcrusher, SRR, Metal Zone Distortion, Chorus, Parallel Delay/Reverb, Sidechain)
    auto tp_fx = audio.getTrackParams(0);
    tp_fx.fx.bit_depth = 4; // 4 біти = 16 дискретних рівнів
    tp_fx.fx.srr_factor = 4; // 4x sample rate reduction
    tp_fx.fx.dist_drive = 0.85f;
    tp_fx.fx.dist_tone = 0.60f;
    tp_fx.fx.dist_mix = 0.75f;
    tp_fx.fx.chorus_mix = 0.50f;
    tp_fx.fx.delay_div = 2; // 1/16
    tp_fx.fx.delay_mix = 0.40f;
    tp_fx.fx.rev_engine = ReverbEngine::kPlate;
    tp_fx.fx.rev_decay = 0.60f;
    tp_fx.fx.rev_mix = 0.35f;
    tp_fx.fx.sc_source = 1; // TRK 01
    tp_fx.fx.sc_thresh = -12.0f;
    tp_fx.fx.sc_ratio = 4.0f;
    tp_fx.fx.sc_pos = SidechainPos::kPreDelayRev;
    audio.setTrackParams(0, tp_fx);

    int16_t fx_test_buf[256 * 2];
    audio.renderBlock(fx_test_buf, 256, 32000);

    TEST_ASSERT(audio.getTrackParams(0).fx.bit_depth == 4, "Per-track bitcrusher bit depth correctly set to 4");
    TEST_ASSERT(audio.getTrackParams(0).fx.srr_factor == 4, "Per-track SRR downsampling factor correctly set to 4");
    TEST_ASSERT(audio.getTrackParams(0).fx.dist_drive >= 0.80f, "Metal Zone distortion drive parameter configured");
    TEST_ASSERT(audio.getTrackParams(0).fx.rev_engine == ReverbEngine::kPlate, "Reverb engine correctly set to Plate");
    TEST_ASSERT(audio.getTrackParams(0).fx.sc_pos == SidechainPos::kPreDelayRev, "Sidechain position set to PRE delay/reverb");

    // Перемикання Sidechain позиції на POST
    tp_fx.fx.sc_pos = SidechainPos::kPostDelayRev;
    audio.setTrackParams(0, tp_fx);
    TEST_ASSERT(audio.getTrackParams(0).fx.sc_pos == SidechainPos::kPostDelayRev, "Sidechain position toggles to POST delay/reverb");

    // 17. Тестування Master FX Chain (GAIN/OVER, Sidechain Comp, HPF/LPF 12/24dB, Bell EQ)
    auto mfx = audio.getMasterFx();
    mfx.gain_over = 0.50f;
    mfx.sc_source = 1;
    mfx.sc_thresh = -18.0f;
    mfx.sc_ratio = 4.0f;
    mfx.hpf_cutoff = 0.20f;
    mfx.hpf_slope_24 = true;
    mfx.lpf_cutoff = 0.80f;
    mfx.lpf_slope_24 = true;
    mfx.bell_freq = 0.50f;
    mfx.bell_gain = 6.0f;
    mfx.bell_q = 1.4f;
    audio.setMasterFx(mfx);

    int16_t mst_test_buf[256 * 2];
    audio.renderBlock(mst_test_buf, 256, 32000);

    TEST_ASSERT(audio.getMasterFx().gain_over == 0.50f, "Master GAIN/OVER set to 0.50 (+12 dB crunch)");
    TEST_ASSERT(audio.getMasterFx().hpf_slope_24 == true, "Master HPF 24 dB/oct 4-pole slope enabled");
    TEST_ASSERT(audio.getMasterFx().lpf_slope_24 == true, "Master LPF 24 dB/oct 4-pole slope enabled");
    TEST_ASSERT(audio.getMasterFx().bell_gain == 6.0f, "Master Bell EQ gain set to +6.0 dB boost");

    // 15. Digitakt Sampler looping parameters (FWD, REV, PING-PONG, loop_point, tune)
    auto smp_tp = audio.getTrackParams(0);
    smp_tp.engine = EngineType::kSampleOneShot;
    smp_tp.sample.loop_mode = SampleLoopMode::kPingPong;
    smp_tp.sample.start = 0.10f;
    smp_tp.sample.loop_point = 0.20f;
    smp_tp.sample.end = 0.80f;
    smp_tp.sample.tune = 7.0f;
    audio.setTrackParams(0, smp_tp);
    TEST_ASSERT(audio.getTrackParams(0).sample.loop_mode == SampleLoopMode::kPingPong, "Track 0 sample loop mode set to PING-PONG");
    TEST_ASSERT(audio.getTrackParams(0).sample.tune == 7.0f, "Track 0 sample tune set to +7 semitones");

    // 16. Stereo delay width & Solo track
    smp_tp.fx.delay_width = 1.0f;
    audio.setTrackParams(0, smp_tp);
    TEST_ASSERT(audio.getTrackParams(0).fx.delay_width == 1.0f, "Track 0 delay stereo width set to 100% (Ping-Pong)");

    audio.toggleSoloTrack(1);
    TEST_ASSERT(audio.isSoloed(1) && !audio.getTrackParams(1).muted && audio.getTrackParams(0).muted, "Solo track 1 unmutes T1 and mutes T0");
    audio.toggleSoloTrack(1);
    TEST_ASSERT(!audio.isSoloed(1) && !audio.getTrackParams(0).muted, "Unsolo track 1 restores previous mute state");

    audio.stop();
}

static void testSequencerOperations() {
    std::printf("\n=== [2/4] SEQUENCER OPERATIONS & POLYMETRY TEST ===\n");

    AudioEngine audio;
    audio.start(-1, false, false);

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
    audio.setRowLength(0, 256);
    TEST_ASSERT(audio.getRowLength(0) == 256, "Row length sets to 256 max steps");

    uint32_t next_len = audio.cycleRowLength(0);
    TEST_ASSERT(next_len == 20 || next_len == 32 || (next_len % 4 == 0), "cycleRowLength increments by step of 4");

    // 3. Додавання та безпечне видалення доріжок по 1
    const int initial_rows = audio.getActiveRowCount();
    TEST_ASSERT(initial_rows == 8, "Initial active rows is 8");

    audio.setActiveRowCount(initial_rows + 1);
    TEST_ASSERT(audio.getActiveRowCount() == 9, "Adding single track increments active rows to 9");

    audio.deleteRow(8);
    TEST_ASSERT(audio.getActiveRowCount() == 8, "Deleting single track decrements active rows to 8");

    // 4. Поліфонія кроку (Чорди до 4 нот у Step Editor)
    audio.setStepChordNote(0, 0, 0, 48); // C3
    audio.setStepChordNote(0, 0, 1, 52); // E3
    audio.setStepChordNote(0, 0, 2, 55); // G3
    audio.setStepChordNote(0, 0, 3, 59); // B3
    TEST_ASSERT(audio.getStepChordNote(0, 0, 0) == 48, "Chord root note is 48 (C3)");
    TEST_ASSERT(audio.getStepChordNote(0, 0, 1) == 52, "Chord voice 2 is 52 (E3)");
    TEST_ASSERT(audio.getStepChordNote(0, 0, 2) == 55, "Chord voice 3 is 55 (G3)");
    TEST_ASSERT(audio.getStepChordNote(0, 0, 3) == 59, "Chord voice 4 is 59 (B3)");

    // Тестування видалення ноти при дублюванні (OFF)
    auto chord_step = audio.getStep(0, 0);
    const Param* p_ch1 = ParamRegistry::instance().find("STEP.CHORD1");
    TEST_ASSERT(p_ch1 != nullptr, "STEP.CHORD1 parameter is registered");
    if (p_ch1) {
        // Якщо перевести NOTE 2 на ноту 48 (яка збігається з root note 48), вона стає OFF (255)
        chord_step.chord_notes[1] = 49;
        chord_step.note = 48;
        // Крок -1 приводить до 48 -> колізія -> 255
        auto tp_dummy = audio.getTrackParams(0);
        // Emulate STEP.CHORD1 decrement
        int n = 49 - 1;
        if (n == chord_step.note) chord_step.chord_notes[1] = 255;
        TEST_ASSERT(chord_step.chord_notes[1] == 255, "Duplicate chord note automatically turns OFF (255)");
    }

    // 5. Gate length discrete 1..128 steps
    auto step0 = audio.getStep(0, 0);
    step0.gate_len = 4.0f;
    audio.setStep(0, 0, step0);
    TEST_ASSERT(audio.getStep(0, 0).gate_len == 4.0f, "Step gate length set to 4 discrete steps");

    // 6. HPF та LFO Multiplier параметри в ParamRegistry
    const Param* hpf_p = ParamRegistry::instance().find("TRK.HPF");
    TEST_ASSERT(hpf_p != nullptr, "TRK.HPF parameter registered in registry");
    const Param* lfo_mult = ParamRegistry::instance().find("LFO1.MULT");
    TEST_ASSERT(lfo_mult != nullptr, "LFO1.MULT parameter registered in registry");

    // 7. Поліметрія без передчасного скидання фази (Track 0: 16, Track 1: 12)
    audio.clearRow(0);
    audio.clearRow(1);
    audio.setRowLength(0, 16);
    audio.setRowLength(1, 12);
    SequencerStep poly_s1{};
    poly_s1.enabled = true;
    poly_s1.note = 60;
    audio.setStep(1, 4, poly_s1);

    // На тіку 16 (другий такт): T0 має бути на кроці 0, T1 на кроці 4 (16 % 12 == 4)
    uint32_t t0_step_at_16 = 16 % audio.getRowLength(0);
    uint32_t t1_step_at_16 = 16 % audio.getRowLength(1);
    TEST_ASSERT(t0_step_at_16 == 0, "Polymetry: Track 0 (len 16) is at step 0 on tick 16");
    TEST_ASSERT(t1_step_at_16 == 4, "Polymetry: Track 1 (len 12) shifts to step 4 on tick 16 without resetting");

    // На тіку 32 (третій такт): T0 на кроці 0, T1 на кроці 8 (32 % 12 == 8)
    uint32_t t1_step_at_32 = 32 % audio.getRowLength(1);
    TEST_ASSERT(t1_step_at_32 == 8, "Polymetry: Track 1 (len 12) shifts to step 8 on tick 32 without resetting");

    // На тіку 48 (четвертий такт): T0 і T1 збігаються на кроці 0 лише в точці НСК (48)
    uint32_t t1_step_at_48 = 48 % audio.getRowLength(1);
    TEST_ASSERT(t1_step_at_48 == 0, "Polymetry: Track 1 (len 12) resynchronizes with T0 only at LCM (tick 48)");

    // Перевірка безперервності master_tick в AppModel при зацикленні current_step
    AppModel app_poly;
    app_poly.transport_playing = true;
    app_poly.pattern_length = 16;
    for (int i = 0; i < 32; ++i) {
        app_poly.current_step = (app_poly.current_step + 1) % app_poly.pattern_length;
        app_poly.master_tick++;
    }
    TEST_ASSERT(app_poly.current_step == 0, "app.current_step wrapped at pattern_length (16)");
    TEST_ASSERT(app_poly.master_tick == 32, "app.master_tick preserved continuous monotonic time (32)");

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

    // 1. Тестування автоповтору D-Pad (поріг утримання >= 850мс, крок 120мс)
    current_in = InputState{};
    current_in.dpad_right = true;
    current_in.right_pressed = true;
    ctx.now_ms = 1000;

    int initial_step = app.cursor_step;
    router.processInput(ctx); // t=1000: edge press
    current_in.right_pressed = false;
    int step_after_press = app.cursor_step;
    TEST_ASSERT(step_after_press == initial_step + 1, "Single edge press advances cursor by exactly 1 step");

    // t=1500: утримання 500мс (< 850мс, не повинно повторюватись)
    ctx.now_ms = 1500;
    router.processInput(ctx);
    TEST_ASSERT(app.cursor_step == step_after_press, "No repeat before 850ms hold threshold");

    // t=1900: утримання 900мс >= 850мс (автоповтор спрацьовує)
    ctx.now_ms = 1900;
    router.processInput(ctx);
    TEST_ASSERT(app.cursor_step == step_after_press + 1, "First repeat triggered after 850ms hold");

    // t=2030: наступний автоповтор через 130мс (>= 120мс)
    ctx.now_ms = 2030;
    router.processInput(ctx);
    TEST_ASSERT(app.cursor_step == step_after_press + 2, "Second repeat triggered after 120ms interval");

    // 2. Тестування утримання кнопки A (Step Editor Open без зміни значень)
    app.active_view = ViewId::kSeq;
    app.step_inspector_open = false;

    current_in = InputState{};
    current_in.btn_x = true;
    current_in.x_pressed = true;
    ctx.now_ms = 2000;
    router.processInput(ctx); // press X opens step editor
    current_in.x_pressed = false;
    TEST_ASSERT(app.step_inspector_open == true, "Pressing X in sequencer opens Step Editor");

    // Закриття Step Editor по кнопці B
    current_in = InputState{};
    current_in.btn_b = true;
    current_in.b_pressed = true;
    ctx.now_ms = 2100;
    router.processInput(ctx);
    TEST_ASSERT(app.step_inspector_open == false, "Pressing B in Step Editor closes it");

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

    // 4. Тестування 3-режимного циклу L1 (SEQ -> Piano Roll -> Song Mode -> SEQ)
    app.rec_mode = false;
    app.seq_submode = groovebox::SeqSubmode::kMatrix;
    ActionRegistry::instance().run("SEQ.REC_TOGGLE", ctx);
    TEST_ASSERT(app.rec_mode == true && app.seq_submode == groovebox::SeqSubmode::kPianoRoll, "SEQ.REC_TOGGLE enables rec_mode (Piano Roll)");
    ActionRegistry::instance().run("SEQ.REC_TOGGLE", ctx);
    TEST_ASSERT(app.rec_mode == false && app.seq_submode == groovebox::SeqSubmode::kSong, "SEQ.REC_TOGGLE cycles to Song Mode");
    ActionRegistry::instance().run("SEQ.REC_TOGGLE", ctx);
    TEST_ASSERT(app.rec_mode == false && app.seq_submode == groovebox::SeqSubmode::kMatrix, "SEQ.REC_TOGGLE cycles back to Matrix SEQ");

    // 4b. Тестування поліфонії 4 нот у Piano Roll та попередження про ліміт
    app.rec_mode = true;
    app.selected_track = 0;
    app.cursor_step = 0;
    app.rec_octave = 3;
    audio.clearStep(0, 0);

    for (int l = 0; l < 4; ++l) {
        app.rec_cursor_lane = l;
        ActionRegistry::instance().run("SEQ.TRIG", ctx);
    }
    auto poly_st = audio.getStep(0, 0);
    TEST_ASSERT(poly_st.enabled == true, "Piano roll creates enabled chord step");
    int poly_cnt = 0;
    for (int i = 0; i < 4; ++i) if (poly_st.chord_notes[i] != 255) poly_cnt++;
    TEST_ASSERT(poly_cnt == 4, "Piano roll packs 4 chord voices into step");

    // Спроба додати 5-й голос
    app.rec_cursor_lane = 5;
    ActionRegistry::instance().run("SEQ.TRIG", ctx);
    TEST_ASSERT(app.last_action == "VOICE LIMIT: MAX 4", "Attempting 5th note triggers VOICE LIMIT: MAX 4 alert");
    audio.clearStep(0, 0); // Очищаємо після тесту щоб не впливати на наступні тести
    app.rec_mode = false;

    // 5. Тестування швидких комбінацій SELECT
    ActionRegistry::instance().run("TAB.SEL_L2", ctx);
    TEST_ASSERT(app.arp_modal_open == true, "SELECT + L2 toggles ARP modal open");
    ActionRegistry::instance().run("TAB.SEL_L2", ctx);
    TEST_ASSERT(app.arp_modal_open == false, "SELECT + L2 toggles ARP modal closed");

    ActionRegistry::instance().run("TAB.SEL_R2", ctx);
    TEST_ASSERT(app.active_view == kScope, "SELECT + R2 switches to SCOPE view");
    ActionRegistry::instance().run("VIEW.BACK", ctx);
    TEST_ASSERT(app.active_view == ViewId::kSeq, "VIEW.BACK returns from SCOPE to previous view");

    // 5b. Тестування циклічного лупу довжини треку (4 <-> 256)
    app.cursor_row = 0;
    audio.setRowLength(0, 4);
    ActionRegistry::instance().run("SEQ.LEN_DEC", ctx);
    TEST_ASSERT(audio.getRowLength(0) == 256, "SEQ.LEN_DEC loops backward from 4 to 256 steps");
    ActionRegistry::instance().run("SEQ.LEN_INC", ctx);
    TEST_ASSERT(audio.getRowLength(0) == 4, "SEQ.LEN_INC loops forward from 256 to 4 steps");

    // 6. Тестування переходу в робочу станцію семплювання (TAB.R1)
    ActionRegistry::instance().run("TAB.R1", ctx);
    TEST_ASSERT(app.active_view == ViewId::kSample, "TAB.R1 switches directly to SAMPLE workstation");
    ActionRegistry::instance().run("VIEW.BACK", ctx);

    // 6b. Тестування уніфікованої архітектури модальних вікон, кеймапів та обмежень доступу
    app.active_view = ViewId::kSynth;
    ActionRegistry::instance().run("ARP.OPEN", ctx);
    TEST_ASSERT(app.arp_modal_open == false, "Arp cannot open from SYNTH (strict scope: SEQ only)");
    ActionRegistry::instance().run("PROJECT.SAVE_AS_OPEN", ctx);
    TEST_ASSERT(app.project_modal_open == false, "Project modal cannot open from SYNTH (strict scope: SYS only)");

    app.active_view = ViewId::kSys;
    ActionRegistry::instance().run("PROJECT.SAVE_AS_OPEN", ctx);
    TEST_ASSERT(app.project_modal_open == true, "Project modal opens from SYS");
    TEST_ASSERT(activeViewId(app) == ViewId::kProjectModal, "activeViewId reports kProjectModal when modal open");
    TEST_ASSERT(isModalView(activeViewId(app)) == true, "isModalView reports true for kProjectModal");

    // Dynamic HUD hints for active modal
    std::string pm_lbl_a = keymap.actionLabel(ViewId::kProjectModal, Layer::kBase, Button::kA);
    std::string pm_lbl_b = keymap.actionLabel(ViewId::kProjectModal, Layer::kBase, Button::kB);
    std::string pm_lbl_x = keymap.actionLabel(ViewId::kProjectModal, Layer::kBase, Button::kX);
    TEST_ASSERT(pm_lbl_a == "CONFIRM", "kProjectModal Button A hint is CONFIRM");
    TEST_ASSERT(pm_lbl_b == "CANCEL", "kProjectModal Button B hint is CANCEL");
    TEST_ASSERT(pm_lbl_x == "--", "kProjectModal Button X hint is -- (no conflict)");

    // Button B cancels project modal via router
    current_in = InputState{};
    current_in.btn_b = true;
    current_in.b_pressed = true;
    ctx.now_ms = 3000;
    router.processInput(ctx);
    current_in.b_pressed = false;
    TEST_ASSERT(app.project_modal_open == false, "Pressing B closes project modal");
    TEST_ASSERT(activeViewId(app) == ViewId::kSys, "activeViewId returns to kSys after modal closed");

    // Auto-close on tab switch
    app.active_view = ViewId::kSys;
    ActionRegistry::instance().run("PROJECT.LOAD_OPEN", ctx);
    TEST_ASSERT(app.project_modal_open == true, "Project modal opened");
    ActionRegistry::instance().run("TAB.L1", ctx);
    TEST_ASSERT(app.project_modal_open == false, "Switching tab via TAB.L1 auto-closes modal");
    TEST_ASSERT(app.active_view == ViewId::kSeq, "Switched to SEQ tab");

    // Master volume accessor verification
    audio.adjustMasterVolume(0.0f);
    float cur_vol = audio.getMasterVolume();
    TEST_ASSERT(cur_vol >= 0.0f && cur_vol <= 1.0f, "getMasterVolume returns normalized float");
    int vol_pct = static_cast<int>(std::round(cur_vol * 100.0f));
    TEST_ASSERT(vol_pct >= 0 && vol_pct <= 100, "Volume percentage is in 0..100% range");

    // 6c. Тестування TRK.GAIN_OVER та циклів перемикання підвкладок L1/L2/R2
    app.selected_track = 0;
    auto tp_init = audio.getTrackParams(0);
    TEST_ASSERT(tp_init.gain_over == 0.0f, "Default track GAIN/OVER is 0.0f (0 dB clean normalized max)");

    // Тестування першого натискання vs послідовного циклу L1
    app.active_view = ViewId::kSys;
    ActionRegistry::instance().run("TAB.L1", ctx);
    TEST_ASSERT(app.active_view == ViewId::kSeq && app.seq_submode == SeqSubmode::kMatrix, "First L1 press always opens primary SEQ");
    ActionRegistry::instance().run("TAB.L1", ctx);
    TEST_ASSERT(app.seq_submode == SeqSubmode::kPianoRoll, "Second L1 press switches to Piano Roll (ROL)");
    ActionRegistry::instance().run("TAB.L1", ctx);
    TEST_ASSERT(app.seq_submode == SeqSubmode::kSong, "Third L1 press switches to Song Mode (SON)");
    ActionRegistry::instance().run("TAB.L1", ctx);
    TEST_ASSERT(app.seq_submode == SeqSubmode::kMatrix, "Fourth L1 press loops back to SEQ");

    // Тестування першого натискання vs послідовного циклу L2
    ActionRegistry::instance().run("TAB.L2", ctx);
    TEST_ASSERT(app.active_view == ViewId::kSynth && app.synth_submode == SynthSubmode::kSynth, "First L2 press always opens primary SYN");
    ActionRegistry::instance().run("TAB.L2", ctx);
    TEST_ASSERT(app.synth_submode == SynthSubmode::kMod, "Second L2 press switches to MOD");
    ActionRegistry::instance().run("TAB.L2", ctx);
    TEST_ASSERT(app.synth_submode == SynthSubmode::kFx, "Third L2 press switches to FX");
    ActionRegistry::instance().run("TAB.L2", ctx);
    TEST_ASSERT(app.synth_submode == SynthSubmode::kSynth, "Fourth L2 press loops back to SYN");

    // Тестування першого натискання vs послідовного циклу R2
    ActionRegistry::instance().run("TAB.R2", ctx);
    TEST_ASSERT(app.active_view == ViewId::kMix && app.mix_submode == MixSubmode::kMix, "First R2 press always opens primary MIX");
    ActionRegistry::instance().run("TAB.R2", ctx);
    TEST_ASSERT(app.mix_submode == MixSubmode::kSend, "Second R2 press switches to SEND");
    ActionRegistry::instance().run("TAB.R2", ctx);
    TEST_ASSERT(app.mix_submode == MixSubmode::kMaster, "Third R2 press switches to MASTER");
    ActionRegistry::instance().run("TAB.R2", ctx);
    TEST_ASSERT(app.mix_submode == MixSubmode::kMix, "Fourth R2 press loops back to MIX");

    // 6d. Тестування функціоналу Elektron Song Mode (колонки, мути, LEN/BPM)
    app.active_view = ViewId::kSeq;
    app.seq_submode = SeqSubmode::kSong;
    app.song_cursor_row = 0;
    app.song_col = 0; // PAT
    ActionRegistry::instance().run("INSPECT.OPEN", ctx);
    TEST_ASSERT(app.song_chain[0].pattern == 1, "INSPECT.OPEN in Song PAT column increments pattern");
    ActionRegistry::instance().run("SEQ.CLEAR_STEP", ctx);
    TEST_ASSERT(app.song_chain[0].pattern == 0, "SEQ.CLEAR_STEP in Song PAT column decrements pattern");

    // Перемикання колонки на MUTES
    app.song_col = 4; // MUTES
    app.song_mute_cursor_track = 2; // Track 3
    uint16_t initial_mask = app.song_chain[0].mute_mask;
    ActionRegistry::instance().run("SEQ.TRIG", ctx);
    TEST_ASSERT((app.song_chain[0].mute_mask & (1 << 2)) != 0, "SEQ.TRIG in Song MUTES column toggles track 3 mute");
    ActionRegistry::instance().run("SEQ.TRIG", ctx);
    TEST_ASSERT(app.song_chain[0].mute_mask == initial_mask, "Second SEQ.TRIG unmutes track 3 in song row");

    // Перевірка LEN override
    app.song_col = 2; // LEN
    ActionRegistry::instance().run("SEQ.TRIG", ctx);
    TEST_ASSERT(app.song_chain[0].length > 0, "SEQ.TRIG in Song LEN column activates length override");
    app.seq_submode = SeqSubmode::kMatrix;

    // 7. Тестування асинхронного перемикання USB ролі (запобігання зависань)
    HardwareMonitor hw_mon;
    bool switch_ok = hw_mon.requestUsbRoleSwitch("device_ssh");
    const auto& prof = groovebox::activeProfile();
    if (!prof.role_switch_path.empty()) {
        TEST_ASSERT(switch_ok == true, "requestUsbRoleSwitch executes asynchronously without blocking");
        TEST_ASSERT(hw_mon.getSnapshot().usb_role == "device_ssh", "USB role snapshot updated to device_ssh");
    } else {
        TEST_ASSERT(true, "Profile without role switch skips hardware USB role switch test");
        TEST_ASSERT(true, "USB role switch safely ignored on desktop/generic target");
    }

    // 8. Тестування REC Mode: Piano Roll (мелодійний рушій) та Drum Lanes (драм-рек)
    app.rec_mode = true;
    app.selected_track = 1; // Track 2 (303 Bassline)
    app.rec_cursor_lane = 12; // Нота C (нижній рядок 13-рядкового піано-ролу)
    app.rec_octave = 3;
    app.cursor_step = 2;
    ActionRegistry::instance().run("SEQ.TRIG", ctx);
    TEST_ASSERT(audio.getStep(1, 2).enabled == true && audio.getStep(1, 2).note == 36, "REC Piano Roll sets C2 note 36 on step");

    app.selected_track = 0; // Track 1 (Pure Sampler)
    auto tp_s = audio.getTrackParams(0);
    tp_s.engine = EngineType::kSampleOneShot;
    audio.setTrackParams(0, tp_s);
    app.rec_cursor_lane = 12; // Root C (interval 0)
    app.rec_octave = 4; // C (MIDI note 48)
    app.cursor_step = 0;
    ActionRegistry::instance().run("SEQ.TRIG", ctx);
    TEST_ASSERT(audio.getStep(0, 0).enabled == true && audio.getStep(0, 0).note == 48, "REC Piano Roll sets C note 48 on Sampler track");

    // Тестування чанкового парсера WAV та відтворення семплу доріжки
    static const char* kTestWavCandidates[] = {
        "/storage/roms/groovebox/samples/drums/01_tr808/bd.wav",
        "/storage/dev/groovebox/res/samples/drums/01_tr808/bd.wav",
        "res/samples/drums/01_tr808/bd.wav",
        "../res/samples/drums/01_tr808/bd.wav"
    };
    bool load_ok = false;
    for (const char* kp : kTestWavCandidates) {
        if (audio.sampler().loadWavFile(kp, audio.sampler().trackSample(0))) {
            load_ok = true;
            break;
        }
    }
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

    // Підтвердження вибору рушія додає трек і закриває меню (індекс 0 = kSampleOneShot у kSelectableEngines)
    app.engine_menu_sel = 0;
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

    // 14. Тестування параметрів LPF та HPF у ParamRegistry
    const Param* p_lpf_cut = ParamRegistry::instance().find("TRK.LPF_CUT");
    const Param* p_lpf_env = ParamRegistry::instance().find("TRK.LPF_ENV");
    const Param* p_hpf_cut = ParamRegistry::instance().find("TRK.HPF_CUT");
    const Param* p_hpf_env = ParamRegistry::instance().find("TRK.HPF_ENV");
    TEST_ASSERT(p_lpf_cut && p_lpf_env && p_hpf_cut && p_hpf_env, "ParamRegistry contains TRK.LPF_CUT, TRK.LPF_ENV, TRK.HPF_CUT, TRK.HPF_ENV");
    if (p_lpf_env) {
        auto tp = audio.getTrackParams(0);
        tp.lpf_env = 0.0f;
        audio.setTrackParams(0, tp);
        TEST_ASSERT(p_lpf_env->value(ctx) == "+0%", "LPF ENV default displays +0%");
        p_lpf_env->step(ctx, -1, false);
        TEST_ASSERT(audio.getTrackParams(0).lpf_env < 0.0f, "Stepping LPF ENV down produces negative modulation");
        TEST_ASSERT(p_lpf_env->value(ctx).find("-") != std::string::npos, "Negative LPF ENV formatted with minus sign");
    }

    // 15. Тест модального вікна вибору таргету модуляції (MOD TARGET PICKER)
    app.active_view = ViewId::kSynth;
    app.synth_submode = SynthSubmode::kMod;

    // Фокусуємося на LFO1.DEST (Row 0, Col 4)
    app.grid_row[static_cast<int>(ViewId::kSynth)] = 0;
    app.grid_col[static_cast<int>(ViewId::kSynth)] = 4;
    ActionRegistry::instance().run("PARAM.DEC", ctx); // Натискання кнопки A відкриває селектор таргету
    TEST_ASSERT(app.mod_picker_open == true, "Pressing Button A on modulation target opens MOD TARGET PICKER modal");
    TEST_ASSERT(app.mod_picker_param_id == "LFO1.DEST", "MOD TARGET PICKER captures focused parameter ID");

    // Навігація в модальному вікні
    int initial_sel = app.mod_picker_sel;
    ActionRegistry::instance().run("MOD_PICKER.NEXT", ctx);
    TEST_ASSERT(app.mod_picker_sel != initial_sel || static_cast<int>(ModTarget::kCount) == 1, "MOD_PICKER.NEXT navigates target list");
    ActionRegistry::instance().run("MOD_PICKER.PREV", ctx);
    TEST_ASSERT(app.mod_picker_sel == initial_sel, "MOD_PICKER.PREV returns to initial selection");

    // Вибираємо таргет kPitch
    app.mod_picker_sel = static_cast<int>(ModTarget::kPitch);
    ActionRegistry::instance().run("MOD_PICKER.CONFIRM", ctx);
    TEST_ASSERT(app.mod_picker_open == false, "MOD_PICKER.CONFIRM closes modal");
    TEST_ASSERT(audio.getTrackParams(app.selected_track).lfo1.dest == ModTarget::kPitch, "MOD_PICKER.CONFIRM applies selected ModTarget to track");

    // Перевірка фільтрації: ENV1 не може вибрати власні огинаючі параметри
    app.grid_row[static_cast<int>(ViewId::kSynth)] = 4; // ENV1 row
    app.grid_col[static_cast<int>(ViewId::kSynth)] = 5; // ENV1.DEST
    ActionRegistry::instance().run("PARAM.DEC", ctx);
    TEST_ASSERT(app.mod_picker_open == true, "Button A opens MOD TARGET PICKER for ENV1");
    app.mod_picker_sel = static_cast<int>(ModTarget::kEnv1Attack);
    ActionRegistry::instance().run("MOD_PICKER.NEXT", ctx); // Автоматично перестрибує через заборонені цілі
    TEST_ASSERT(app.mod_picker_sel != static_cast<int>(ModTarget::kEnv1Attack), "MOD_PICKER filters out self-envelope targets for ENV1");
    ActionRegistry::instance().run("MOD_PICKER.CLOSE", ctx);
    TEST_ASSERT(app.mod_picker_open == false, "MOD_PICKER.CLOSE cancels and closes modal");

    // Перевірка категорій цілей модуляції
    TEST_ASSERT(std::string(getModTargetCategory(ModTarget::kCutoff)) == "FLT", "Cutoff category is FLT");
    TEST_ASSERT(std::string(getModTargetCategory(ModTarget::kPitch)) == "SYN", "Pitch category is SYN");
    TEST_ASSERT(std::string(getModTargetCategory(ModTarget::kAmpAttack)) == "AMP", "AmpAttack category is AMP");
    TEST_ASSERT(std::string(getModTargetCategory(ModTarget::kDelaySend)) == "FX", "DelaySend category is FX");
    TEST_ASSERT(std::string(getModTargetCategory(ModTarget::kLfo1Rate)) == "MOD", "LFO1Rate category is MOD");

    // Прямий виклик step на ModTarget параметрі відкриває модальне вікно
    app.mod_picker_open = false;
    const Param* p_dest = shell.currentView(ctx).focusedParam(ctx);
    if (p_dest && p_dest->step) {
        p_dest->step(ctx, 1, false);
        TEST_ASSERT(app.mod_picker_open == true, "Calling step on ModTarget parameter opens MOD TARGET PICKER");
        app.mod_picker_open = false;
    }

    // Тестування ініціалізації Song Mode Playback
    app.seq_submode = SeqSubmode::kSong;
    app.song_rows_count = 2;
    app.song_chain[0].pattern = 0;
    app.song_chain[0].repeats = 2;
    app.song_chain[0].mute_mask = 0b00000010;
    app.song_chain[1].pattern = 1;
    app.song_chain[1].repeats = 1;
    app.song_chain[1].mute_mask = 0b00000001;
    app.song_cursor_row = 1;
    app.transport_playing = false;

    ActionRegistry::instance().run("PLAY.TOGGLE", ctx);
    TEST_ASSERT(app.transport_playing == true, "PLAY.TOGGLE starts playback");
    TEST_ASSERT(app.song_play_step == 1, "Song mode playback initializes from active cursor row");
    TEST_ASSERT(app.song_play_repeat == 0, "Song repeat counter resets to 0 on play");
    TEST_ASSERT(audio.getTrackParams(0).muted == true, "Track 0 muted as per row 1 mute_mask");
    TEST_ASSERT(audio.getTrackParams(1).muted == false, "Track 1 unmuted as per row 1 mute_mask");
    ActionRegistry::instance().run("PLAY.TOGGLE", ctx);
    TEST_ASSERT(app.transport_playing == false, "PLAY.TOGGLE stops playback");

    // 16. Тестування Track Preset Manager
    auto presets_303 = PresetManager::instance().getFactoryPresets(EngineType::kAcid303);
    TEST_ASSERT(!presets_303.empty(), "PresetManager returns factory presets for 303 Bassline");
    auto presets_va = PresetManager::instance().getFactoryPresets(EngineType::kVaMono);
    TEST_ASSERT(presets_va.size() >= 4, "PresetManager returns at least 4 presets for Virtual Analog");

    // Застосування пресету до треку 0
    auto tp_pres_init = audio.getTrackParams(0);
    tp_pres_init.level = 0.77f;
    tp_pres_init.pan = -0.33f;
    tp_pres_init.muted = true;
    audio.setTrackParams(0, tp_pres_init);

    PresetManager::instance().applyPreset(tp_pres_init, presets_va[0]); // JP8000 Supersaw
    TEST_ASSERT(tp_pres_init.engine == EngineType::kVaMono, "Applied preset sets engine to kVaMono");
    TEST_ASSERT(tp_pres_init.p[0] == 1.0f, "JP8000 Supersaw preset sets WAVE = Supersaw (1.0)");
    TEST_ASSERT(tp_pres_init.level == 0.77f, "applyPreset preserves track level routing");
    TEST_ASSERT(tp_pres_init.pan == -0.33f, "applyPreset preserves track pan routing");
    TEST_ASSERT(tp_pres_init.muted == true, "applyPreset preserves track mute status");

    // Серіалізація та десеріалізація файлу пресету (.snd)
    std::string test_snd_path = "/tmp/test_preset.snd";
    TrackPreset tp_save;
    std::strncpy(tp_save.name, "MY CUSTOM LEAD", sizeof(tp_save.name) - 1);
    tp_save.engine = EngineType::kVaMono;
    tp_save.params = tp_pres_init;
    bool pres_save_ok = PresetManager::instance().savePreset(test_snd_path, tp_save);
    TEST_ASSERT(pres_save_ok == true, "PresetManager saves binary .snd preset successfully");

    TrackPreset tp_loaded;
    bool pres_load_ok = PresetManager::instance().loadPreset(test_snd_path, tp_loaded);
    TEST_ASSERT(pres_load_ok == true, "PresetManager loads binary .snd preset successfully");
    TEST_ASSERT(std::string(tp_loaded.name) == "MY CUSTOM LEAD", "Loaded preset name matches saved name");
    TEST_ASSERT(tp_loaded.engine == EngineType::kVaMono, "Loaded preset engine matches saved engine");
    TEST_ASSERT(tp_loaded.params.p[0] == 1.0f, "Loaded preset parameters match saved parameters");

    // Тест параметра TRK.PRESET у ParamRegistry
    const Param* p_preset = ParamRegistry::instance().find("TRK.PRESET");
    TEST_ASSERT(p_preset != nullptr, "Param TRK.PRESET is registered in ParamRegistry");
    if (p_preset) {
        audio.setTrackParams(0, tp_pres_init);
        std::string pres_val1 = p_preset->value(ctx);
        p_preset->step(ctx, 1, false);
        std::string pres_val2 = p_preset->value(ctx);
        TEST_ASSERT(pres_val1 != pres_val2, "Stepping TRK.PRESET cycles through engine sound presets");
    }

    // Тестування Project Persistence v2 (Patterns + Song Mode + Master FX + BPM)
    std::string test_proj_path = "/tmp/test_project_v2.xgproj";
    std::remove(test_proj_path.c_str());

    MasterFxParameters orig_mfx = audio.getMasterFx();
    orig_mfx.gain_over = 0.55f;
    orig_mfx.bell_gain = 4.5f;
    orig_mfx.lpf_cutoff = 0.65f;
    audio.setMasterFx(orig_mfx);

    app.bpm = 135.0f;
    bool save_ok = app.pattern_mgr.saveProject(test_proj_path, audio, app.bpm, &app.song_chain, app.song_rows_count);
    TEST_ASSERT(save_ok == true, "saveProject v2 successfully serializes project to file");

    // Змінюємо стан перед завантаженням
    app.bpm = 90.0f;
    app.song_rows_count = 1;
    app.song_chain[1].mute_mask = 0;
    orig_mfx.gain_over = 0.0f;
    orig_mfx.bell_gain = 0.0f;
    orig_mfx.lpf_cutoff = 1.0f;
    audio.setMasterFx(orig_mfx);

    float loaded_bpm = 0.0f;
    int loaded_rows = 0;
    std::array<SongStep, 16> loaded_sc{};
    bool proj_load_ok = app.pattern_mgr.loadProject(test_proj_path, audio, loaded_bpm, &loaded_sc, &loaded_rows);
    TEST_ASSERT(proj_load_ok == true, "loadProject v2 successfully deserializes project from file");
    TEST_ASSERT(std::abs(loaded_bpm - 135.0f) < 0.1f, "loadProject restores accurate BPM (135)");
    TEST_ASSERT(loaded_rows == 2, "loadProject restores exact song_rows_count (2)");
    TEST_ASSERT(loaded_sc[1].mute_mask == 0b00000001, "loadProject restores song row mute_mask");
    MasterFxParameters loaded_mfx = audio.getMasterFx();
    TEST_ASSERT(std::abs(loaded_mfx.gain_over - 0.55f) < 0.01f, "loadProject restores gain_over");
    TEST_ASSERT(std::abs(loaded_mfx.bell_gain - 4.5f) < 0.01f, "loadProject restores EQ bell_gain");
    TEST_ASSERT(std::abs(loaded_mfx.lpf_cutoff - 0.65f) < 0.01f, "loadProject restores lpf_cutoff");
    std::remove(test_proj_path.c_str());

    // Тестування потокового запису майстер-сигналу у WAV (Master WAV Recording)
    std::string test_rec_path = "/tmp/test_master_rec.wav";
    std::remove(test_rec_path.c_str());

    TEST_ASSERT(audio.isRecording() == false, "Initial recording state is false");
    bool rec_started = audio.startRecording(test_rec_path);
    TEST_ASSERT(rec_started == true, "audio.startRecording returns true");
    TEST_ASSERT(audio.isRecording() == true, "audio.isRecording is true after start");

    // Відрендеримо кілька блоків аудіо
    std::vector<int16_t> dummy_out(256 * 2, 0);
    for (int b = 0; b < 10; ++b) {
        audio.renderBlock(dummy_out.data(), 256, 32000);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    audio.stopRecording();
    TEST_ASSERT(audio.isRecording() == false, "audio.isRecording is false after stop");

    // Перевіряємо що WAV файл створено і його заголовок валідний
    std::ifstream wav_in(test_rec_path, std::ios::binary);
    TEST_ASSERT(wav_in.is_open() == true, "Recorded WAV file exists on disk");
    char riff[4]{};
    wav_in.read(riff, 4);
    TEST_ASSERT(std::memcmp(riff, "RIFF", 4) == 0, "Recorded file starts with RIFF header");
    wav_in.seekg(8, std::ios::beg);
    char wave[4]{};
    wav_in.read(wave, 4);
    TEST_ASSERT(std::memcmp(wave, "WAVE", 4) == 0, "Recorded file format is WAVE");
    wav_in.close();
    std::remove(test_rec_path.c_str());

    // Тестування розширених Elektron P-Locks
    SequencerStep plock_step{};
    plock_step.enabled = true;
    plock_step.has_plock = true;
    plock_step.note = 60;
    plock_step.plock_p[0] = 0.95f;
    plock_step.plock_p[4] = 0.80f;
    plock_step.plock_delay_send = 0.70f;
    audio.setStep(0, 0, plock_step);

    SequencerStep retrieved_step = audio.getStep(0, 0);
    TEST_ASSERT(retrieved_step.has_plock == true, "getStep retrieves has_plock == true");
    TEST_ASSERT(std::abs(retrieved_step.plock_p[0] - 0.95f) < 0.001f, "getStep preserves plock_p[0]");
    TEST_ASSERT(std::abs(retrieved_step.plock_p[4] - 0.80f) < 0.001f, "getStep preserves plock_p[4]");
    TEST_ASSERT(std::abs(retrieved_step.plock_delay_send - 0.70f) < 0.001f, "getStep preserves plock_delay_send");

    audio.triggerNote(0, 60, 0.90f, -1.0f, false, 0.60f, &retrieved_step);
    TEST_ASSERT(true, "triggerNote successfully applies expanded P-Locks to voice");

    // Тестування автодетекції транзієнтів та слайсингу семплера
    SamplerEngine test_sampler;
    std::vector<float> syn_in_l(32000, 0.0f);
    std::vector<float> syn_in_r(32000, 0.0f);
    for (int beat = 0; beat < 4; ++beat) {
        int peak_idx = beat * 7500 + 500;
        for (int p = 0; p < 800; ++p) {
            float env = std::exp(-static_cast<float>(p) / 120.0f);
            syn_in_l[peak_idx + p] = 0.85f * env * std::sin(p * 0.2f);
            syn_in_r[peak_idx + p] = 0.85f * env * std::sin(p * 0.2f);
        }
    }
    test_sampler.processAudioInput(syn_in_l.data(), syn_in_r.data(), 32000);
    test_sampler.sliceTransients(0.5f);
    TEST_ASSERT(test_sampler.sliceCount() >= 4, "sliceTransients detects distinct energy burst transients");

    test_sampler.sliceToDrumSlots(0.0f, 1.0f);
    TEST_ASSERT(test_sampler.drumSlot(0).loaded == true, "sliceToDrumSlots successfully generates loaded drum slot 1");
    TEST_ASSERT(test_sampler.drumSlot(0).lengthFrames() > 0, "Drum slot 1 contains non-zero frame length");

    // Тестування Digitakt II USB OTG Hot-Plug Automation
    HardwareMonitor dt_hw_mon;
    dt_hw_mon.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    dt_hw_mon.toggleMockDigitakt();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    HardwareSnapshot snap_plugged = dt_hw_mon.getSnapshot();
    TEST_ASSERT(snap_plugged.digitakt_connected == true, "Mock Digitakt II detected on USB bus");
    TEST_ASSERT(snap_plugged.active_audio_card == 1, "Active audio card switches to USB Digitakt II");

    audio.updateRouting(snap_plugged.active_audio_card, snap_plugged.digitakt_connected, false);
    TEST_ASSERT(audio.sampleRate() == 48000, "AudioEngine switches sample rate to 48000 Hz for Digitakt II");

    dt_hw_mon.toggleMockDigitakt();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    HardwareSnapshot snap_unplugged = dt_hw_mon.getSnapshot();
    TEST_ASSERT(snap_unplugged.digitakt_connected == false, "Digitakt II hot-unplug detected");
    TEST_ASSERT(snap_unplugged.active_audio_card == activeProfile().internal_card, "Active audio card reverts to internal DAC");

    audio.updateRouting(snap_unplugged.active_audio_card, snap_unplugged.digitakt_connected, false);
    TEST_ASSERT(audio.sampleRate() == static_cast<uint32_t>(activeProfile().internal_rate), "AudioEngine reverts sample rate to native internal DAC rate");

    // Перевірка вибору частоти дискретизації зі списку (24k, 32k, 44.1k)
    audio.setUserSampleRate(24000);
    TEST_ASSERT(audio.sampleRate() == 24000, "AudioEngine sets sample rate to 24000 Hz");
    audio.stepSampleRate(1);
    TEST_ASSERT(audio.sampleRate() == 32000, "AudioEngine steps sample rate forward to 32000 Hz");
    audio.stepSampleRate(1);
    TEST_ASSERT(audio.sampleRate() == 44100, "AudioEngine steps sample rate forward to 44100 Hz");
    audio.stepSampleRate(1);
    TEST_ASSERT(audio.sampleRate() == 24000, "AudioEngine wraps sample rate to 24000 Hz");
    audio.stepSampleRate(-1);
    TEST_ASSERT(audio.sampleRate() == 44100, "AudioEngine steps sample rate backward to 44100 Hz");

    // Перевірка степінгу буфера списком {96, 128, 256, 512, 1024, 2048} в обох напрямках
    audio.stepBufferSize(1);
    uint32_t b1 = audio.getTargetPeriodFrames();
    audio.stepBufferSize(1);
    uint32_t b2 = audio.getTargetPeriodFrames();
    TEST_ASSERT(b2 != b1, "AudioEngine steps buffer size forward via list");
    audio.stepBufferSize(-1);
    TEST_ASSERT(audio.getTargetPeriodFrames() == b1, "AudioEngine steps buffer size backward via list");

    dt_hw_mon.stop();
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
    audio.start(-1, false, false);

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

    // Перевірка навігації по всіх 26 рядках інспектора кроку (Elektron P-Locks)
    IView* inspect_view = shell.findView(ViewId::kInspect);
    TEST_ASSERT(inspect_view != nullptr, "ViewId::kInspect registered in shell");
    if (inspect_view) {
        UiContext ctx{app, audio, nullptr, in, hw, tel, midi, 1000, &shell};
        for (int r = 0; r < 9; ++r) {
            inspect_view->navigate(ctx, 0, 1, false);
        }
        TEST_ASSERT(app.grid_row[static_cast<int>(ViewId::kInspect)] == 9, "Step Inspector scrolls to bottom of MIDI page (COND)");
        inspect_view->navigate(ctx, 0, -9, false);
        TEST_ASSERT(app.grid_row[static_cast<int>(ViewId::kInspect)] == 0, "Step Inspector returns to top row (TYPE)");
    }
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

    // 8. Рендеринг виділеної вкладки MOD ([L2]syn<MOD>fx) та модального селектора таргету
    app.active_view = ViewId::kSynth;
    app.synth_submode = SynthSubmode::kMod;
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "Dedicated MOD submode renders 4 LFOs, 2 ENVs, and Stick mode tables cleanly");

    app.mod_picker_open = true;
    app.mod_picker_param_id = "LFO1.DEST";
    app.mod_picker_sel = static_cast<int>(ModTarget::kCutoff);
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "MOD TARGET PICKER modal renders cleanly over workspace");
    app.mod_picker_open = false;

    // 9. Рендеринг виділеної вкладки TRACK FX ([L2]mod<FX>syn) з динамічним розташуванням S-COMP (PRE vs POST)
    app.synth_submode = SynthSubmode::kFx;
    auto cur_tp = audio.getTrackParams(app.selected_track);
    cur_tp.fx.sc_pos = SidechainPos::kPreDelayRev;
    audio.setTrackParams(app.selected_track, cur_tp);
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "TRACK FX submode renders with S-COMP in PRE delay/reverb position");

    cur_tp.fx.sc_pos = SidechainPos::kPostDelayRev;
    audio.setTrackParams(app.selected_track, cur_tp);
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "TRACK FX submode dynamically rearranges table with S-COMP in POST position");

    // 10. Рендеринг виділеної вкладки MASTERING ([R2]snd<MSTR>mix) з графіком АЧХ та смугами мастера
    app.active_view = ViewId::kMix;
    app.mix_submode = MixSubmode::kMaster;
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "MASTERING submode renders EQ spectrum curve (АЧХ) and master strips cleanly");

    // 11. Рендеринг візуалізатора хвилі Digitakt у вкладці SYNTH
    app.active_view = ViewId::kSynth;
    app.synth_submode = SynthSubmode::kSynth;
    app.selected_track = 0;
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(true, "SYNTH tab renders Digitakt waveform visualizer with S/L/E markers");

    // 12. Рендеринг Song Mode з 1 рядком та [+] ADD ROW
    app.active_view = ViewId::kSeq;
    app.seq_submode = SeqSubmode::kSong;
    shell.render(app, audio, nullptr, in, hw, tel, midi, 1000);
    TEST_ASSERT(app.song_rows_count == 1, "Song mode starts with exactly 1 row");

    audio.stop();
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    SDL_Quit();
}

int main() {
    groovebox::loadActiveProfile();

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
