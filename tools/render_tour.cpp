#include "audio_engine.hpp"
#include "config.hpp"
#include "hw_monitor.hpp"
#include "input_manager.hpp"
#include "midi_engine.hpp"
#include "synth_engines.hpp"
#include "ui_renderer.hpp"

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

    UiRenderer ui(sw_renderer, w, h);

    AudioEngine audio_engine;
    AppModel app;
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

    // 1. SEQ Tab
    app.active_tab = WorkspaceTab::kSeq;
    ui.render(app, hw, input, audio_engine, audio, midi, 0.75f);
    capture("/tmp/xu10_tour/tour_01_seq.bmp");

    // 2. SYNTH Tab (Cyan tab)
    app.active_tab = WorkspaceTab::kSynth;
    app.selected_param = 0;
    ui.render(app, hw, input, audio_engine, audio, midi, 0.75f);
    capture("/tmp/xu10_tour/tour_02_synth.bmp");

    // 3. FX Tab
    app.active_tab = WorkspaceTab::kFx;
    ui.render(app, hw, input, audio_engine, audio, midi, 0.75f);
    capture("/tmp/xu10_tour/tour_03_fx.bmp");

    // 4. MOD Tab (Dual LFO & Mini-Scope)
    app.active_tab = WorkspaceTab::kMod;
    ui.render(app, hw, input, audio_engine, audio, midi, 0.75f);
    capture("/tmp/xu10_tour/tour_04_mod.bmp");

    // 5. MIX Tab
    app.active_tab = WorkspaceTab::kMixFx;
    ui.render(app, hw, input, audio_engine, audio, midi, 0.75f);
    capture("/tmp/xu10_tour/tour_05_mix.bmp");

    // 6. SYS Tab
    app.active_tab = WorkspaceTab::kSys;
    app.sys_menu_cursor = 1; // Highlight USB Port Role
    ui.render(app, hw, input, audio_engine, audio, midi, 0.75f);
    capture("/tmp/xu10_tour/tour_06_sys.bmp");

    // 7. Step Inspector
    app.active_tab = WorkspaceTab::kSeq;
    app.step_inspector_open = true;
    app.cursor_row = 0;
    app.cursor_step = 4;
    ui.render(app, hw, input, audio_engine, audio, midi, 0.75f);
    capture("/tmp/xu10_tour/tour_07_inspector.bmp");

    SDL_DestroyRenderer(sw_renderer);
    SDL_FreeSurface(surf);
    SDL_Quit();

    std::printf("All screenshots generated successfully!\n");
    return 0;
}
