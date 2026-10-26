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

UiRenderer::UiRenderer(SDL_Renderer* custom_renderer, int w, int h)
    : renderer_(custom_renderer), logical_w_(w), logical_h_(h) {}

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
    if (window_) {
        if (renderer_) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
    }
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

int g_stick_l_mode = 0;
int g_stick_r_mode = 0;

const char* getStickLName(int mode) {
    switch (mode) {
        case 0: return "CUT/RES";
        case 1: return "FLT/ENV";
        case 2: return "DLY/FB";
        case 3: return "REV/DLY";
        default: return "CUT/RES";
    }
}

const char* getStickRName(int mode) {
    switch (mode) {
        case 0: return "MOD1/MOD2";
        case 1: return "FM/RATIO";
        case 2: return "ATK/DEC";
        case 3: return "PAN/LVL";
        default: return "MOD1/MOD2";
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
        const char* sel_hint = (app.active_tab == WorkspaceTab::kSynth)
            ? "SEL: [A:PREVIEW NOTE]"
            : ((w >= 320)
                ? "SEL: [A:PREV] [B:CLR TRK] [X:LEN] [Y:PAGE] [STA:RND] [VOL:BPM]"
                : "SEL: A:PRV B:CLR X:LEN Y:PG STA:RND");
        drawText(4, 5, sel_hint, palette::kParamTrig, 1);
        return;
    }

    // 5 вкладок: L1 (SEQ/SYNTH тогл), L2:FX, R1:MOD, R2:MIX, G:SYS
    char l1_label[24];
    if (app.active_tab == WorkspaceTab::kSynth) {
        std::snprintf(l1_label, sizeof(l1_label), "[L1] SYNTH");
    } else {
        std::snprintf(l1_label, sizeof(l1_label), "[L1] SEQ");
    }

    const char* labels_320[5] = {l1_label, "[L2] FX", "[R1] MOD", "[R2] MIX", "[G] SYS"};
    const char* labels_160[5] = {"L1:SYN", "L2:FX", "R1:MOD", "R2:MIX", "G:SYS"};
    const bool held[5] = {input.l1, input.l2, input.r1, input.r2, input.btn_fn};

    // Пропорційне розбиття: G-SYS звужено до 44 px, L1 розширено до 76 px, інші по 66 px
    const int tab_widths[5] = {76, 66, 66, 66, 46};
    int tx = 0;

    for (int i = 0; i < 5; ++i) {
        const int cur_tw = (w >= 320) ? tab_widths[i] : (w / 5);
        bool is_active = false;
        if (i == 0) {
            is_active = (app.active_tab == WorkspaceTab::kSeq || app.active_tab == WorkspaceTab::kSynth);
        } else if (i == 1) {
            is_active = (app.active_tab == WorkspaceTab::kFx);
        } else if (i == 2) {
            is_active = (app.active_tab == WorkspaceTab::kMod);
        } else if (i == 3) {
            is_active = (app.active_tab == WorkspaceTab::kMixFx);
        } else if (i == 4) {
            is_active = (app.active_tab == WorkspaceTab::kSys);
        }
        const char* lbl = (w >= 320) ? labels_320[i] : labels_160[i];
        const int text_x = tx + (cur_tw - static_cast<int>(std::strlen(lbl)) * 4) / 2;
        if (is_active) {
            ColorRGB tab_bg = (i == 0 && app.active_tab == WorkspaceTab::kSynth)
                ? palette::kAccentCyan
                : palette::kAccentOrange;
            fillRect(tx + 1, 1, cur_tw - 2, bar_h - 3, tab_bg);
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
    const int bot_y = h - 34; // 240 - 34 = 206 px (для розширеного 32px HUD)
    const int ws_h = bot_y - top_y;

    // Рамка активного воркспейсу
    drawRect(2, top_y, w - 4, ws_h, palette::kGridDim);

    char buf[128];

    if (app.active_tab == WorkspaceTab::kSeq) {
        // [L1] SEQ: 8-рядкова матриця Elektron-секвенсора (2 чисті інструменти: Drums + VA Synth)
        const int active_rows = audio_engine.getActiveRowCount();
        const bool on_add_slot = (app.cursor_row >= active_rows);
        const int cur_r = on_add_slot ? (active_rows - 1) : app.cursor_row;
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
        if (on_add_slot) {
            drawText(4, top_y + 4, "[+] CREATE NEW TRACK | PRESS A TO ADD 4 VOICES", palette::kAccentCyan, 1);
        } else {
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
        }

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
            bool is_active_row = (!on_add_slot && r == cur_r);
            uint32_t r_len = audio_engine.getRowLength(r);
            const auto trk = audio_engine.getTrackParams(t);

            // Розділювач між інструментами (Drums vs Synth)
            if (v == 0 && r > 0) {
                drawLine(0, ry - 2, 265, ry - 2, palette::kGridDim);
            }

            // Номер рядка зліва (01..16, стиль Elektron, без дублювання назв інструментів)
            char row_num[8];
            std::snprintf(row_num, sizeof(row_num), "%02d", r + 1);
            ColorRGB lbl_col = is_active_row ? palette::kAccentOrange : (trk.muted ? palette::kGridDim : palette::kTextDim);
            drawText(6, ry + 2, row_num, lbl_col, 1);

            // Назва інструмента для правого сегмента
            char lbl[8];
            if (trk.engine == EngineType::kDrumSyn) {
                static constexpr const char* kDrumLabels[4] = {"BD", "SD", "CH", "OH"};
                std::snprintf(lbl, sizeof(lbl), "%s", kDrumLabels[std::clamp(v, 0, 3)]);
            } else if (trk.engine == EngineType::kAcid303) {
                std::snprintf(lbl, sizeof(lbl), "303");
            } else {
                std::snprintf(lbl, sizeof(lbl), "V%d", v + 1);
            }

            // Персональний Playhead для цього рядка на основі його власної довжини!
            uint32_t row_playhead_step = app.transport_playing ? (app.current_step % r_len) : 0;

            // Малюємо 32 видимих кроки для цього рядка (велика сітка 7px)
            for (uint32_t s = 0; s < kStepsPerPage; ++s) {
                uint32_t step_idx = page_offset + s;
                int cx = grid_x + static_cast<int>(s) * 7;
                bool is_cursor = (is_active_row && step_idx == static_cast<uint32_t>(app.cursor_step));
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

        // [+] ADD TRACK слот у кінці списку доріжок:
        if (active_rows < kTotalRows) {
            int add_y = grid_y + active_rows * row_h;
            if (on_add_slot) {
                fillRect(4, add_y, w - 8, row_h - 1, palette::kHeaderSel);
                drawRect(4, add_y, w - 8, row_h - 1, palette::kAccentOrange);
            }
            drawText(6, add_y + 2, "[+]", on_add_slot ? palette::kAccentOrange : palette::kTextDim, 1);
            drawText(36, add_y + 2, "ADD NEW INSTRUMENT TRACK [PRESS A]", on_add_slot ? palette::kAccentCyan : palette::kTextDim, 1);
            drawText(270, add_y + 2, "[+] ADD", on_add_slot ? palette::kAccentOrange : palette::kTextDim, 1);
        }

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
        // [L1] SYNTH: Модульний табличний редактор саунд-дизайну та інструментів
        const int active_rows = audio_engine.getActiveRowCount();
        const int active_trks = (active_rows + 3) / 4;
        const auto tp = audio_engine.getTrackParams(app.selected_track);

        std::snprintf(
            buf, sizeof(buf), "SYNTH TRK %d/%d [%s] | DPAD:GRID STICK:TWEAK | [X:ADD Y:DEL]",
            app.selected_track + 1, active_trks, getEngineTypeName(tp.engine)
        );
        drawText(6, top_y + 4, buf, palette::kAccentOrange, 1);

        // 6 категорій параметрів (таблиця 6 рядків x 4 колонки)
        // app.selected_param: 0..23 (row = selected_param / 4, col = selected_param % 4)
        const int sel_row = std::clamp(app.selected_param / 4, 0, 5);
        const int sel_col = std::clamp(app.selected_param % 4, 0, 3);

        struct CellInfo {
            const char* title;
            char val[16];
            float norm_val; // -1 if no bar
        };

        static constexpr const char* kCatTitles[6] = {
            "INST", "FLT", "OSC", "ADSR", "MOD", "SEND"
        };

        for (int r = 0; r < 6; ++r) {
            int cy = top_y + 15 + r * 28;
            drawText(6, cy + 9, kCatTitles[r], (sel_row == r) ? palette::kAccentOrange : palette::kTextDim, 1);

            CellInfo cells[4];
            if (r == 0) {
                // INST: ENGINE, ACTIVE TRK, [+] ADD TRK, [-] DEL TRK
                cells[0] = {"ENGINE", "", -1.0f};
                std::snprintf(cells[0].val, sizeof(cells[0].val), "%s", getEngineTypeName(tp.engine));
                cells[1] = {"ACTIVE TRK", "", -1.0f};
                std::snprintf(cells[1].val, sizeof(cells[1].val), "TRK %d/%d", app.selected_track + 1, active_trks);
                cells[2] = {"ADD TRK", "[+] X", -1.0f};
                cells[3] = {"DEL TRK", "[-] Y", -1.0f};
            } else if (r == 1) {
                // FILTER: CUTOFF, RESO, ENV MOD, TYPE
                cells[0] = {"CUTOFF", "", tp.p[0]};
                std::snprintf(cells[0].val, sizeof(cells[0].val), "%3.0f", tp.p[0] * 127.0f);
                cells[1] = {"RESO", "", tp.p[1]};
                std::snprintf(cells[1].val, sizeof(cells[1].val), "%3.0f", tp.p[1] * 127.0f);
                cells[2] = {"ENV MOD", "", tp.p[2]};
                std::snprintf(cells[2].val, sizeof(cells[2].val), "%+3.0f", (tp.p[2] - 0.5f) * 127.0f);
                cells[3] = {"TYPE", "24dB LP", -1.0f};
            } else if (r == 2) {
                // OSC / FM: 4 динамічні параметри обраного движка
                for (int c = 0; c < 4; ++c) {
                    int p_idx = c + 2; // p[2]..p[5]
                    if (p_idx < kParamsPerTrack) {
                        const char* pn = getParamName(tp.engine, p_idx);
                        cells[c] = {pn, "", tp.p[p_idx]};
                        std::snprintf(cells[c].val, sizeof(cells[c].val), "%3.0f%%", tp.p[p_idx] * 100.0f);
                    } else {
                        cells[c] = {"---", "---", -1.0f};
                    }
                }
            } else if (r == 3) {
                // ADSR: ATTACK, DECAY, SUSTAIN, RELEASE
                cells[0] = {"ATTACK", "", tp.attack / 0.8f};
                std::snprintf(cells[0].val, sizeof(cells[0].val), "%3.0f ms", tp.attack * 1000.0f);
                cells[1] = {"DECAY", "", tp.decay / 1.5f};
                std::snprintf(cells[1].val, sizeof(cells[1].val), "%3.0f ms", tp.decay * 1000.0f);
                cells[2] = {"SUSTAIN", "", tp.sustain};
                std::snprintf(cells[2].val, sizeof(cells[2].val), "%3.0f%%", tp.sustain * 100.0f);
                cells[3] = {"RELEASE", "", tp.release / 1.8f};
                std::snprintf(cells[3].val, sizeof(cells[3].val), "%3.0f ms", tp.release * 1000.0f);
            } else if (r == 4) {
                // MOD / LFO: MOD1 RT, MOD1 DP, MOD2 RT, MOD2 DP
                cells[0] = {"MOD1 RATE", "1/8 T", 0.5f};
                cells[1] = {"MOD1 DPTH", "40%", 0.4f};
                cells[2] = {"MOD2 RATE", "1/4 D", 0.35f};
                cells[3] = {"MOD2 DPTH", "0%", 0.0f};
            } else if (r == 5) {
                // SENDS & STICKS: DLY SND, PAN, L-STICK, R-STICK
                cells[0] = {"DELAY SND", "", tp.delay_send};
                std::snprintf(cells[0].val, sizeof(cells[0].val), "%3.0f%%", tp.delay_send * 100.0f);
                cells[1] = {"PAN", "", (tp.pan + 1.0f) * 0.5f};
                std::snprintf(cells[1].val, sizeof(cells[1].val), "%+3.0f", tp.pan * 100.0f);
                cells[2] = {"L-STICK", "", -1.0f};
                std::snprintf(cells[2].val, sizeof(cells[2].val), "%s", getStickLName(g_stick_l_mode));
                cells[3] = {"R-STICK", "", -1.0f};
                std::snprintf(cells[3].val, sizeof(cells[3].val), "%s", getStickRName(g_stick_r_mode));
            }

            const int start_x = 42;
            const int cell_w = (w - start_x - 6) / 4;
            for (int c = 0; c < 4; ++c) {
                int bx = start_x + c * cell_w;
                bool is_cell_sel = (sel_row == r && sel_col == c);

                fillRect(bx + 1, cy + 1, cell_w - 2, 24, is_cell_sel ? palette::kHeaderSel : palette::kPanelBg);
                drawRect(bx, cy, cell_w - 1, 26, is_cell_sel ? palette::kAccentOrange : palette::kGridDim);

                drawText(bx + 3, cy + 3, cells[c].title, is_cell_sel ? palette::kAccentCyan : palette::kTextDim, 1);
                drawText(bx + 3, cy + 12, cells[c].val, is_cell_sel ? palette::kTextMain : palette::kTextDim, 1);

                if (cells[c].norm_val >= 0.0f) {
                    int bar_w = cell_w - 8;
                    int fill = static_cast<int>(std::clamp(cells[c].norm_val, 0.0f, 1.0f) * bar_w);
                    fillRect(bx + 3, cy + 21, bar_w, 2, palette::kGridDim);
                    if (fill > 0) {
                        fillRect(bx + 3, cy + 21, fill, 2, is_cell_sel ? palette::kAccentOrange : palette::kLevelOk);
                    }
                }
            }
        }

    } else if (app.active_tab == WorkspaceTab::kMixFx) {
        // Вкладка [R2] MIX: Компактна таблична матриця 4 треків + окремий рядок MASTER FX BUS
        std::snprintf(buf, sizeof(buf), "MIXER & MASTER EFFECTS BUS | MST VOL:%3.0f%%", master_vol * 100.0f);
        drawText(6, top_y + 4, buf, palette::kAccentOrange, 1);

        const int act_trks = (audio_engine.getActiveRowCount() + 3) / 4;

        // 4 канали треків (рядки 0..3)
        for (int t = 0; t < 4; ++t) {
            const auto tp = audio_engine.getTrackParams(t);
            const int by = top_y + 16 + t * 27;
            const bool is_act = (t < act_trks);
            const bool sel = (app.selected_track == t);

            if (sel) {
                fillRect(4, by, w - 8, 25, palette::kHeaderSel);
                drawRect(4, by, w - 8, 25, palette::kAccentOrange);
            } else {
                drawRect(4, by, w - 8, 25, palette::kGridDim);
            }

            // Col 1: Назва та статус (x = 8..90)
            char trk_hdr[32];
            std::snprintf(trk_hdr, sizeof(trk_hdr), "T%d [%-9s]", t + 1, is_act ? getEngineTypeName(tp.engine) : "OFF");
            drawText(8, by + 3, trk_hdr, is_act ? (sel ? palette::kAccentCyan : palette::kTextMain) : palette::kTextDim, 1);
            const char* mute_str = tp.muted ? "[MUTE]" : "[ON]";
            drawText(8, by + 13, mute_str, tp.muted ? palette::kLevelWarnClip : palette::kLevelOk, 1);

            // Col 2: Level slider + VU Peak (x = 94..180)
            char lvl_str[16];
            std::snprintf(lvl_str, sizeof(lvl_str), "LVL:%3.0f%%", tp.level * 100.0f);
            drawText(94, by + 3, lvl_str, palette::kTextDim, 1);
            drawRect(94, by + 12, 86, 9, palette::kGridDim);
            int v_lvl = static_cast<int>(std::clamp(audio.track_levels[t] * 1.4f, 0.0f, 1.0f) * 82.0f);
            if (v_lvl > 0 && !tp.muted && is_act) {
                fillRect(96, by + 14, v_lvl, 5, (v_lvl > 68) ? palette::kLevelWarnClip : palette::kLevelOk);
            }

            // Col 3: PAN (x = 186..240)
            char pan_str[16];
            std::snprintf(pan_str, sizeof(pan_str), "PAN:%+3.0f", tp.pan * 100.0f);
            drawText(186, by + 3, pan_str, palette::kTextDim, 1);
            drawRect(186, by + 12, 54, 9, palette::kGridDim);
            int pan_dot = 186 + 27 + static_cast<int>(tp.pan * 23.0f);
            fillRect(pan_dot - 1, by + 13, 3, 7, palette::kAccentCyan);

            // Col 4: DLY SEND (x = 246..312)
            char dly_str[16];
            std::snprintf(dly_str, sizeof(dly_str), "DLY:%2.0f%%", tp.delay_send * 100.0f);
            drawText(246, by + 3, dly_str, palette::kTextDim, 1);
            drawRect(246, by + 12, 64, 9, palette::kGridDim);
            int dly_fill = static_cast<int>(tp.delay_send * 60.0f);
            if (dly_fill > 0 && is_act) {
                fillRect(248, by + 14, dly_fill, 5, palette::kAccentOrange);
            }
        }

        // Рядок 5: MASTER FX BUS (y = top_y + 126, x = 4..316)
        const int my = top_y + 126;
        fillRect(4, my, w - 8, 48, palette::kPanelBg);
        drawRect(4, my, w - 8, 48, palette::kAccentOrange);
        drawText(8, my + 3, "MASTER FX BUS (PING-PONG DELAY / LIMITER)", palette::kAccentOrange, 1);

        float cur_fb = audio_engine.params().delay_feedback.load(std::memory_order_relaxed);
        const int m_cell_w = (w - 16) / 4;
        const char* m_titles[4] = {"MST VOL", "DLY TIME", "DLY FDBK", "LIMITER"};
        char m_vals[4][16];
        std::snprintf(m_vals[0], sizeof(m_vals[0]), "%3.0f%%", master_vol * 100.0f);
        std::snprintf(m_vals[1], sizeof(m_vals[1]), "250 ms");
        std::snprintf(m_vals[2], sizeof(m_vals[2]), "%3.0f%%", cur_fb * 100.0f);
        std::snprintf(m_vals[3], sizeof(m_vals[3]), "ACTIVE");

        for (int c = 0; c < 4; ++c) {
            int mx = 8 + c * m_cell_w;
            drawRect(mx, my + 14, m_cell_w - 4, 30, palette::kGridDim);
            drawText(mx + 4, my + 17, m_titles[c], palette::kAccentCyan, 1);
            drawText(mx + 4, my + 28, m_vals[c], palette::kTextMain, 1);
        }
    } else if (app.active_tab == WorkspaceTab::kFx) {
        // [L2] FX: Master Send Effects (Stereo Ping-Pong Delay & Reverb & Master Limiter)
        drawText(8, top_y + 4, "MASTER SEND EFFECTS & MASTER BUS", palette::kAccentOrange, 1);
        drawLine(6, top_y + 14, w - 6, top_y + 14, palette::kGridDim);

        float cur_fb = audio_engine.params().delay_feedback.load(std::memory_order_relaxed);
        float cur_dm = audio_engine.params().delay_mix.load(std::memory_order_relaxed);
        float cur_rd = audio_engine.params().reverb_decay.load(std::memory_order_relaxed);
        float cur_rm = audio_engine.params().reverb_mix.load(std::memory_order_relaxed);

        // Блок 1: STEREO PING-PONG DELAY (top_y + 20..80)
        fillRect(6, top_y + 20, w - 12, 54, palette::kPanelBg);
        drawRect(6, top_y + 20, w - 12, 54, palette::kAccentCyan);
        drawText(12, top_y + 24, "STEREO PING-PONG DELAY", palette::kAccentCyan, 1);

        const int col_w = (w - 24) / 4;
        const char* dly_titles[4] = {"TIME", "FEEDBACK", "MIX", "CROSS"};
        char dly_vals[4][16];
        std::snprintf(dly_vals[0], sizeof(dly_vals[0]), "250 ms");
        std::snprintf(dly_vals[1], sizeof(dly_vals[1]), "%3.0f%%", cur_fb * 100.0f);
        std::snprintf(dly_vals[2], sizeof(dly_vals[2]), "%3.0f%%", cur_dm * 100.0f);
        std::snprintf(dly_vals[3], sizeof(dly_vals[3]), "PING-PONG");

        for (int c = 0; c < 4; ++c) {
            int cx = 10 + c * col_w;
            drawRect(cx, top_y + 36, col_w - 4, 32, palette::kGridDim);
            drawText(cx + 4, top_y + 40, dly_titles[c], palette::kTextDim, 1);
            drawText(cx + 4, top_y + 52, dly_vals[c], palette::kTextMain, 1);
        }

        // Блок 2: DIFFUSION REVERB (top_y + 80..140)
        fillRect(6, top_y + 80, w - 12, 54, palette::kPanelBg);
        drawRect(6, top_y + 80, w - 12, 54, palette::kAccentOrange);
        drawText(12, top_y + 84, "SCHROEDER DIFFUSION STEREO REVERB", palette::kAccentOrange, 1);

        const char* rev_titles[4] = {"DECAY", "DAMPING", "RETURN", "SPACE"};
        char rev_vals[4][16];
        std::snprintf(rev_vals[0], sizeof(rev_vals[0]), "%3.0f%%", cur_rd * 100.0f);
        std::snprintf(rev_vals[1], sizeof(rev_vals[1]), "60%%");
        std::snprintf(rev_vals[2], sizeof(rev_vals[2]), "%3.0f%%", cur_rm * 100.0f);
        std::snprintf(rev_vals[3], sizeof(rev_vals[3]), "HALL");

        for (int c = 0; c < 4; ++c) {
            int cx = 10 + c * col_w;
            drawRect(cx, top_y + 96, col_w - 4, 32, palette::kGridDim);
            drawText(cx + 4, top_y + 100, rev_titles[c], palette::kTextDim, 1);
            drawText(cx + 4, top_y + 112, rev_vals[c], palette::kTextMain, 1);
        }

        // Блок 3: MASTER LIMITER & METERS (top_y + 140..176)
        fillRect(6, top_y + 140, w - 12, 38, palette::kPanelBg);
        drawRect(6, top_y + 140, w - 12, 38, palette::kGridDim);
        drawText(12, top_y + 144, "MASTER BUS LIMITER & OUTPUT STAGE", palette::kTextMain, 1);

        char mst_str[32];
        std::snprintf(mst_str, sizeof(mst_str), "OUT: %3.0f%% | PEAK: %3.0f%%", master_vol * 100.0f, audio.peak_hold * 100.0f);
        drawText(12, top_y + 158, mst_str, (audio.peak_hold > 0.95f) ? palette::kLevelWarnClip : palette::kLevelOk, 1);
        drawRect(180, top_y + 156, w - 192, 10, palette::kGridDim);
        int m_bar = static_cast<int>(std::clamp(audio.peak_hold, 0.0f, 1.0f) * (w - 196));
        if (m_bar > 0) {
            fillRect(182, top_y + 158, m_bar, 6, (m_bar > (w - 205)) ? palette::kLevelWarnClip : palette::kLevelOk);
        }

    } else if (app.active_tab == WorkspaceTab::kMod) {
        // [R1] MOD: Модуляція (2x LFO + Macro Sticks) + живий Mini-Scope у правому кутку
        const auto tp = audio_engine.getTrackParams(app.selected_track);
        std::snprintf(buf, sizeof(buf), "MODULATION MATRIX | TRK %d [%s]", app.selected_track + 1, getEngineTypeName(tp.engine));
        drawText(8, top_y + 4, buf, palette::kAccentOrange, 1);

        // Вбудований живий Mini-Scope (64x24 px) у правому верхньому кутку
        const int mini_x = w - 74;
        const int mini_y = top_y + 2;
        const int mini_w = 68;
        const int mini_h = 24;
        fillRect(mini_x, mini_y, mini_w, mini_h, palette::kBgDark);
        drawRect(mini_x, mini_y, mini_w, mini_h, palette::kAccentCyan);
        int prev_my = mini_y + mini_h / 2;
        for (int px = 1; px < mini_w - 1; ++px) {
            uint32_t s_idx = (static_cast<uint32_t>(px) * 255u) / static_cast<uint32_t>(mini_w);
            float sv = audio.scope_samples[s_idx];
            int cur_my = mini_y + (mini_h / 2) - static_cast<int>(sv * (mini_h / 2 - 2));
            cur_my = std::clamp(cur_my, mini_y + 1, mini_y + mini_h - 2);
            drawLine(mini_x + px - 1, prev_my, mini_x + px, cur_my, palette::kAccentCyan);
            prev_my = cur_my;
        }

        drawLine(6, top_y + 28, w - 6, top_y + 28, palette::kGridDim);

        // Блок 1: LFO 1 (top_y + 32..80)
        fillRect(6, top_y + 32, w - 12, 44, palette::kPanelBg);
        drawRect(6, top_y + 32, w - 12, 44, palette::kAccentCyan);
        drawText(12, top_y + 36, "LFO 1 (TRACK ASSIGNABLE)", palette::kAccentCyan, 1);
        const int l_col = (w - 24) / 4;
        const char* l1_titles[4] = {"WAVE", "RATE", "DEPTH", "DESTINATION"};
        char l1_vals[4][16];
        std::snprintf(l1_vals[0], sizeof(l1_vals[0]), "%s", getLfoWaveName(tp.lfo1.wave));
        std::snprintf(l1_vals[1], sizeof(l1_vals[1]), "%2.1f Hz", tp.lfo1.rate * 10.0f);
        std::snprintf(l1_vals[2], sizeof(l1_vals[2]), "%3.0f%%", tp.lfo1.depth * 100.0f);
        std::snprintf(l1_vals[3], sizeof(l1_vals[3]), "%s", getStickDestShortName(tp.lfo1.dest));

        for (int c = 0; c < 4; ++c) {
            int cx = 10 + c * l_col;
            drawRect(cx, top_y + 46, l_col - 4, 26, palette::kGridDim);
            drawText(cx + 4, top_y + 48, l1_titles[c], palette::kTextDim, 1);
            drawText(cx + 4, top_y + 58, l1_vals[c], palette::kTextMain, 1);
        }

        // Блок 2: LFO 2 (top_y + 80..128)
        fillRect(6, top_y + 80, w - 12, 44, palette::kPanelBg);
        drawRect(6, top_y + 80, w - 12, 44, palette::kAccentOrange);
        drawText(12, top_y + 84, "LFO 2 (TRACK ASSIGNABLE)", palette::kAccentOrange, 1);
        const char* l2_titles[4] = {"WAVE", "RATE", "DEPTH", "DESTINATION"};
        char l2_vals[4][16];
        std::snprintf(l2_vals[0], sizeof(l2_vals[0]), "%s", getLfoWaveName(tp.lfo2.wave));
        std::snprintf(l2_vals[1], sizeof(l2_vals[1]), "%2.1f Hz", tp.lfo2.rate * 10.0f);
        std::snprintf(l2_vals[2], sizeof(l2_vals[2]), "%3.0f%%", tp.lfo2.depth * 100.0f);
        std::snprintf(l2_vals[3], sizeof(l2_vals[3]), "%s", getStickDestShortName(tp.lfo2.dest));

        for (int c = 0; c < 4; ++c) {
            int cx = 10 + c * l_col;
            drawRect(cx, top_y + 94, l_col - 4, 26, palette::kGridDim);
            drawText(cx + 4, top_y + 96, l2_titles[c], palette::kTextDim, 1);
            drawText(cx + 4, top_y + 106, l2_vals[c], palette::kTextMain, 1);
        }

        // Блок 3: DYNAMIC ANALOG STICK MACRO ROUTING (top_y + 128..176)
        fillRect(6, top_y + 128, w - 12, 46, palette::kPanelBg);
        drawRect(6, top_y + 128, w - 12, 46, palette::kGridDim);
        drawText(12, top_y + 132, "DYNAMIC ANALOG STICK MACRO ROUTING", palette::kTextMain, 1);
        const char* stk_titles[4] = {"L-STICK X", "L-STICK Y", "R-STICK X", "R-STICK Y"};
        char stk_vals[4][16];
        std::snprintf(stk_vals[0], sizeof(stk_vals[0]), "%s", getStickDestShortName(tp.stick_left_x));
        std::snprintf(stk_vals[1], sizeof(stk_vals[1]), "%s", getStickDestShortName(tp.stick_left_y));
        std::snprintf(stk_vals[2], sizeof(stk_vals[2]), "%s", getStickDestShortName(tp.stick_right_x));
        std::snprintf(stk_vals[3], sizeof(stk_vals[3]), "%s", getStickDestShortName(tp.stick_right_y));

        for (int c = 0; c < 4; ++c) {
            int cx = 10 + c * l_col;
            drawRect(cx, top_y + 142, l_col - 4, 26, palette::kGridDim);
            drawText(cx + 4, top_y + 144, stk_titles[c], palette::kTextDim, 1);
            drawText(cx + 4, top_y + 154, stk_vals[c], palette::kAccentCyan, 1);
        }

    } else {
        // [G] SYS: Elektron System Settings Menu (вертикальний список налаштувань)
        drawText(8, top_y + 4, "GLOBAL SYSTEM SETTINGS", palette::kAccentOrange, 1);
        drawLine(6, top_y + 14, w - 6, top_y + 14, palette::kGridDim);

        const char* usb_mode_str = "DEVICE (MAC SSH 10.1.1.2)";
        if (hw.usb_role == "host") {
            usb_mode_str = "HOST (OTG 5V ON: MIDI/AUDIO)";
        } else if (hw.usb_role == "device_audio") {
            usb_mode_str = "DEVICE (AUDIO GADGET http://10.1.1.2:8000)";
        }

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
        std::snprintf(menu_values[1], sizeof(menu_values[1]), "< %s >", usb_mode_str);
        std::snprintf(menu_values[2], sizeof(menu_values[2]), "%u Hz (%s)",
                      audio.sample_rate, (hw.usb_role == "host") ? "DIGITAKT II UAC2" : "RK817 DAC");
        std::snprintf(menu_values[3], sizeof(menu_values[3]), "%s",
                      input.headphones_plugged ? "HEADPHONES INSERTED [HP]" : "INTERNAL SPEAKER [SPK]");
        std::snprintf(menu_values[4], sizeof(menu_values[4]), "CPU: %.1f%% (%dC) | NET AUDIO: :8000",
                      hw.cpu_load_percent, hw.cpu_temp_c);

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
    const AudioEngine& audio_engine,
    int w,
    int h,
    int s
) {
    (void)hw;
    (void)s;
    const int hud_h = 32;
    const int hud_y = h - hud_h;

    fillRect(0, hud_y, w, hud_h, palette::kPanelBg);
    drawLine(0, hud_y, w - 1, hud_y, palette::kGridDim);

    // 1. D-Pad Графічна піктограма (14x14 px)
    const int dpad_cx = 12;
    const int dpad_cy = hud_y + 19;
    drawText(4, hud_y + 2, "DPAD", palette::kTextDim, 1);
    fillRect(dpad_cx - 2, dpad_cy - 7, 4, 4, input.dpad_up ? palette::kAccentOrange : palette::kGridDim);
    fillRect(dpad_cx - 2, dpad_cy + 3, 4, 4, input.dpad_down ? palette::kAccentOrange : palette::kGridDim);
    fillRect(dpad_cx - 7, dpad_cy - 2, 4, 4, input.dpad_left ? palette::kAccentOrange : palette::kGridDim);
    fillRect(dpad_cx + 3, dpad_cy - 2, 4, 4, input.dpad_right ? palette::kAccentOrange : palette::kGridDim);
    fillRect(dpad_cx - 2, dpad_cy - 2, 4, 4, palette::kHeaderSel);

    // 2. L-Stick 2D Box (16x16 px) з динамічним підписом мішені
    const auto tp = audio_engine.getTrackParams(app.selected_track);
    std::string stick_l_name = std::string(getStickDestShortName(tp.stick_left_x)) + "/" + getStickDestShortName(tp.stick_left_y);
    std::string stick_r_name = std::string(getStickDestShortName(tp.stick_right_x)) + "/" + getStickDestShortName(tp.stick_right_y);

    const int lbox_x = 28;
    const int lbox_y = hud_y + 12;
    drawText(26, hud_y + 2, stick_l_name.c_str(), palette::kAccentCyan, 1);
    drawRect(lbox_x, lbox_y, 16, 16, palette::kGridDim);
    drawLine(lbox_x + 1, lbox_y + 8, lbox_x + 14, lbox_y + 8, palette::kBgDark);
    drawLine(lbox_x + 8, lbox_y + 1, lbox_x + 8, lbox_y + 14, palette::kBgDark);
    int lpx = lbox_x + 8 + static_cast<int>(input.lx * 6.0f);
    int lpy = lbox_y + 8 + static_cast<int>(input.ly * 6.0f);
    fillRect(lpx - 1, lpy - 1, 3, 3, input.l3 ? palette::kParamTrig : palette::kAccentCyan);

    // 3. Центральний блок статусу транспорту, BPM та звуку (x = 56..202)
    drawLine(56, hud_y + 2, 56, h - 2, palette::kGridDim);
    drawLine(202, hud_y + 2, 202, h - 2, palette::kGridDim);

    const char* play_str = app.transport_playing ? "PLAY" : "STOP";
    ColorRGB play_col = app.transport_playing ? palette::kLevelOk : palette::kAccentOrange;
    drawText(60, hud_y + 3, play_str, play_col, 1);

    char bpm_buf[16];
    std::snprintf(bpm_buf, sizeof(bpm_buf), "%3.0f BPM", app.bpm);
    drawText(98, hud_y + 3, bpm_buf, palette::kTextMain, 1);

    const char* hp_str = input.headphones_plugged ? "[HP]" : "[SPK]";
    drawText(160, hud_y + 3, hp_str, input.headphones_plugged ? palette::kAccentCyan : palette::kTextDim, 1);

    // Контекстні рядки статусу та підказок
    char ctx_buf[48];
    if (app.step_inspector_open) {
        std::snprintf(ctx_buf, sizeof(ctx_buf), "STEP INSPECTOR (P-LOCK EDIT)");
        drawText(60, hud_y + 13, ctx_buf, palette::kParamTrig, 1);
        drawText(60, hud_y + 22, "DPAD:NAV | STICK:TWEAK", palette::kAccentCyan, 1);
    } else if (app.active_tab == WorkspaceTab::kSeq) {
        std::snprintf(ctx_buf, sizeof(ctx_buf), "SEQ MATRIX | %d STEPS/PG", kStepsPerPage);
        drawText(60, hud_y + 13, ctx_buf, palette::kTextMain, 1);
        drawText(60, hud_y + 22, "DPAD:NAV | A:TRIG B:DEL", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kSynth) {
        std::snprintf(ctx_buf, sizeof(ctx_buf), "SYNTH EDITOR | TRK %d", app.selected_track + 1);
        drawText(60, hud_y + 13, ctx_buf, palette::kAccentOrange, 1);
        drawText(60, hud_y + 22, "DPAD:CELL | A:VAL+ B:VAL-", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kFx) {
        drawText(60, hud_y + 13, "MASTER FX BUS & REVERB", palette::kAccentCyan, 1);
        drawText(60, hud_y + 22, "DPAD:CELL | STICK:MACRO", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kMod) {
        drawText(60, hud_y + 13, "DUAL LFO & STICK MATRIX", palette::kAccentOrange, 1);
        drawText(60, hud_y + 22, "DPAD:NAV | A/B:ADJUST", palette::kTextDim, 1);
    } else {
        drawText(60, hud_y + 13, "ACTIVE WORKSPACE", palette::kTextMain, 1);
        drawText(60, hud_y + 22, "ELEKTRON WORKFLOW", palette::kTextDim, 1);
    }

    // 4. R-Stick 2D Box (16x16 px) з динамічним підписом мішені
    const int rbox_x = 212;
    const int rbox_y = hud_y + 12;
    drawText(206, hud_y + 2, stick_r_name.c_str(), palette::kAccentOrange, 1);
    drawRect(rbox_x, rbox_y, 16, 16, palette::kGridDim);
    drawLine(rbox_x + 1, rbox_y + 8, rbox_x + 14, rbox_y + 8, palette::kBgDark);
    drawLine(rbox_x + 8, rbox_y + 1, rbox_x + 8, rbox_y + 14, palette::kBgDark);
    int rpx = rbox_x + 8 + static_cast<int>(input.rx * 6.0f);
    int rpy = rbox_y + 8 + static_cast<int>(input.ry * 6.0f);
    fillRect(rpx - 1, rpy - 1, 3, 3, input.r3 ? palette::kParamTrig : palette::kAccentOrange);

    // 5. ABXY Diamond Графічний ромб (16x16 px)
    const int abxy_cx = 244;
    const int abxy_cy = hud_y + 18;
    fillRect(abxy_cx - 2, abxy_cy - 7, 4, 4, input.btn_x ? palette::kLevelOk : palette::kGridDim); // North = X
    fillRect(abxy_cx - 7, abxy_cy - 2, 4, 4, input.btn_y ? palette::kLevelOk : palette::kGridDim); // West = Y
    fillRect(abxy_cx + 3, abxy_cy - 2, 4, 4, input.btn_a ? palette::kLevelOk : palette::kGridDim); // East = A
    fillRect(abxy_cx - 2, abxy_cy + 3, 4, 4, input.btn_b ? palette::kLevelOk : palette::kGridDim); // South = B

    // 6. Динамічні підписи кнопок у два окремі рядки (Рядок 1: X/Y, Рядок 2: A/B)
    const char* func_x = "--";
    const char* func_y = "--";
    const char* func_a = "TRIG";
    const char* func_b = "DEL";

    if (input.btn_select) {
        if (app.active_tab == WorkspaceTab::kSeq) {
            func_x = "LEN";
            func_y = "PAGE";
            func_a = "PREV";
            func_b = "CLEAR";
        } else if (app.active_tab == WorkspaceTab::kSynth) {
            func_x = "--";
            func_y = "--";
            func_a = "PREV";
            func_b = "--";
        } else {
            func_x = "--";
            func_y = "--";
            func_a = "PREV";
            func_b = "--";
        }
    } else if (app.active_tab == WorkspaceTab::kSeq) {
        func_x = "NOTE+";
        func_y = "PAGE";
        func_a = "TRIG";
        func_b = "DEL";
    } else if (app.active_tab == WorkspaceTab::kSynth) {
        func_x = "ADD";
        func_y = "DEL";
        func_a = "VAL+";
        func_b = "VAL-";
    } else if (app.active_tab == WorkspaceTab::kFx) {
        func_x = "TIME";
        func_y = "REV";
        func_a = "VAL+";
        func_b = "VAL-";
    } else if (app.active_tab == WorkspaceTab::kMod) {
        func_x = "DEST";
        func_y = "WAVE";
        func_a = "RATE+";
        func_b = "RATE-";
    } else if (app.active_tab == WorkspaceTab::kMixFx) {
        func_x = "MUTE";
        func_y = "SOLO";
        func_a = "SET";
        func_b = "RESET";
    } else if (app.active_tab == WorkspaceTab::kSys) {
        func_x = "--";
        func_y = "--";
        func_a = "ENTER";
        func_b = "BACK";
    }

    char r1_txt[24];
    std::snprintf(r1_txt, sizeof(r1_txt), "X:%-4s Y:%-4s", func_x, func_y);
    drawText(256, hud_y + 6, r1_txt, palette::kTextMain, 1);

    char r2_txt[24];
    std::snprintf(r2_txt, sizeof(r2_txt), "A:%-4s B:%-4s", func_a, func_b);
    drawText(256, hud_y + 18, r2_txt, palette::kTextMain, 1);
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
    renderBottomHud(app, hw, input, audio_engine, logical_w_, logical_h_, 1);

    SDL_RenderPresent(renderer_);
}

} // namespace groovebox
