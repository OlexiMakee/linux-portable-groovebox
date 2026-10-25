#include "ui_renderer.hpp"

#include <SDL.h>
#include <cmath>
#include <cstdio>

namespace groovebox {

namespace {

// Компактний 3x5 піксельний растровий шрифт (кожен символ — 15 бітів, 5 рядків по 3 біти)
// Займає 4x6 пікселів з інтервалом -> 40 символів у рядку на 160x120!
constexpr uint16_t kFont3x5[96] = {
    0x0000, // ' '
    0x2492, // '!'
    0x002D, // '"'
    0x5F7D, // '#'
    0x279E, // '$'
    0x52A5, // '%'
    0x6CA9, // '&'
    0x0012, // '''
    0x4494, // '('
    0x1491, // ')'
    0x0550, // '*'
    0x05D0, // '+'
    0x1400, // ','
    0x01C0, // '-'
    0x0800, // '.'
    0x12A4, // '/'
    0x7B6F, // '0'
    0x749A, // '1'
    0x73E7, // '2'
    0x79E7, // '3'
    0x49ED, // '4'
    0x79CF, // '5'
    0x7BCF, // '6'
    0x4927, // '7'
    0x7BEF, // '8'
    0x79EF, // '9'
    0x0410, // ':'
    0x1410, // ';'
    0x4454, // '<'
    0x1C70, // '='
    0x1511, // '>'
    0x21A7, // '?'
    0x73EF, // '@'
    0x5BEF, // 'A'
    0x3BEB, // 'B'
    0x724F, // 'C'
    0x3B6B, // 'D'
    0x73CF, // 'E'
    0x13CF, // 'F'
    0x7B4F, // 'G'
    0x5BED, // 'H'
    0x7497, // 'I'
    0x7B24, // 'J'
    0x5AED, // 'K'
    0x7249, // 'L'
    0x5B7D, // 'M'
    0x5B6F, // 'N'
    0x7B6F, // 'O'
    0x13EF, // 'P'
    0x6B6F, // 'Q'
    0x5AEF, // 'R'
    0x79CF, // 'S'
    0x2497, // 'T'
    0x7B6D, // 'U'
    0x2B6D, // 'V'
    0x5F6D, // 'W'
    0x5AAD, // 'X'
    0x24AD, // 'Y'
    0x72A7, // 'Z'
    0x6496, // '['
    0x4291, // '\'
    0x3493, // ']'
    0x002A, // '^'
    0x7000, // '_'
    0x0011, // '`'
    0x5BEF, // 'a' -> uppercase for clean tracker readability
    0x3BEB, // 'b'
    0x724F, // 'c'
    0x3B6B, // 'd'
    0x73CF, // 'e'
    0x13CF, // 'f'
    0x7B4F, // 'g'
    0x5BED, // 'h'
    0x7497, // 'i'
    0x7B24, // 'j'
    0x5AED, // 'k'
    0x7249, // 'l'
    0x5B7D, // 'm'
    0x5B6F, // 'n'
    0x7B6F, // 'o'
    0x13EF, // 'p'
    0x6B6F, // 'q'
    0x5AEF, // 'r'
    0x79CF, // 's'
    0x2497, // 't'
    0x7B6D, // 'u'
    0x2B6D, // 'v'
    0x5F6D, // 'w'
    0x5AAD, // 'x'
    0x24AD, // 'y'
    0x72A7, // 'z'
    0x64D6, // '{'
    0x2492, // '|'
    0x3593, // '}'
    0x0360, // '~'
    0x7FFF  // DEL
};

} // namespace

UiRenderer::UiRenderer() = default;

UiRenderer::~UiRenderer() {
    shutdown();
}

bool UiRenderer::init() {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Nearest-neighbor integer scaling

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    uint32_t win_flags = SDL_WINDOW_SHOWN;
#if defined(__linux__)
    win_flags |= SDL_WINDOW_FULLSCREEN;
#endif

    window_ = SDL_CreateWindow(
        "XU10 Groovebox [160x120 / 320x240]",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        kPhysicalWidth,
        kPhysicalHeight,
        win_flags
    );
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_ShowCursor(SDL_DISABLE);
    applyLogicalResolution(true); // Стартуємо у 320x240 з високою щільністю
    return true;
}

void UiRenderer::shutdown() {
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void UiRenderer::applyLogicalResolution(bool high_res) {
    logical_w_ = high_res ? kHighResWidth : kLowResWidth;
    logical_h_ = high_res ? kHighResHeight : kLowResHeight;
    if (renderer_) {
        SDL_RenderSetLogicalSize(renderer_, logical_w_, logical_h_);
    }
}

void UiRenderer::setColor(ColorRGB c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 255);
}

void UiRenderer::fillRect(int x, int y, int w, int h, ColorRGB c) {
    setColor(c);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(renderer_, &r);
}

void UiRenderer::drawRect(int x, int y, int w, int h, ColorRGB c) {
    setColor(c);
    SDL_Rect r{x, y, w, h};
    SDL_RenderDrawRect(renderer_, &r);
}

void UiRenderer::drawLine(int x1, int y1, int x2, int y2, ColorRGB c) {
    setColor(c);
    SDL_RenderDrawLine(renderer_, x1, y1, x2, y2);
}

void UiRenderer::drawChar(int x, int y, char ch, ColorRGB c, int scale) {
    int idx = static_cast<unsigned char>(ch) - 32;
    if (idx < 0 || idx >= 96) idx = 0;
    uint16_t bits = kFont3x5[idx];
    if (bits == 0) return;

    setColor(c);
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if ((bits >> (row * 3 + col)) & 1) {
                if (scale == 1) {
                    SDL_RenderDrawPoint(renderer_, x + col, y + row);
                } else {
                    SDL_Rect r{x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer_, &r);
                }
            }
        }
    }
}

