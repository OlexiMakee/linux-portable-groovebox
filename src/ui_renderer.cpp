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
            ? "SEL: [A:BUF 96..2048] [B:PANIC/KILL] [X:LEN 16..128] [Y:PAGE] [STA:RESET]"
            : "SEL: A:BUF B:PANIC X:LEN Y:PG STA:RST";
        drawText(4, 5, sel_hint, palette::kParamTrig, 1);
        return;
    }

    // 5 вкладок: L1:SEQ (Головний), L2:SYNTH, R2:MIX, R1:SCOPE, G:SYS
    const char* labels_320[5] = {"[L1] SEQ", "[L2] SYNTH", "[R2] MIX", "[R1] SCOPE", "[G] SYS"};
    const char* labels_160[5] = {"L1:SEQ", "L2:SYN", "R2:MIX", "R1:SCP", "G:SYS"};
    const bool held[5] = {input.l1, input.l2, input.r2, input.r1, input.btn_fn};
    const int tab_w = w / 5;

    for (int i = 0; i < 5; ++i) {
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
            drawText(text_x, 5, lbl, (i == 4) ? palette::kAccentOrange : palette::kTextMain, 1);
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

    if (app.active_tab == WorkspaceTab::kSeq) {
        // [L1] SEQ: Повноекранна 16-рядкова x 64/128-крокова матриця Elektron-секвенсора
        const int cur_r = std::clamp(app.cursor_row, 0, kTotalRows - 1);
        const int cur_t = cur_r / kVoicesPerTrack;
        const int cur_v = cur_r % kVoicesPerTrack;
        const auto tp = audio_engine.getTrackParams(cur_t);
        const auto cur_st = audio_engine.getStep(cur_r, app.cursor_step);
        static constexpr const char* kNoteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        int oct = (cur_st.note / 12) - 1;
        const char* nn = kNoteNames[cur_st.note % 12];

        const char* trig_tag = !cur_st.enabled ? "OFF" : (cur_st.is_param_trig ? "[PARAM-LOCK]" : "[NOTE-TRIG]");

        const uint32_t cur_row_len = audio_engine.getRowLength(cur_r);
        const uint32_t total_row_pages = (cur_row_len + kStepsPerPage - 1) / kStepsPerPage;
        std::snprintf(
            buf, sizeof(buf), "T%d:V%d [%s] | ST%03d:%s%d %s%s | P%d/%d L:%u | BPM:%3.0f %s",
            cur_t + 1, cur_v + 1,
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
        const int grid_y = top_y + 14;
        const int row_h = 10;
        const uint32_t page_offset = app.seq_page * kStepsPerPage;

        // Малюємо 16 рядків з індивідуальною поліметрією та персональним Playhead
        for (int r = 0; r < kTotalRows; ++r) {
            int t = audio_engine.getRowTrack(r);
            int v = audio_engine.getRowVoice(r);
            int ry = grid_y + r * row_h;
            bool is_active_row = (r == cur_r);
            uint32_t r_len = audio_engine.getRowLength(r);

            // Розділювач між доріжками
            if (v == 0 && r > 0) {
                drawLine(0, ry - 1, w - 1, ry - 1, palette::kGridDim);
            }

            // Назва рядка зліва
            char lbl[8];
            const auto trk = audio_engine.getTrackParams(t);
            if (trk.engine == EngineType::kDrumSyn) {
                static constexpr const char* kDrumLabels[4] = {"BD", "SD", "CH", "OH"};
                std::snprintf(lbl, sizeof(lbl), "1%s", kDrumLabels[v]);
            } else if (trk.engine == EngineType::kAcid303) {
                std::snprintf(lbl, sizeof(lbl), "2303"); // Монофонічний 303!
            } else {
                std::snprintf(lbl, sizeof(lbl), "%d:%d", t + 1, v + 1);
            }
            ColorRGB lbl_col = is_active_row ? palette::kAccentOrange : (trk.muted ? palette::kGridDim : palette::kTextDim);
            drawText(4, ry + 1, lbl, lbl_col, 1);

            // Персональний Playhead для цього рядка на основі його власної довжини!
            uint32_t row_playhead_step = app.transport_playing ? (app.current_step % r_len) : 0;

            // Малюємо 32 видимих кроки для цього рядка (велика сітка 7px)
            for (uint32_t s = 0; s < kStepsPerPage; ++s) {
                uint32_t step_idx = page_offset + s;
                int cx = grid_x + static_cast<int>(s) * 7;
                bool is_cursor = (r == cur_r && step_idx == static_cast<uint32_t>(app.cursor_step));
                bool is_playhead = app.transport_playing && (step_idx == row_playhead_step);

                if (step_idx < r_len) {
                    // Активна область патерну
                    auto step_data = audio_engine.getStep(r, step_idx);
                    if (step_data.enabled) {
                        if (step_data.is_param_trig) {
                            // Жовтий Elektron Param Trig (Lock-only)
                            fillRect(cx, ry + 1, 6, 7, palette::kParamTrig);
                        } else {
                            // Зелений/Бірюзовий Note Trig
                            ColorRGB trig_col = (t % 2 == 0) ? palette::kLevelOk : palette::kAccentCyan;
                            fillRect(cx, ry + 1, 6, 7, trig_col);
                        }
                    } else {
                        // Неактивна клітинка: розмітка тактів і долей
                        if (s % 16 == 0) {
                            fillRect(cx + 2, ry + 3, 2, 3, palette::kTextDim);
                        } else if (s % 4 == 0) {
                            fillRect(cx + 2, ry + 4, 2, 2, palette::kGridDim);
                        }
                    }

                    if (is_playhead) {
                        // Вертикальна лінія положення програвання (Playhead)
                        drawLine(cx + 3, ry, cx + 3, ry + 8, palette::kTextMain);
                    }
                } else if (step_idx == r_len) {
                    // Маркер кінця патерну доріжки |
                    drawLine(cx, ry, cx, ry + 8, palette::kAccentOrange);
                } else {
                    // Поза довжиною доріжки (затемнена неактивна область)
                }

                if (is_cursor) {
                    // Білий контур курсора редагування (8x9 пікселів)
                    drawRect(cx - 1, ry, 8, 9, palette::kCursorWhite);
                }
            }
        }

        // Вертикальні маркери сторінок та тактів справа для активного рядка (P1..P4)
        drawText(266, grid_y + 4,  (app.seq_page == 0) ? "[P1]" : " P1 ", (app.seq_page == 0) ? palette::kLevelOk : palette::kTextDim, 1);
        if (cur_row_len > 32) {
            drawText(266, grid_y + 14, (app.seq_page == 1) ? "[P2]" : " P2 ", (app.seq_page == 1) ? palette::kLevelOk : palette::kTextDim, 1);
        }
        if (cur_row_len > 64) {
            drawText(266, grid_y + 24, (app.seq_page == 2) ? "[P3]" : " P3 ", (app.seq_page == 2) ? palette::kLevelOk : palette::kTextDim, 1);
        }
        if (cur_row_len > 96) {
            drawText(266, grid_y + 34, (app.seq_page == 3) ? "[P4]" : " P4 ", (app.seq_page == 3) ? palette::kLevelOk : palette::kTextDim, 1);
        }
        char len_buf[16];
        std::snprintf(len_buf, sizeof(len_buf), "L%03u", cur_row_len);
        drawText(266, grid_y + 46, len_buf, palette::kAccentOrange, 1);

        // --- STEP INSPECTOR MODAL POPUP (Утримання кнопки A > 200 мс) ---
        if (app.step_inspector_open) {
            const int pw = 170;
            const int ph = 125;
            const int px = (w - pw) / 2;
            const int py = top_y + 10;

            fillRect(px, py, pw, ph, palette::kPanelBg);
            drawRect(px, py, pw, ph, palette::kAccentOrange);
            fillRect(px + 1, py + 1, pw - 2, 12, palette::kHeaderSel);

            std::snprintf(buf, sizeof(buf), "STEP INSPECTOR [T%d:V%d ST%03d]", cur_t + 1, cur_v + 1, app.cursor_step + 1);
            drawText(px + 4, py + 3, buf, palette::kParamTrig, 1);

            static constexpr const char* kParamNames[10] = {
                "TYPE", "NOTE", "VELOCITY", "GATE LEN", "SLIDE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "CUTOFF"
            };

            for (int i = 0; i < 10; ++i) {
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
                }

                std::snprintf(buf, sizeof(buf), "%s%-8s: %s", is_sel ? ">" : " ", kParamNames[i], val_str);
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
        // [G] SYS: Системний екран, апаратний монітор, USB роутинг та тест кнопок
        ColorRGB route_col = hw.digitakt_connected ? palette::kLevelOk : palette::kAccentOrange;
        std::snprintf(
            buf, sizeof(buf), "AUDIO OUT : %s | BUF:%u | VOL:%3.0f%% | XRUN:%u",
            audio.device_label.c_str(),
            audio.period_frames,
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
            buf, sizeof(buf), "SYSTEM    : CPU %2.0f%% [MAX %2.0f%%] (%dC) | RAM %d/%d MB",
            hw.cpu_load_percent, hw.cpu_max_percent, hw.cpu_temp_c, hw.ram_used_mb, hw.ram_total_mb
        );
        drawText(8, top_y + 24, buf, palette::kTextMain, 1);

        const bool is_host = (hw.usb_role == "host");
        std::snprintf(
            buf, sizeof(buf), "USB PORT  : %s [FN/G:SWITCH]",
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

        // 2D-приціли аналогових стіків
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

    if (input.btn_select) {
        drawText(4, hud_y + 3,  "DPAD: JUMP", palette::kParamTrig, 1);
        drawText(4, hud_y + 11, "U/D : TRACK x4", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "L/R : BAR x16", palette::kTextDim, 1);
    } else if (app.step_inspector_open) {
        drawText(4, hud_y + 3,  "DPAD: SELECT", palette::kAccentCyan, 1);
        drawText(4, hud_y + 11, "U/D : PARAM", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "L-STK: TWEAK", palette::kParamTrig, 1);
    } else if (app.active_tab == WorkspaceTab::kSeq) {
        drawText(4, hud_y + 3,  "DPAD: MATRIX", dpad_col, 1);
        drawText(4, hud_y + 11, "U/D : ROW 1..16", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "L/R : STEP 1..64", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kSynth) {
        drawText(4, hud_y + 3,  "DPAD: SYNTH", dpad_col, 1);
        drawText(4, hud_y + 11, "U/D : PARAM", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "L/R : VALUE", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kMixFx) {
        drawText(4, hud_y + 3,  "DPAD: MIXER", dpad_col, 1);
        drawText(4, hud_y + 11, "U/D : TRACK", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "L/R : LEVEL", palette::kTextDim, 1);
    } else {
        drawText(4, hud_y + 3,  "DPAD: NAV", dpad_col, 1);
        drawText(4, hud_y + 11, "U/D : ITEM", palette::kTextMain, 1);
        drawText(4, hud_y + 19, "L/R : CHANGE", palette::kTextDim, 1);
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
    if (input.btn_select) {
        drawText(cx_pos, hud_y + 11, "SEL:HOTKEYS", palette::kParamTrig, 1);
        drawText(cx_pos, hud_y + 19, "STA:RESET/STOP", palette::kAccentOrange, 1);
    } else {
        drawText(cx_pos, hud_y + 11, "SEL:HOLD", palette::kTextMain, 1);
        drawText(cx_pos, hud_y + 19, app.transport_playing ? "STA:[PLAY]" : "STA:STOP", app.transport_playing ? palette::kLevelOk : palette::kTextDim, 1);
    }

    // Правий стік
    drawText(rx_pos, hud_y + 3,  "R-STK:MOD", palette::kAccentCyan, 1);
    drawText(rx_pos, hud_y + 11, rbuf, palette::kTextMain, 1);
    drawText(rx_pos, hud_y + 19, input.r3 ? "[R3 PRESSED]" : "RESO / FM", palette::kTextDim, 1);

    // --- 3. Правий нижній кут: ABXY ---
    ColorRGB abxy_col = (input.btn_a || input.btn_b || input.btn_x || input.btn_y)
                            ? palette::kLevelOk
                            : palette::kAccentOrange;

    if (input.btn_select) {
        drawText(x_right + 4, hud_y + 3,  "SEL + ABXY", palette::kParamTrig, 1);
        drawText(x_right + 4, hud_y + 11, "A:BUF B:PANIC", palette::kTextMain, 1);
        drawText(x_right + 4, hud_y + 19, "X:LEN Y:PAGE", palette::kTextDim, 1);
    } else if (app.step_inspector_open) {
        drawText(x_right + 4, hud_y + 3,  "ABXY: INSPECT", palette::kAccentCyan, 1);
        drawText(x_right + 4, hud_y + 11, "HOLD A: EDIT", palette::kParamTrig, 1);
        drawText(x_right + 4, hud_y + 19, "REL A: CLOSE", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kSeq) {
        drawText(x_right + 4, hud_y + 3,  "ABXY: 16x64 SEQ", abxy_col, 1);
        drawText(x_right + 4, hud_y + 11, "A:TRIG(HOLD:ED)", palette::kTextMain, 1);
        drawText(x_right + 4, hud_y + 19, "B:PLCK X/Y:NOTE", palette::kTextDim, 1);
    } else if (app.active_tab == WorkspaceTab::kMixFx) {
        drawText(x_right + 4, hud_y + 3,  "ABXY: MIX/FX", abxy_col, 1);
        drawText(x_right + 4, hud_y + 11, "A:MUTE B:DLY+", palette::kTextMain, 1);
        drawText(x_right + 4, hud_y + 19, "X:DLY- Y:FBK", palette::kTextDim, 1);
    } else {
        drawText(x_right + 4, hud_y + 3,  "ABXY: LIVE PAD", abxy_col, 1);
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
