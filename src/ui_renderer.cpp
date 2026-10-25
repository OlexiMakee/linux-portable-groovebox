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

    // Якщо затиснуто SELECT, верхній бар динамічно відображає гарячі клавіші Elektron!
    if (input.btn_select) {
        fillRect(1, 1, w - 2, bar_h - 3, palette::kHeaderSel);
        const char* sel_hint = (w >= 320)
            ? "SEL: [A:PREV] [B:CLR TRK] [X:LEN] [Y:PAGE] [STA:RND] [VOL:BPM]"
            : "SEL: A:PRV B:CLR X:LEN Y:PG STA:RND";
        drawText(4, 5, sel_hint, palette::kParamTrig, 1);
        return;
    }

    // 5 вкладок: L1 (SEQ/SYNTH тогл), L2:FX, R2:MIX, R1:LFO, G:SYS
    char l1_label[24];
    if (app.active_tab == WorkspaceTab::kSynth) {
        std::snprintf(l1_label, sizeof(l1_label), "[L1] SYNTH/seq");
    } else {
        std::snprintf(l1_label, sizeof(l1_label), "[L1] SEQ/synth");
    }

    const char* labels_320[5] = {l1_label, "[L2] FX", "[R2] MIX", "[R1] LFO", "[G] SYS"};
    const char* labels_160[5] = {"L1:SEQ", "L2:FX", "R2:MIX", "R1:LFO", "G:SYS"};
    const bool held[5] = {input.l1, input.l2, input.r2, input.r1, input.btn_fn};

    // Пропорційне розбиття: G-SYS звужено до 44 px (на 1/3 менше), L1 розширено до 84 px, інші по 64 px
    const int tab_widths[5] = {84, 64, 64, 64, 44};
    int tx = 0;

    for (int i = 0; i < 5; ++i) {
        const int cur_tw = (w >= 320) ? tab_widths[i] : (w / 5);
        const bool is_active = (static_cast<int>(app.active_tab) == i);
        const char* lbl = (w >= 320) ? labels_320[i] : labels_160[i];
        const int text_x = tx + (cur_tw - static_cast<int>(std::strlen(lbl)) * 4) / 2;
        if (is_active) {
            fillRect(tx + 1, 1, cur_tw - 2, bar_h - 3, palette::kAccentOrange);
            drawText(text_x, 5, lbl, palette::kBgDark, 1);
        } else if (held[i]) {
            fillRect(tx + 1, 1, cur_tw - 2, bar_h - 3, palette::kAccentCyan);
            drawText(text_x, 5, lbl, palette::kBgDark, 1);
        } else {
            drawRect(tx + 1, 1, cur_tw - 2, bar_h - 3, palette::kGridDim);
            drawText(text_x, 5, lbl, (i == 4) ? palette::kAccentOrange : palette::kTextMain, 1);
        }
        tx += cur_tw;
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

    if (app.active_tab == WorkspaceTab::kSeq) {
        // [L1] SEQ: 8-рядкова матриця Elektron-секвенсора (2 чисті інструменти: Drums + VA Synth)
        const int active_rows = audio_engine.getActiveRowCount();
        const int cur_r = std::clamp(app.cursor_row, 0, active_rows - 1);
        const int cur_t = audio_engine.getRowTrack(cur_r);
        const int cur_v = audio_engine.getRowVoice(cur_r);
        const auto tp = audio_engine.getTrackParams(cur_t);
        const auto cur_st = audio_engine.getStep(cur_r, app.cursor_step);
        static constexpr const char* kNoteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        int oct = (cur_st.note / 12) - 1;
        const char* nn = kNoteNames[cur_st.note % 12];

        const char* trig_tag = !cur_st.enabled ? "OFF" : (cur_st.is_param_trig ? "[PARAM-LOCK]" : "[NOTE-TRIG]");

        const uint32_t cur_row_len = audio_engine.getRowLength(cur_r);
        const uint32_t total_row_pages = (cur_row_len + kStepsPerPage - 1) / kStepsPerPage;
        std::snprintf(
            buf, sizeof(buf), "R%d [%s] | ST%03d:%s%d %s%s | P%d/%d L:%u | BPM:%3.0f %s",
            cur_r + 1,
            getEngineTypeName(tp.engine),
            app.cursor_step + 1,
            nn, oct,
            (cur_st.slide && tp.engine != EngineType::kDrumSyn) ? "~SLD " : "",
            trig_tag,
            app.seq_page + 1,
            total_row_pages,
            cur_row_len,
            midi.external_sync ? midi.detected_bpm : app.bpm,
            app.transport_playing ? "PLAY" : "STOP"
        );
        ColorRGB hdr_col = cur_st.is_param_trig ? palette::kParamTrig : palette::kAccentOrange;
        drawText(4, top_y + 4, buf, hdr_col, 1);

        const int grid_x = 36;
        const int grid_y = top_y + 16;
        const int row_h = 13; // 8 рядків x 13 px = 104 px (висока читабельна сітка)
        const uint32_t page_offset = app.seq_page * kStepsPerPage;

        // Лінія-розділювач між центральною сіткою та правим сегментом
        drawLine(266, top_y + 2, 266, bot_y - 2, palette::kGridDim);
        drawText(270, top_y + 4, "TRK INFO", palette::kAccentOrange, 1);

        // Малюємо активні рядки з індивідуальною поліметрією та персональним Playhead
        for (int r = 0; r < active_rows; ++r) {
            int t = audio_engine.getRowTrack(r);
            int v = audio_engine.getRowVoice(r);
            int ry = grid_y + r * row_h;
            bool is_active_row = (r == cur_r);
            uint32_t r_len = audio_engine.getRowLength(r);
            const auto trk = audio_engine.getTrackParams(t);

            // Розділювач між інструментами (Drums vs Synth)
            if (v == 0 && r > 0) {
                drawLine(0, ry - 2, 265, ry - 2, palette::kGridDim);
            }

            // Назва рядка зліва
            char lbl[8];
            if (trk.engine == EngineType::kDrumSyn) {
                static constexpr const char* kDrumLabels[4] = {"BD", "SD", "CH", "OH"};
                std::snprintf(lbl, sizeof(lbl), "%s", kDrumLabels[std::clamp(v, 0, 3)]);
            } else if (trk.engine == EngineType::kAcid303) {
                std::snprintf(lbl, sizeof(lbl), "303");
            } else {
                std::snprintf(lbl, sizeof(lbl), "V%d", v + 1);
            }
            ColorRGB lbl_col = is_active_row ? palette::kAccentOrange : (trk.muted ? palette::kGridDim : palette::kTextDim);
            drawText(6, ry + 2, lbl, lbl_col, 1);

            // Персональний Playhead для цього рядка на основі його власної довжини!
            uint32_t row_playhead_step = app.transport_playing ? (app.current_step % r_len) : 0;

            // Малюємо 32 видимих кроки для цього рядка (велика сітка 7px)
            for (uint32_t s = 0; s < kStepsPerPage; ++s) {
                uint32_t step_idx = page_offset + s;
                int cx = grid_x + static_cast<int>(s) * 7;
                bool is_cursor = (r == cur_r && step_idx == static_cast<uint32_t>(app.cursor_step));
                bool is_playhead = app.transport_playing && (step_idx == row_playhead_step);

                if (step_idx < r_len) {
                    auto step_data = audio_engine.getStep(r, step_idx);
                    if (step_data.enabled) {
                        if (step_data.is_param_trig) {
                            fillRect(cx, ry + 1, 6, 9, palette::kParamTrig);
                        } else {
                            ColorRGB trig_col = (t % 2 == 0) ? palette::kLevelOk : palette::kAccentCyan;
                            fillRect(cx, ry + 1, 6, 9, trig_col);
                        }
                    } else {
                        if (s % 16 == 0) {
                            fillRect(cx + 2, ry + 4, 2, 3, palette::kTextDim);
                        } else if (s % 4 == 0) {
                            fillRect(cx + 2, ry + 5, 2, 2, palette::kGridDim);
                        }
                    }

                    if (is_playhead) {
                        drawLine(cx + 3, ry, cx + 3, ry + 10, palette::kCursorWhite);
                    }
                } else if (step_idx == r_len) {
                    drawLine(cx, ry, cx, ry + 10, palette::kAccentOrange);
                }

                if (is_cursor) {
                    drawRect(cx - 1, ry, 8, 11, palette::kCursorWhite);
                }
            }

            // ПРАВИЙ СЕГМЕНТ ДОРОЖОК (зона під вкладкою G-SYS, x = 268..318)
            if (is_active_row) {
                fillRect(268, ry, 50, row_h - 1, palette::kHeaderSel);
            }
            char r_info[16];
            uint32_t cur_p = (r_len > 0) ? ((r_len - 1) / kStepsPerPage + 1) : 1;
            std::snprintf(r_info, sizeof(r_info), "%-3s L%02u P%u", lbl, r_len, cur_p);
            drawText(270, ry + 2, r_info, is_active_row ? palette::kAccentOrange : palette::kTextMain, 1);
        }

        // Кнопка додавання нової доріжки під списком
        drawText(270, grid_y + active_rows * row_h + 4, "[+] ADD", palette::kTextDim, 1);

        // --- STEP INSPECTOR MODAL POPUP (Утримання кнопки A > 200 мс) ---
        if (app.step_inspector_open) {
            const int pw = 176;
            const int ph = 145;
            const int px = (w - pw) / 2;
            const int py = top_y + 8;

            fillRect(px, py, pw, ph, palette::kPanelBg);
            drawRect(px, py, pw, ph, palette::kAccentOrange);
            fillRect(px + 1, py + 1, pw - 2, 12, palette::kHeaderSel);

            std::snprintf(buf, sizeof(buf), "STEP INSPECTOR [T%d:V%d ST%03d]", cur_t + 1, cur_v + 1, app.cursor_step + 1);
            drawText(px + 4, py + 3, buf, palette::kParamTrig, 1);

            static constexpr const char* kParamNames[12] = {
                "TYPE", "NOTE", "VELOCITY", "GATE LEN", "SLIDE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "CUTOFF", "PROBABILITY", "CONDITION"
            };

            for (int i = 0; i < 12; ++i) {
                int iy = py + 15 + i * 10;
                bool is_sel = (app.step_inspector_param == i);
                if (is_sel) {
                    fillRect(px + 2, iy - 1, pw - 4, 10, palette::kHeaderSel);
                }

                char val_str[32];
                if (i == 0) {
                    std::snprintf(val_str, sizeof(val_str), "%s", !cur_st.enabled ? "OFF" : (cur_st.is_param_trig ? "PARAM TRIG" : "NOTE TRIG"));
                } else if (i == 1) {
                    std::snprintf(val_str, sizeof(val_str), "%s%d (%u)", nn, oct, cur_st.note);
                } else if (i == 2) {
                    std::snprintf(val_str, sizeof(val_str), "%3.0f (0..127)", cur_st.velocity * 127.0f);
                } else if (i == 3) {
                    std::snprintf(val_str, sizeof(val_str), "%3.0f%%", cur_st.gate_len * 100.0f);
                } else if (i == 4) {
                    if (tp.engine == EngineType::kDrumSyn) {
                        std::snprintf(val_str, sizeof(val_str), "N/A (DRUM)");
                    } else {
                        std::snprintf(val_str, sizeof(val_str), "%s", cur_st.slide ? "ON (65ms)" : "OFF");
                    }
                } else if (i == 5) {
                    float atk = (cur_st.attack >= 0.0f) ? cur_st.attack : tp.attack;
                    std::snprintf(val_str, sizeof(val_str), "%3.0f", atk * 127.0f);
                } else if (i == 6) {
                    float dec = (cur_st.decay >= 0.0f) ? cur_st.decay : tp.decay;
                    std::snprintf(val_str, sizeof(val_str), "%3.0f", dec * 127.0f);
                } else if (i == 7) {
                    float sus = (cur_st.sustain >= 0.0f) ? cur_st.sustain : tp.sustain;
                    std::snprintf(val_str, sizeof(val_str), "%3.0f", sus * 127.0f);
                } else if (i == 8) {
                    float rel = (cur_st.release >= 0.0f) ? cur_st.release : tp.release;
                    std::snprintf(val_str, sizeof(val_str), "%3.0f", rel * 127.0f);
                } else if (i == 9) {
                    float cut = (cur_st.plock_cutoff >= 0.0f) ? cur_st.plock_cutoff : 0.5f;
                    std::snprintf(val_str, sizeof(val_str), "%3.0f %s", cut * 127.0f, cur_st.has_plock ? "[LOCK]" : "");
                } else if (i == 10) {
                    std::snprintf(val_str, sizeof(val_str), "%3u%%", cur_st.probability);
                } else if (i == 11) {
                    std::snprintf(val_str, sizeof(val_str), "%s", getConditionName(cur_st.condition));
                }

                std::snprintf(buf, sizeof(buf), "%s%-11s: %s", is_sel ? ">" : " ", kParamNames[i], val_str);
                drawText(px + 4, iy + 1, buf, is_sel ? palette::kAccentCyan : palette::kTextMain, 1);
            }
            if (app.stick_plock_tweaked) {
                drawText(px + 4, py + ph - 8, "STICK TWEAKING... RELEASE A TO LOCK", palette::kParamTrig, 1);
            } else {
                drawText(px + 4, py + ph - 8, "DPAD:EDIT | STICK:P-LOCK | REL A:LOCK", palette::kTextDim, 1);
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
    } else if (app.active_tab == WorkspaceTab::kScope) {
        // [R1] SCOPE: Повноекранний осцилограф звукової хвилі + стерео VU
        std::snprintf(
            buf, sizeof(buf), "REAL-TIME OSCILLOSCOPE | RATE:%u Hz | BUF:%u | VOL:%3.0f%%",
            audio.sample_rate, audio.period_frames, master_vol * 100.0f
        );
        drawText(8, top_y + 6, buf, palette::kAccentOrange, 1);

        const int sc_x = 8;
        const int sc_y = top_y + 20;
        const int sc_w = w - 16;
        const int sc_h = ws_h - 45;

        drawRect(sc_x, sc_y, sc_w, sc_h, palette::kGridDim);
        drawLine(sc_x + 1, sc_y + sc_h / 2, sc_x + sc_w - 2, sc_y + sc_h / 2, palette::kPanelBg);

        int prev_y = sc_y + sc_h / 2;
        for (int px = 1; px < sc_w - 1; ++px) {
            uint32_t s_idx = (static_cast<uint32_t>(px) * 255u) / static_cast<uint32_t>(sc_w);
            float v = audio.scope_samples[s_idx];
            int cur_y = sc_y + (sc_h / 2) - static_cast<int>(v * (sc_h / 2 - 6));
            cur_y = std::clamp(cur_y, sc_y + 2, sc_y + sc_h - 2);
            drawLine(sc_x + px - 1, prev_y, sc_x + px, cur_y, palette::kAccentCyan);
            prev_y = cur_y;
        }

        // Подвійний стерео VU метр (L / R) внизу
        const int vu_y = sc_y + sc_h + 6;
        drawText(8, vu_y + 1, "L:", palette::kTextDim, 1);
        drawRect(24, vu_y, w - 32, 6, palette::kGridDim);
        int fill_l = static_cast<int>(std::clamp(audio.vu_left * 1.3f, 0.0f, 1.0f) * (w - 34));
        if (fill_l > 0) fillRect(25, vu_y + 1, fill_l, 4, palette::kLevelOk);

        drawText(8, vu_y + 9, "R:", palette::kTextDim, 1);
        drawRect(24, vu_y + 8, w - 32, 6, palette::kGridDim);
        int fill_r = static_cast<int>(std::clamp(audio.vu_right * 1.3f, 0.0f, 1.0f) * (w - 34));
        if (fill_r > 0) fillRect(25, vu_y + 9, fill_r, 4, palette::kLevelOk);

    } else {
        // [G] SYS: Elektron System Settings Menu (вертикальний список налаштувань)
        drawText(8, top_y + 4, "GLOBAL SYSTEM SETTINGS", palette::kAccentOrange, 1);
        drawLine(6, top_y + 14, w - 6, top_y + 14, palette::kGridDim);

        const bool is_host = (hw.usb_role == "host");
        const char* menu_titles[5] = {
            "AUDIO BUFFER SIZE",
            "USB PORT ROLE",
            "DAC SAMPLE RATE",
            "HEADPHONE MUX",
            "SYSTEM STATS"
        };
        char menu_values[5][64];
        std::snprintf(menu_values[0], sizeof(menu_values[0]), "< %u SAMPLES (%2.1f ms) >",
                      audio.period_frames, (audio.sample_rate > 0) ? (audio.period_frames * 1000.0f) / audio.sample_rate : 0.0f);
        std::snprintf(menu_values[1], sizeof(menu_values[1]), "< %s >",
                      is_host ? "HOST (5V ON: AUDIO/MIDI)" : "DEVICE (MAC SSH 10.1.1.2)");
        std::snprintf(menu_values[2], sizeof(menu_values[2]), "%u Hz (%s)",
                      audio.sample_rate, is_host ? "DIGITAKT II UAC2" : "RK817 DAC");
        std::snprintf(menu_values[3], sizeof(menu_values[3]), "%s",
                      input.headphones_plugged ? "HEADPHONES INSERTED [HP]" : "INTERNAL SPEAKER [SPK]");
        std::snprintf(menu_values[4], sizeof(menu_values[4]), "CPU: %.1f%% (%dC) | RAM: %d/%d MB",
                      hw.cpu_load_percent, hw.cpu_temp_c, hw.ram_used_mb, hw.ram_total_mb);

        for (int i = 0; i < 5; ++i) {
            int iy = top_y + 22 + i * 18;
            bool is_sel = (app.sys_menu_cursor == i);
            if (is_sel) {
                fillRect(8, iy - 2, w - 16, 15, palette::kHeaderSel);
                drawRect(8, iy - 2, w - 16, 15, palette::kAccentOrange);
            }
            drawText(14, iy + 2, menu_titles[i], is_sel ? palette::kAccentOrange : palette::kTextDim, 1);
            drawText(140, iy + 2, menu_values[i], is_sel ? palette::kAccentCyan : palette::kTextMain, 1);
        }

        drawText(8, bot_y - 14, "UP/DOWN: SELECT ITEM | LEFT/RIGHT or A: CHANGE VALUE", palette::kTextDim, 1);
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
    const int hud_h = 24;
    const int hud_y = h - hud_h;

    fillRect(0, hud_y, w, hud_h, palette::kPanelBg);
    drawLine(0, hud_y, w - 1, hud_y, palette::kGridDim);

    // 1. D-Pad Графічна піктограма (14x14 px)
    const int dpad_cx = 16;
    const int dpad_cy = hud_y + 11;
    fillRect(dpad_cx - 2, dpad_cy - 7, 4, 4, input.dpad_up ? palette::kAccentOrange : palette::kGridDim);
    fillRect(dpad_cx - 2, dpad_cy + 3, 4, 4, input.dpad_down ? palette::kAccentOrange : palette::kGridDim);
    fillRect(dpad_cx - 7, dpad_cy - 2, 4, 4, input.dpad_left ? palette::kAccentOrange : palette::kGridDim);
    fillRect(dpad_cx + 3, dpad_cy - 2, 4, 4, input.dpad_right ? palette::kAccentOrange : palette::kGridDim);
    fillRect(dpad_cx - 2, dpad_cy - 2, 4, 4, palette::kHeaderSel);

    // 2. L-Stick 2D Box (16x16 px)
    const int lbox_x = 42;
    const int lbox_y = hud_y + 4;
    drawRect(lbox_x, lbox_y, 16, 16, palette::kGridDim);
    drawLine(lbox_x + 1, lbox_y + 8, lbox_x + 14, lbox_y + 8, palette::kBgDark);
    drawLine(lbox_x + 8, lbox_y + 1, lbox_x + 8, lbox_y + 14, palette::kBgDark);
    int lpx = lbox_x + 8 + static_cast<int>(input.lx * 6.0f);
    int lpy = lbox_y + 8 + static_cast<int>(input.ly * 6.0f);
    fillRect(lpx - 1, lpy - 1, 3, 3, input.l3 ? palette::kParamTrig : palette::kAccentCyan);

    // 3. Центральний блок статусу транспорту, BPM та звуку (x = 68..212)
    drawLine(68, hud_y + 2, 68, h - 2, palette::kGridDim);
    drawLine(212, hud_y + 2, 212, h - 2, palette::kGridDim);

    if (input.btn_select) {
        drawText(74, hud_y + 4, "SEL: [A:PREV] [B:CLEAR] [X:LEN] [Y:PAGE]", palette::kParamTrig, 1);
        drawText(74, hud_y + 13, "SEL + VOL+/- : BPM TEMPO (60..200)", palette::kAccentOrange, 1);
    } else {
        const char* play_str = app.transport_playing ? "PLAY" : "STOP";
        ColorRGB play_col = app.transport_playing ? palette::kLevelOk : palette::kAccentOrange;
        drawText(74, hud_y + 4, play_str, play_col, 1);

        char bpm_buf[16];
        std::snprintf(bpm_buf, sizeof(bpm_buf), "%3.0f BPM", app.bpm);
        drawText(112, hud_y + 4, bpm_buf, palette::kTextMain, 1);

        const char* hp_str = input.headphones_plugged ? "[HP]" : "[SPK]";
        drawText(174, hud_y + 4, hp_str, input.headphones_plugged ? palette::kAccentCyan : palette::kTextDim, 1);

        // Контекстний рядок підказок
        if (app.step_inspector_open) {
            drawText(74, hud_y + 13, "INSPECTOR: DPAD:EDIT | STICK:LOCK", palette::kAccentCyan, 1);
        } else if (app.active_tab == WorkspaceTab::kSeq) {
            drawText(74, hud_y + 13, "A:TRIG/LOCK  B:DEL  X/Y:NOTE", palette::kTextDim, 1);
        } else if (app.active_tab == WorkspaceTab::kSynth) {
            drawText(74, hud_y + 13, "DPAD:PARAM/VALUE  A/B/X/Y:TRACK", palette::kTextDim, 1);
        } else {
            drawText(74, hud_y + 13, "ACTIVE WORKSPACE", palette::kTextDim, 1);
        }
    }

    // 4. R-Stick 2D Box (16x16 px)
    const int rbox_x = 222;
    const int rbox_y = hud_y + 4;
    drawRect(rbox_x, rbox_y, 16, 16, palette::kGridDim);
    drawLine(rbox_x + 1, rbox_y + 8, rbox_x + 14, rbox_y + 8, palette::kBgDark);
    drawLine(rbox_x + 8, rbox_y + 1, rbox_x + 8, rbox_y + 14, palette::kBgDark);
    int rpx = rbox_x + 8 + static_cast<int>(input.rx * 6.0f);
    int rpy = rbox_y + 8 + static_cast<int>(input.ry * 6.0f);
    fillRect(rpx - 1, rpy - 1, 3, 3, input.r3 ? palette::kParamTrig : palette::kAccentOrange);

    // 5. ABXY Diamond Графічний ромб (16x16 px)
    const int abxy_cx = 258;
    const int abxy_cy = hud_y + 11;
    fillRect(abxy_cx - 2, abxy_cy - 7, 4, 4, input.btn_x ? palette::kLevelOk : palette::kGridDim);
    fillRect(abxy_cx - 7, abxy_cy - 2, 4, 4, input.btn_y ? palette::kLevelOk : palette::kGridDim);
    fillRect(abxy_cx + 3, abxy_cy - 2, 4, 4, input.btn_a ? palette::kLevelOk : palette::kGridDim);
    fillRect(abxy_cx - 2, abxy_cy + 3, 4, 4, input.btn_b ? palette::kLevelOk : palette::kGridDim);

    drawText(274, hud_y + 4,  "A:TRIG", input.btn_a ? palette::kLevelOk : palette::kTextMain, 1);
    drawText(274, hud_y + 13, "B:DEL",  input.btn_b ? palette::kLevelOk : palette::kTextDim, 1);
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