void UiRenderer::drawText(int x, int y, const std::string& text, ColorRGB c, int scale) {
    int cx = x;
    const int step = 4 * scale;
    for (char ch : text) {
        drawChar(cx, y, ch, c, scale);
        cx += step;
    }
}

void UiRenderer::renderTopBar(const AppModel& app, const InputState& input, int w, int s) {
    (void)s;
    const int bar_h = 16;
    fillRect(0, 0, w, bar_h, palette::kPanelBg);
    drawLine(0, bar_h - 1, w - 1, bar_h - 1, palette::kGridDim);

    // 4 вкладки верхнього бара зліва направо: L1, L2, R2, R1
    const char* labels_320[4] = {"[L1] HW/SYS", "[L2] SYNTH", "[R2] MIX/FX", "[R1] SCOPE"};
    const char* labels_160[4] = {"L1:SYS", "L2:SYN", "R2:MIX", "R1:SCP"};
    const bool held[4] = {input.l1, input.l2, input.r2, input.r1};
    const int tab_w = w / 4;

    for (int i = 0; i < 4; ++i) {
        const int tx = i * tab_w;
        const bool is_active = (static_cast<int>(app.active_tab) == i);
        const char* lbl = (w >= 320) ? labels_320[i] : labels_160[i];
        const int text_x = tx + (tab_w - static_cast<int>(std::strlen(lbl)) * 4) / 2;
        if (is_active) {
            fillRect(tx + 1, 1, tab_w - 2, bar_h - 3, palette::kAccentOrange);
            drawText(text_x, 5, lbl, palette::kBgDark, 1);
        } else if (held[i]) {
            fillRect(tx + 1, 1, tab_w - 2, bar_h - 3, palette::kAccentCyan);
            drawText(text_x, 5, lbl, palette::kBgDark, 1);
        } else {
            drawRect(tx + 1, 1, tab_w - 2, bar_h - 3, palette::kGridDim);
            drawText(text_x, 5, lbl, palette::kTextMain, 1);
        }
    }
}

