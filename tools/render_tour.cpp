#include "audio_engine.hpp"
#include "config.hpp"
#include "hw_monitor.hpp"
#include "input_manager.hpp"
#include "midi_engine.hpp"
#include "synth_engines.hpp"
#include "ui/ui_shell.hpp"

#include <SDL.h>
#include <cstdio>
#include <string>

using namespace groovebox;

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int w = kHighResWidth;  // 320
    const int h = kHighResHeight; // 240

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        std::fprintf(stderr, "CreateRGBSurface failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* sw_renderer = SDL_CreateSoftwareRenderer(surf);
    if (!sw_renderer) {
        std::fprintf(stderr, "CreateSoftwareRenderer failed: %s\n", SDL_GetError());
        return 1;
    }

    UiShell ui(sw_renderer, w, h);
    setupAppLayout(ui);

    AudioEngine audio_engine;
    AppModel app;
    audio_engine.setSampler(&app.sampler);
    HardwareSnapshot hw;
    hw.cpu_load_percent = 7.1f;
    hw.cpu_temp_c = 42;
    hw.ram_used_mb = 47;
    hw.ram_total_mb = 1024;
    hw.usb_role = "device_audio";

    InputState input;
    AudioEngineTelemetry audio;
    audio.sample_rate = 32000;
    audio.period_frames = 128;
    audio.vu_left = 0.65f;
    audio.vu_right = 0.58f;
    audio.peak_hold = 0.72f;
    audio.track_levels = {0.60f, 0.45f, 0.70f, 0.30f};
    for (int i = 0; i < 256; ++i) {
        float t = static_cast<float>(i) / 256.0f;
        audio.scope_samples[i] = std::sin(t * 6.2831853f * 3.0f) * 0.65f + std::sin(t * 6.2831853f * 7.0f) * 0.25f;
    }

    MidiTelemetry midi;
    midi.external_sync = false;

    auto capture = [&](const char* path) {
        SDL_SaveBMP(surf, path);
        std::printf("Saved: %s\n", path);
    };

    // 1. SEQ Tab (Overview)
    app.active_view = ViewId::kSeq;
    app.rec_mode = false;
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_01_seq.bmp");

    // 1b. SEQ Tab (REC Mode: Chromatic Sampler Piano Roll)
    app.rec_mode = true;
    app.selected_track = 0;
    auto tp0 = audio_engine.getTrackParams(0);
    tp0.engine = EngineType::kSampleOneShot;
    audio_engine.setTrackParams(0, tp0);
    app.rec_cursor_lane = 11; // C
    app.rec_octave = 2;
    app.cursor_step = 4;
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_01_rec_sampler.bmp");

    // 1c. SEQ Tab (REC Mode: Melodic Synth Piano Roll)
    app.selected_track = 1; // 303
    app.rec_cursor_lane = 8; // D#
    app.rec_octave = 3;
    app.cursor_step = 2;
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_01_rec_piano.bmp");
    app.rec_mode = false;

    // 2. SYNTH Tab (Cyan tab)
    app.active_view = ViewId::kSynth;
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_02_synth.bmp");

    // 3. FX Tab
    app.active_view = ViewId::kFx;
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_03_fx.bmp");

    // 4. MIX Tab
    app.active_view = ViewId::kMix;
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_04_mix.bmp");

    // 5. SAMPLE Tab (Sampling Workstation with 8 Slices & Active S3 Highlight)
    app.active_view = ViewId::kSample;
    app.sampler.setActivePlaybackSlice(2); // S3 highlighted
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_05_sample.bmp");
    capture("/tmp/xu10_tour/tour_05_scope.bmp");
    app.sampler.setActivePlaybackSlice(-1);

    // 6. SYS Tab
    app.active_view = ViewId::kSys;
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_06_sys.bmp");

    // 7. Step Inspector Modal
    app.active_view = ViewId::kSeq;
    app.step_inspector_open = true;
    app.cursor_row = 0;
    app.cursor_step = 4;
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_07_inspector.bmp");
    app.step_inspector_open = false;

    // 8. Quick Mute Layer in SEQ (holding L2)
    app.active_view = ViewId::kSeq;
    input = InputState{};
    input.l2 = true;
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_08_quick_mute.bmp");

    // 9. SELECT Layer in SEQ (holding SELECT - non-overlapping tab bar!)
    app.active_view = ViewId::kSeq;
    input = InputState{};
    input.btn_select = true;
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_09_select_layer.bmp");

    // 10. Performance Mode (Live Playing, Pad A pressed)
    app.active_view = ViewId::kPerf;
    app.selected_track = 1; // VA Synth
    input = InputState{};
    input.btn_a = true;
    input.lx = 0.25f;
    input.ly = -0.40f;
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_10_perf.bmp");

    // 11. Dual Arpeggiator & Procedural Generator
    app.active_view = ViewId::kArp;
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_11_arp.bmp");

    // 12. DX7 FM Engine View (Track 3)
    app.active_view = ViewId::kSynth;
    app.selected_track = 2; // T3 DX7
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_12_dx7.bmp");

    // 13. Engine Selection Modal Dropdown
    app.engine_menu_open = true;
    app.engine_menu_sel = 6; // SAMPLER
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_13_engine_menu.bmp");
    app.engine_menu_open = false;

    // 14. Sample Browser Modal (Sample Pool & Filesystem)
    app.sample_browser_open = true;
    app.sample_browser_dir = "res/samples/drums/01_tr808";
    app.sample_browser_items = {"[..] PARENT DIR", "[DIR] 01_tr808", "bd.wav", "sd.wav", "ch.wav", "oh.wav", "cp.wav", "rim.wav"};
    app.sample_browser_sel = 2; // bd.wav
    input = InputState{};
    ui.render(app, audio_engine, nullptr, input, hw, audio, midi, 1000);
    capture("/tmp/xu10_tour/tour_14_sample_browser.bmp");
    app.sample_browser_open = false;

    SDL_DestroyRenderer(sw_renderer);
    SDL_FreeSurface(surf);
    SDL_Quit();

    std::printf("All 14 tour screenshots generated successfully!\n");
    return 0;
}