void UiRenderer::renderWorkspace(
    const AppModel& app,
    const HardwareSnapshot& hw,
    const InputState& input,
    const AudioEngine& audio_engine,
    const AudioEngineTelemetry& audio,
    const MidiTelemetry& midi,
    float master_vol,
    int w,
    int h,
    int s
) {
    (void)s;
    const int top_y = 18;
    const int bot_y = h - 30;
    const int ws_h = bot_y - top_y;

    // Рамка активного воркспейсу
    drawRect(2, top_y, w - 4, ws_h, palette::kGridDim);

    char buf[128];

    if (app.active_tab == WorkspaceTab::kHwSys) {
        // Вкладка L1: Апаратний монітор, аудіо-роутинг, MIDI Clock та тест кнопок
        ColorRGB route_col = hw.digitakt_connected ? palette::kLevelOk : palette::kAccentOrange;
        std::snprintf(
            buf, sizeof(buf), "AUDIO OUT : %s | VOL:%3.0f%% | XRUN:%u",
            audio.device_label.c_str(),
            master_vol * 100.0f,
            audio.xrun_count
        );
        drawText(8, top_y + 6, buf, route_col, 1);

        std::snprintf(
            buf, sizeof(buf), "MIDI SYNC : %s | BPM:%3.0f",
            midi.port_name.c_str(),
            midi.external_sync ? midi.detected_bpm : app.bpm
        );
        drawText(8, top_y + 15, buf, palette::kAccentCyan, 1);

        std::snprintf(
            buf, sizeof(buf), "SYSTEM    : CPU %2.0f%% (%dC) | RAM %d / %d MB",
            hw.cpu_load_percent, hw.cpu_temp_c, hw.ram_used_mb, hw.ram_total_mb
        );
        drawText(8, top_y + 24, buf, palette::kTextMain, 1);

        const bool is_host = (hw.usb_role == "host");
        std::snprintf(
            buf, sizeof(buf), "USB PORT  : %s [FN:SWITCH]",
            is_host ? "HOST (5V ON: AUDIO/MIDI)" : "DEVICE (MAC SSH 10.1.1.2)"
        );
        drawText(8, top_y + 33, buf, is_host ? palette::kLevelOk : palette::kAccentCyan, 1);

        // Інтерактивна матриця перевірки фізичних кнопок у воркспейсі
        const int vy = top_y + 45;
        drawLine(6, vy - 4, w - 6, vy - 4, palette::kGridDim);

        auto drawBadge = [&](int bx, int by, const char* name, bool on) {
            int bw = static_cast<int>(std::strlen(name)) * 4 + 6;
            int bh = 11;
            if (on) {
                fillRect(bx, by, bw, bh, palette::kLevelOk);
                drawText(bx + 3, by + 3, name, palette::kBgDark, 1);
            } else {
                drawRect(bx, by, bw, bh, palette::kGridDim);
                drawText(bx + 3, by + 3, name, palette::kTextDim, 1);
            }
        };

        drawBadge(8,   vy, "UP",   input.dpad_up);
        drawBadge(26,  vy, "DN",   input.dpad_down);
        drawBadge(44,  vy, "LT",   input.dpad_left);
        drawBadge(62,  vy, "RT",   input.dpad_right);
        drawBadge(84,  vy, "SEL",  input.btn_select);
        drawBadge(106, vy, "STA",  input.btn_start);
        drawBadge(132, vy, "A",    input.btn_a);
        drawBadge(146, vy, "B",    input.btn_b);
        drawBadge(160, vy, "X",    input.btn_x);
        drawBadge(174, vy, "Y",    input.btn_y);
        drawBadge(192, vy, "L1",   input.l1);
        drawBadge(210, vy, "L2",   input.l2);
        drawBadge(228, vy, "R2",   input.r2);
        drawBadge(246, vy, "R1",   input.r1);
        drawBadge(268, vy, "V+",   input.vol_up);
        drawBadge(286, vy, "V-",   input.vol_down);

        // 2D-приціли аналогових стіків + реальний осцилограф аудіовиходу та VU-метр
        const int sy = vy + 16;
        const int box = (w >= 320) ? 56 : 26;
        drawRect(8, sy, box, box, palette::kGridDim);
        drawText(10, sy + 2, "L-STICK", palette::kTextDim, 1);
        int lpx = 8 + box / 2 + static_cast<int>(input.lx * (box / 2 - 3));
        int lpy = sy + box / 2 + static_cast<int>(input.ly * (box / 2 - 3));
        fillRect(lpx - 2, lpy - 2, 5, 5, palette::kAccentCyan);

        const int rbox_x = 8 + box + 6;
        drawRect(rbox_x, sy, box, box, palette::kGridDim);
        drawText(rbox_x + 2, sy + 2, "R-STICK", palette::kTextDim, 1);
        int rpx = rbox_x + box / 2 + static_cast<int>(input.rx * (box / 2 - 3));
        int rpy = sy + box / 2 + static_cast<int>(input.ry * (box / 2 - 3));
        fillRect(rpx - 2, rpy - 2, 5, 5, palette::kAccentOrange);

        // Реальний осцилограф звукової хвилі праворуч у воркспейсі
        const int sc_x = rbox_x + box + 8;
        const int sc_w = w - sc_x - 8;
        const int sc_h = box;
        if (sc_w > 20) {
            drawRect(sc_x, sy, sc_w, sc_h, palette::kGridDim);
            drawText(sc_x + 4, sy + 2, "LIVE AUDIO SCOPE", palette::kTextDim, 1);
            int prev_y = sy + sc_h / 2;
            for (int px = 1; px < sc_w - 1; ++px) {
                uint32_t s_idx = (static_cast<uint32_t>(px) * 255u) / static_cast<uint32_t>(sc_w);
                float v = audio.scope_samples[s_idx];
                int cur_y = sy + (sc_h / 2) - static_cast<int>(v * (sc_h / 2 - 4));
                cur_y = std::clamp(cur_y, sy + 2, sy + sc_h - 2);
                drawLine(sc_x + px - 1, prev_y, sc_x + px, cur_y, palette::kAccentCyan);
                prev_y = cur_y;
            }
        }

        // Смуга динамічного рівня реального сигналу (VU-метр) внизу воркспейсу
        const int vu_y = sy + box + 6;
        if (vu_y + 10 < bot_y) {
            drawText(8, vu_y + 2, "OUT VU :", palette::kTextDim, 1);
            const int vu_x = 44;
            const int vu_w = w - vu_x - 8;
            drawRect(vu_x, vu_y, vu_w, 9, palette::kGridDim);
            float vu_val = std::max(audio.vu_left, audio.vu_right);
            int fill_w = static_cast<int>(std::clamp(vu_val * 1.25f, 0.0f, 1.0f) * (vu_w - 2));
            ColorRGB vu_col = (fill_w > (vu_w * 8) / 10) ? palette::kLevelWarnClip : palette::kLevelOk;
            if (fill_w > 0) {
                fillRect(vu_x + 1, vu_y + 1, fill_w, 7, vu_col);
            }
        }

    } else if (app.active_tab == WorkspaceTab::kSynth) {
        // Вкладка L2: Модульний редактор движка та 6 параметрів обраної доріжки!
        // Кнопки A/B/X/Y обирають і тригерять TRK 1..4, Up/Down обирає параметр, Left/Right змінює значення!
        const auto tp = audio_engine.getTrackParams(app.selected_track);
        std::snprintf(
            buf, sizeof(buf), "TRACK %d/4 SYNTH EDITOR  |  [A/B/X/Y = SELECT TRACK]",
            app.selected_track + 1
        );
        drawText(8, top_y + 6, buf, palette::kAccentOrange, 1);

        // Рядок 0: Вибір модульного движка (ENGINE TYPE)
        const bool eng_sel = (app.selected_param == 0);
        if (eng_sel) {
            fillRect(6, top_y + 18, w - 12, 13, palette::kPanelBg);
            drawRect(6, top_y + 18, w - 12, 13, palette::kAccentOrange);
        }
        std::snprintf(
            buf, sizeof(buf), "%s ENGINE TYPE : < %s >",
            eng_sel ? ">" : " ",
            getEngineTypeName(tp.engine)
        );
        drawText(10, top_y + 22, buf, eng_sel ? palette::kAccentCyan : palette::kTextMain, 1);

        // Рядки 1..6: 6 динамічних параметрів обраного движка з графічними слайдерами
        for (int p = 0; p < kParamsPerTrack; ++p) {
            const int py = top_y + 36 + p * 22;
            const bool is_sel = (app.selected_param == p + 1);
            const char* pname = getParamName(tp.engine, p);
            const float pval = tp.p[p];

            if (is_sel) {
                fillRect(6, py - 2, w - 12, 18, palette::kPanelBg);
                drawRect(6, py - 2, w - 12, 18, palette::kAccentOrange);
            }

            std::snprintf(buf, sizeof(buf), "%s %-8s %3.0f%%", is_sel ? ">" : " ", pname, pval * 100.0f);
            drawText(10, py + 4, buf, is_sel ? palette::kAccentOrange : palette::kTextMain, 1);

            const int bar_x = 92;
            const int bar_w = w - bar_x - 14;
            drawRect(bar_x, py + 2, bar_w, 10, palette::kGridDim);
            int fill_w = static_cast<int>(std::clamp(pval, 0.0f, 1.0f) * (bar_w - 2));
            if (fill_w > 0) {
                fillRect(bar_x + 1, py + 3, fill_w, 8, is_sel ? palette::kAccentCyan : palette::kLevelOk);
            }
        }

    } else if (app.active_tab == WorkspaceTab::kMixFx) {
        // Вкладка R2: 4-канальний мікшер, Delay Send, Mute та індивідуальні VU-метри
        std::snprintf(buf, sizeof(buf), "4-TRACK MIXER & STEREO PING-PONG DELAY | VOL:%3.0f%%", master_vol * 100.0f);
        drawText(8, top_y + 6, buf, palette::kAccentOrange, 1);

        for (int t = 0; t < 4; ++t) {
            const auto tp = audio_engine.getTrackParams(t);
            const int by = top_y + 22 + t * 38;
            const bool sel = (app.selected_track == t);
            if (sel) {
                drawRect(6, by - 3, w - 12, 34, palette::kAccentOrange);
            }

            std::snprintf(
                buf, sizeof(buf), "%sTRK %d [%-11s] %s LVL:%3.0f%% DLY:%2.0f%%",
                sel ? ">" : " ",
                t + 1,
                getEngineTypeName(tp.engine),
                tp.muted ? "[MUTE]" : "[ON]  ",
                tp.level * 100.0f,
                tp.delay_send * 100.0f
            );
            drawText(10, by + 2, buf, tp.muted ? palette::kLevelWarnClip : (sel ? palette::kAccentCyan : palette::kTextMain), 1);

            const int bar_w = w - 24;
            drawRect(12, by + 13, bar_w, 11, palette::kGridDim);
            int lvl = static_cast<int>(std::clamp(audio.track_levels[t] * 1.4f, 0.0f, 1.0f) * (bar_w - 2));
            if (lvl > 0 && !tp.muted) {
                fillRect(13, by + 14, lvl, 9, (lvl > (bar_w * 8) / 10) ? palette::kLevelWarnClip : palette::kLevelOk);
            }
        }
    } else {
        // R1: Повноекранний осцилограф реального звуку + інтерактивний 16-кроковий секвенсор із P-Locks!
        const auto tp = audio_engine.getTrackParams(app.selected_track);
        const auto cur_st = audio_engine.getStep(app.selected_track, app.cursor_step);
        static constexpr const char* kNoteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        int oct = (cur_st.note / 12) - 1;
        const char* nn = kNoteNames[cur_st.note % 12];

        std::snprintf(
            buf, sizeof(buf), "TRK %d [%s] | STEP %02d:%s%d %s | BPM:%3.0f %s | XRUN:%u",
            app.selected_track + 1,
            getEngineTypeName(tp.engine),
            app.cursor_step + 1,
            nn, oct,
            cur_st.has_plock ? "[P-LOCK]" : "",
            app.bpm,
            app.transport_playing ? "PLAY" : "STOP",
            audio.xrun_count
        );
        drawText(8, top_y + 6, buf, palette::kAccentOrange, 1);

        const int sc_y = top_y + 18;
        const int sc_h = ws_h - 54;
        drawRect(8, sc_y, w - 16, sc_h, palette::kGridDim);
        int prev_y = sc_y + sc_h / 2;
        for (int px = 1; px < w - 18; ++px) {
            uint32_t s_idx = (static_cast<uint32_t>(px) * 255u) / static_cast<uint32_t>(w - 18);
            float v = audio.scope_samples[s_idx];
            int cur_y = sc_y + (sc_h / 2) - static_cast<int>(v * (sc_h / 2 - 4));
            cur_y = std::clamp(cur_y, sc_y + 2, sc_y + sc_h - 2);
            drawLine(8 + px - 1, prev_y, 8 + px, cur_y, palette::kAccentCyan);
            prev_y = cur_y;
        }

        // 16 кроків секвенсора внизу воркспейсу (відображають активні кроки, P-Locks, курсор і Playhead)
        const int seq_y = sc_y + sc_h + 6;
        const int step_w = (w - 16) / 16;
        for (int st = 0; st < 16; ++st) {
            int sx = 8 + st * step_w;
            auto step_data = audio_engine.getStep(app.selected_track, st);
            bool is_playhead = app.transport_playing && (static_cast<int>(app.current_step % 16) == st);
            bool is_cursor = (app.cursor_step == st);

            if (is_playhead) {
                fillRect(sx, seq_y, step_w - 2, 22, palette::kLevelOk);
            } else if (step_data.enabled) {
                fillRect(sx, seq_y, step_w - 2, 22, step_data.has_plock ? palette::kAccentCyan : palette::kAccentOrange);
            } else {
                drawRect(sx, seq_y, step_w - 2, 22, (st % 4 == 0) ? palette::kTextMain : palette::kGridDim);
            }

            if (is_cursor) {
                drawRect(sx - 1, seq_y - 2, step_w, 26, palette::kTextMain);
            }
        }
    }
}

void UiRenderer::renderBottomHud(
    const AppModel& app,
    const HardwareSnapshot& hw,
    const InputState& input,
    int w,
    int h,
    int s
) {
    (void)hw;
    (void)s;
    const int hud_h = 28;
    const int hud_y = h - hud_h;

    fillRect(0, hud_y, w, hud_h, palette::kPanelBg);
    drawLine(0, hud_y, w - 1, hud_y, palette::kGridDim);

    // Розбиття нижнього бара строго в межах [0 .. w-1]:
    // 1. Лівий нижній кут (0 .. 25% w): підказки дій на Хрестовині (D-Pad)
    // 2. Середина (25% w .. 75% w): L-STICK | SELECT & START | R-STICK
    // 3. Правий нижній кут (75% w .. 100% w): підказки дій на ABXY
    const int x_mid = w / 4;
    const int x_right = (w * 3) / 4;
    const int mid_w = x_right - x_mid;

    drawLine(x_mid, hud_y, x_mid, h - 1, palette::kGridDim);
    drawLine(x_right, hud_y, x_right, h - 1, palette::kGridDim);

    // --- 1. Лівий нижній кут: D-PAD (контекстні підказки залежно від вкладки) ---
    ColorRGB dpad_col = (input.dpad_up || input.dpad_down || input.dpad_left || input.dpad_right)
                            ? palette::kLevelOk
                            : palette::kAccentOrange;
    drawText(4, hud_y + 3, "DPAD: EDIT", dpad_col, 1);
    if (app.active_tab == WorkspaceTab::kSynth) {
        drawText(4, hud_y + 11, "U/D : PARAM", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "L/R : VALUE", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kMixFx) {
        drawText(4, hud_y + 11, "U/D : TRACK", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "L/R : LEVEL", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kScopeSeq) {
        drawText(4, hud_y + 11, "L/R : STEP", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "U/D : NOTE", palette::kTextDim, 1);
    } else {
        drawText(4, hud_y + 11, "U/D : TRACK", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "L/R : BPM", palette::kTextDim, 1);
    }

    // --- 2. Центр: L-STICK | SEL & STA | R-STICK ---
    char lbuf[24], rbuf[24];
    std::snprintf(lbuf, sizeof(lbuf), "X%+4.1f Y%+4.1f", input.lx, input.ly);
    std::snprintf(rbuf, sizeof(rbuf), "X%+4.1f Y%+4.1f", input.rx, input.ry);

    const int col_w = mid_w / 3;
    const int lx_pos = x_mid + 4;
    const int cx_pos = x_mid + col_w + 4;
    const int rx_pos = x_mid + col_w * 2 + 4;

    drawLine(x_mid + col_w, hud_y + 2, x_mid + col_w, h - 2, palette::kGridDim);
    drawLine(x_mid + col_w * 2, hud_y + 2, x_mid + col_w * 2, h - 2, palette::kGridDim);

    // Лівий стік
    drawText(lx_pos, hud_y + 3,  "L-STK:FILT", palette::kAccentCyan, 1);
    drawText(lx_pos, hud_y + 11, lbuf, palette::kTextMain, 1);
    drawText(lx_pos, hud_y + 19, input.l3 ? "[L3 PRESSED]" : "CUTOFF / ENV", palette::kTextDim, 1);

    // Самий центр: SELECT та START
    drawText(cx_pos, hud_y + 3,  "SEL / START", palette::kAccentOrange, 1);
    drawText(cx_pos, hud_y + 11, input.btn_select ? "SEL:[SHIFT]" : "SEL:SHIFT", input.btn_select ? palette::kLevelOk : palette::kTextMain, 1);
    drawText(cx_pos, hud_y + 19, app.transport_playing ? "STA:[PLAY]" : "STA:STOP", app.transport_playing ? palette::kLevelOk : palette::kTextDim, 1);

    // Правий стік
    drawText(rx_pos, hud_y + 3,  "R-STK:MOD", palette::kAccentCyan, 1);
    drawText(rx_pos, hud_y + 11, rbuf, palette::kTextMain, 1);
    drawText(rx_pos, hud_y + 19, input.r3 ? "[R3 PRESSED]" : "RESO / FM", palette::kTextDim, 1);

    // --- 3. Правий нижній кут: ABXY ---
    ColorRGB abxy_col = (input.btn_a || input.btn_b || input.btn_x || input.btn_y)
                            ? palette::kLevelOk
                            : palette::kAccentOrange;
    if (app.active_tab == WorkspaceTab::kMixFx) {
        drawText(x_right + 4, hud_y + 3,  "ABXY: MIX/FX", abxy_col, 1);
        drawText(x_right + 4, hud_y + 11, "A:MUTE B:DLY+", palette::kTextMain, 1);
        drawText(x_right + 4, hud_y + 19, "X:DLY- Y:FBK", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kScopeSeq) {
        drawText(x_right + 4, hud_y + 3,  "ABXY: SEQ", abxy_col, 1);
        drawText(x_right + 4, hud_y + 11, "A:STEP B:PLCK", palette::kTextMain, 1);
        drawText(x_right + 4, hud_y + 19, "X:TRK- Y:TRK+", palette::kTextDim, 1);
    } else {
        drawText(x_right + 4, hud_y + 3,  "ABXY: TRIG", abxy_col, 1);
        drawText(x_right + 4, hud_y + 11, "A:T1  B:T2", palette::kTextMain, 1);
        drawText(x_right + 4, hud_y + 19, "X:T3  Y:T4", palette::kTextDim, 1);
    }
}

void UiRenderer::render(
    const AppModel& app,
    const HardwareSnapshot& hw,
    const InputState& input,
    const AudioEngine& audio_engine,
    const AudioEngineTelemetry& audio,
    const MidiTelemetry& midi,
    float master_vol
) {
    if (!renderer_) return;

    fillRect(0, 0, logical_w_, logical_h_, palette::kBgDark);

    renderTopBar(app, input, logical_w_, 1);
    renderWorkspace(app, hw, input, audio_engine, audio, midi, master_vol, logical_w_, logical_h_, 1);
    renderBottomHud(app, hw, input, logical_w_, logical_h_, 1);

    SDL_RenderPresent(renderer_);
}

} // namespace groovebox
