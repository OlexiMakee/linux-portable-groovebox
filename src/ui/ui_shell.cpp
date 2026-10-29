#include "ui/ui_shell.hpp"
#include "ui/widgets.hpp"

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace groovebox {

UiShell::UiShell() : router_(Keymap{}) {}

UiShell::UiShell(SDL_Renderer* custom_renderer, int w, int h)
    : renderer_(custom_renderer), logical_w_(w), logical_h_(h), router_(Keymap{}) {}

UiShell::~UiShell() {
    shutdown();
}

bool UiShell::init() {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    uint32_t win_flags = SDL_WINDOW_SHOWN;
#if defined(__linux__)
    win_flags |= SDL_WINDOW_FULLSCREEN;
#endif

    window_ = SDL_CreateWindow(
        "XU10 Groovebox [SOLID Modular Architecture]",
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
    applyLogicalResolution(true);
    return true;
}

void UiShell::shutdown() {
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

void UiShell::applyLogicalResolution(bool high_res) {
    logical_w_ = high_res ? kHighResWidth : kLowResWidth;
    logical_h_ = high_res ? kHighResHeight : kLowResHeight;
    if (renderer_) {
        SDL_RenderSetLogicalSize(renderer_, logical_w_, logical_h_);
    }
}

void UiShell::registerView(std::unique_ptr<IView> view) {
    if (!view) return;
    const int idx = static_cast<int>(view->id());
    if (idx >= 0 && idx < kViewCount) {
        views_[idx] = std::move(view);
    }
}

IView* UiShell::findView(ViewId id) {
    const int idx = static_cast<int>(id);
    if (idx >= 0 && idx < kViewCount) return views_[idx].get();
    return nullptr;
}

IView& UiShell::currentView(const UiContext& ctx) {
    if (ctx.app.step_inspector_open) {
        IView* v = findView(ViewId::kInspect);
        if (v) return *v;
    }
    IView* v = findView(ctx.app.active_view);
    return v ? *v : *views_[0];
}

void UiShell::setTabSlots(std::vector<TabSlot> slots) {
    tab_slots_ = std::move(slots);
}

void UiShell::handleInput(UiContext& ctx) {
    ctx.shell = this;
    router_.processInput(ctx);

    IView* active_v = ctx.app.step_inspector_open ? findView(ViewId::kInspect) : findView(ctx.app.active_view);
    if (active_v) {
        active_v->tick(ctx);
    }
}

void UiShell::renderTabBar(Canvas& cv, const UiContext& ctx) {
    cv.fill(0, 0, logical_w_, kTabBarH, palette::kPanelBg);
    cv.line(0, kTabBarH - 1, logical_w_ - 1, kTabBarH - 1, palette::kGridDim);

    int tx = 0;
    for (size_t i = 0; i < tab_slots_.size(); ++i) {
        const auto& slot = tab_slots_[i];
        const int tw = (logical_w_ >= 320) ? slot.width_px : (logical_w_ / static_cast<int>(tab_slots_.size()));

        // Перевіряємо чи активне одне з вікон цього слота
        bool is_active = false;
        for (ViewId vid : slot.views) {
            if (ctx.app.active_view == vid) {
                is_active = true;
                break;
            }
        }

        // Перевірка утримання фізичної кнопки бампера
        bool is_held = false;
        if (slot.key_label == "[L1]") is_held = ctx.in.l1;
        else if (slot.key_label == "[L2]") is_held = ctx.in.l2;
        else if (slot.key_label == "[R2]") is_held = ctx.in.r2;
        else if (slot.key_label == "[R1]") is_held = ctx.in.r1;
        else if (slot.key_label == "[G]")  is_held = ctx.in.btn_fn;

        // Формуємо підпис: наприклад "[L1]son<SEQ>rol" або "[L2]fx<SYN>mod"
        std::string title = slot.key_label;
        ColorRGB slot_accent = palette::kAccentOrange;
        if (ctx.in.btn_select) {
            if (slot.key_label == "[L1]") title += " INSP";
            else if (slot.key_label == "[L2]") title += " ARP";
            else if (slot.key_label == "[R2]") title += " SCOPE";
            else if (slot.key_label == "[R1]") title += " PERF";
            else if (slot.key_label == "[G]")  title += "SYS";
            slot_accent = palette::kParamTrig;
        } else if (slot.key_label == "[L1]") {
            if (ctx.app.seq_submode == SeqSubmode::kPianoRoll) {
                title += "seq<ROL>son";
                slot_accent = palette::kParamTrig; // Жовтий
            } else if (ctx.app.seq_submode == SeqSubmode::kSong) {
                title += "rol<SON>seq";
                slot_accent = palette::kLevelWarnClip; // Червоний
            } else {
                title += "son<SEQ>rol";
                slot_accent = palette::kAccentOrange; // Помаранчевий
            }
        } else if (slot.key_label == "[L2]") {
            if (ctx.app.synth_submode == SynthSubmode::kMod) {
                title += "syn<MOD>fx";
                slot_accent = palette::kAccentPurple; // Фіолетовий
            } else if (ctx.app.synth_submode == SynthSubmode::kFx) {
                title += "mod<FX>syn";
                slot_accent = palette::kLevelOk; // Зелений
            } else {
                title += "fx<SYN>mod";
                slot_accent = palette::kAccentCyan; // Неоново-блакитний
            }
        } else if (slot.key_label == "[R2]") {
            if (ctx.app.mix_submode == MixSubmode::kSend) {
                title += "mix<SND>mstr";
                slot_accent = palette::kAccentPurple; // Бузковий
            } else if (ctx.app.mix_submode == MixSubmode::kMaster) {
                title += "snd<MSTR>mix";
                slot_accent = palette::kAccentOrange; // Золотистий
            } else {
                title += "mstr<MIX>snd";
                slot_accent = palette::kAccentCyan; // Ціан
            }
        } else if (slot.key_label == "[R1]") {
            title += "SAMPLE";
            slot_accent = palette::kAccentCyan;
        } else if (slot.key_label == "[G]") {
            title += "SYS";
            slot_accent = palette::kAccentOrange;
        }

        const int text_w = Canvas::textWidth(title);
        const int text_x = tx + (tw - text_w) / 2;

        if (is_active) {
            ColorRGB accent = slot_accent;
            if (ctx.in.btn_select) accent = palette::kParamTrig;
            cv.fill(tx + 1, 1, tw - 2, kTabBarH - 3, accent);
            cv.text(text_x, 5, title, palette::kBgDark);
        } else if (is_held) {
            cv.fill(tx + 1, 1, tw - 2, kTabBarH - 3, slot_accent);
            cv.text(text_x, 5, title, palette::kBgDark);
        } else if (ctx.in.btn_select) {
            cv.rect(tx + 1, 1, tw - 2, kTabBarH - 3, palette::kParamTrig);
            cv.text(text_x, 5, title, palette::kParamTrig);
        } else {
            cv.rect(tx + 1, 1, tw - 2, kTabBarH - 3, palette::kGridDim);
            cv.text(text_x, 5, title, (slot.key_label == "[G]") ? palette::kAccentOrange : palette::kTextMain);
        }

        tx += tw;
    }

    if (ctx.audio.isRecording()) {
        const bool blink = ((ctx.now_ms / 500) & 1);
        char rec_str[24];
        uint32_t sec = ctx.audio.getRecordedSeconds();
        std::snprintf(rec_str, sizeof(rec_str), "[REC %02d:%02d]", sec / 60, sec % 60);
        cv.fill(logical_w_ - 76, 2, 74, kTabBarH - 4, palette::kPanelBg);
        cv.rect(logical_w_ - 76, 2, 74, kTabBarH - 4, blink ? palette::kLevelWarnClip : palette::kTextDim);
        cv.text(logical_w_ - 72, 5, rec_str, blink ? palette::kLevelWarnClip : palette::kTextDim);
    }
}

void UiShell::renderHud(Canvas& cv, const UiContext& ctx) {
    const int hud_y = logical_h_ - kHudH;

    cv.fill(0, hud_y, logical_w_, kHudH, palette::kPanelBg);
    cv.line(0, hud_y, logical_w_ - 1, hud_y, palette::kGridDim);

    // 1. D-Pad Графіка
    widgets::dpad(cv, 12, hud_y + 19, ctx.in);
    cv.text(4, hud_y + 2, "DPAD", palette::kTextDim);

    // 2. L-Stick Box з динамічною мішенню
    const auto tp = ctx.audio.getTrackParams(ctx.app.selected_track);
    std::string l_dest = std::string(getStickDestShortName(tp.stick_left_x)) + "/" + getStickDestShortName(tp.stick_left_y);
    std::string r_dest = std::string(getStickDestShortName(tp.stick_right_x)) + "/" + getStickDestShortName(tp.stick_right_y);

    cv.text(26, hud_y + 2, l_dest, palette::kAccentCyan);
    widgets::stickBox(cv, 28, hud_y + 12, ctx.in.lx, ctx.in.ly, ctx.in.l3, palette::kAccentCyan);

    // 3. Центральна панель статусу (x = 56..202)
    cv.line(56, hud_y + 2, 56, logical_h_ - 2, palette::kGridDim);
    cv.line(202, hud_y + 2, 202, logical_h_ - 2, palette::kGridDim);

    // 1. Транспорт (піктограми Play/Stop), BPM, Аудіо, Шар
    if (ctx.app.transport_playing) {
        widgets::iconPlay(cv, 60, hud_y + 3, palette::kLevelOk);
    } else {
        widgets::iconStop(cv, 60, hud_y + 3, palette::kAccentOrange);
    }

    char bpm_buf[24];
    const int vol_pct = static_cast<int>(std::round(ctx.audio.getMasterVolume() * 100.0f));
    std::snprintf(bpm_buf, sizeof(bpm_buf), "%3.0f BPM %d%%", ctx.app.bpm, vol_pct);
    cv.text(68, hud_y + 3, bpm_buf, palette::kTextMain);

    const char* hp_str = ctx.in.headphones_plugged ? "[HP]" : "[SPK]";
    cv.text(148, hud_y + 3, hp_str, ctx.in.headphones_plugged ? palette::kAccentCyan : palette::kTextDim);

    const Layer cur_layer = router_.activeLayer(ctx);
    if (cur_layer == Layer::kHoldL2) {
        cv.text(174, hud_y + 3, "[MUTE]", palette::kLevelWarnClip);
    } else if (cur_layer == Layer::kSelect) {
        cv.text(174, hud_y + 3, "[SEL]", palette::kParamTrig);
    }

    // Рядок 2: Фокус активного вікна/параметра або модального екрана
    const ViewId active_v_id = activeViewId(ctx.app);
    std::string f_id;
    if (ctx.app.mod_picker_open) {
        f_id = "MOD TARGET: " + ctx.app.mod_picker_param_id;
    } else if (ctx.app.project_modal_open) {
        f_id = ctx.app.project_modal_is_save ? "PROJECT: SAVE AS" : "PROJECT: LOAD";
    } else if (ctx.app.sample_browser_open) {
        f_id = "SAMPLE BROWSER";
    } else if (ctx.app.engine_menu_open) {
        f_id = "SELECT SYNTH ENGINE";
    } else {
        IView* cur_v = findView(active_v_id);
        f_id = cur_v ? cur_v->focusId(ctx) : "GROOVEBOX";
    }
    cv.textClip(60, hud_y + 13, f_id, 34, palette::kAccentOrange);

    // Рядок 3: Підказки стіків L3/R3 та останній статус
    const bool is_arp = (active_v_id == ViewId::kArp);
    const char* l3_label = is_arp ? "L3:CLOSE" : (active_v_id == ViewId::kSeq ? "L3:ARP" : "");
    const char* r3_label = is_arp ? "R3:RND" : (active_v_id == ViewId::kSynth ? "R3:INIT" : "");
    ColorRGB l3_col = ctx.in.l3 ? palette::kParamTrig : (ctx.audio.getFillMode() ? palette::kLevelWarnClip : palette::kAccentCyan);
    ColorRGB r3_col = ctx.in.r3 ? palette::kParamTrig : palette::kAccentOrange;

    cv.text(60, hud_y + 22, l3_label, l3_col);
    if (r3_label[0] != '\0') {
        cv.text(98, hud_y + 22, r3_label, r3_col);
        cv.line(128, hud_y + 22, 128, hud_y + 28, palette::kGridDim);
        cv.textClip(132, hud_y + 22, ctx.app.last_action, 17, palette::kTextDim);
    } else {
        cv.line(98, hud_y + 22, 98, hud_y + 28, palette::kGridDim);
        cv.textClip(102, hud_y + 22, ctx.app.last_action, 24, palette::kTextDim);
    }

    // 4. R-Stick Box з динамічною мішенню
    cv.text(206, hud_y + 2, r_dest, palette::kAccentOrange);
    widgets::stickBox(cv, 212, hud_y + 12, ctx.in.rx, ctx.in.ry, ctx.in.r3, palette::kAccentOrange);

    // 5. ABXY Diamond Графіка
    widgets::abxy(cv, 244, hud_y + 18, ctx.in);

    // 6. Динамічні підписи кнопок (генеруються суворо з Keymap!)
    std::string lbl_x = router_.keymap().actionLabel(active_v_id, cur_layer, Button::kX);
    std::string lbl_y = router_.keymap().actionLabel(active_v_id, cur_layer, Button::kY);
    std::string lbl_a = router_.keymap().actionLabel(active_v_id, cur_layer, Button::kA);
    std::string lbl_b = router_.keymap().actionLabel(active_v_id, cur_layer, Button::kB);

    char r1_txt[32];
    std::snprintf(r1_txt, sizeof(r1_txt), "X:%-4s Y:%-4s", lbl_x.c_str(), lbl_y.c_str());
    cv.text(256, hud_y + 6, r1_txt, (cur_layer == Layer::kHoldL2) ? palette::kLevelWarnClip : palette::kTextMain);

    char r2_txt[32];
    std::snprintf(r2_txt, sizeof(r2_txt), "A:%-4s B:%-4s", lbl_a.c_str(), lbl_b.c_str());
    cv.text(256, hud_y + 18, r2_txt, (cur_layer == Layer::kHoldL2) ? palette::kLevelWarnClip : palette::kTextMain);
}

void UiShell::render(
    AppModel& app,
    AudioEngine& audio,
    HardwareMonitor* hw_mon,
    const InputState& in,
    const HardwareSnapshot& hw,
    const AudioEngineTelemetry& tel,
    const MidiTelemetry& midi,
    uint32_t now_ms
) {
    if (!renderer_) return;

    Canvas cv(renderer_, logical_w_, logical_h_);
    UiContext ctx{app, audio, hw_mon, in, hw, tel, midi, now_ms, this};

    // 1. Очищення фону
    cv.fill(0, 0, logical_w_, logical_h_, palette::kBgDark);

    // 2. Верхній рядок вкладок
    renderTabBar(cv, ctx);

    // 3. Робоча область активного вікна
    const Rect ws{0, kTabBarH, logical_w_, kWorkspaceH};
    IView* cur_v = findView(app.active_view);
    if (cur_v) {
        cur_v->render(cv, ctx, ws);
    }

    // Якщо відкрито модальний інспектор або арпеджиатор — малюємо його поверх робочої області!
    if (app.step_inspector_open) {
        IView* ins_v = findView(ViewId::kInspect);
        if (ins_v) {
            ins_v->render(cv, ctx, ws);
        }
    } else if (app.arp_modal_open) {
        IView* arp_v = findView(ViewId::kArp);
        if (arp_v) {
            arp_v->render(cv, ctx, ws);
        }
    }

    // Випадаюче модальне меню вибору рушія синтезу
    if (app.engine_menu_open) {
        const int mw = 180;
        const int mh = static_cast<int>(EngineType::kCount) * 11 + 18;
        const int mx = (logical_w_ - mw) / 2;
        const int my = (logical_h_ - mh) / 2;
        cv.fill(mx, my, mw, mh, palette::kPanelBg);
        cv.rect(mx, my, mw, mh, palette::kAccentCyan);
        cv.fill(mx + 1, my + 1, mw - 2, 10, palette::kHeaderSel);
        cv.text(mx + 4, my + 3, app.engine_menu_is_add ? "ADD TRACK: SELECT ENGINE" : "EDIT TRACK: SELECT ENGINE", palette::kAccentCyan);
        for (int e = 0; e < static_cast<int>(EngineType::kCount); ++e) {
            const int ey = my + 14 + e * 11;
            const bool sel = (e == app.engine_menu_sel);
            if (sel) {
                cv.fill(mx + 3, ey, mw - 6, 10, palette::kHeaderSel);
                cv.rect(mx + 3, ey, mw - 6, 10, palette::kAccentCyan);
            }
            std::string label = getEngineTypeName(static_cast<EngineType>(e));
            if (static_cast<EngineType>(e) == EngineType::kSampleOneShot) label = "SAMPLER (ONE-SHOT)";
            else if (static_cast<EngineType>(e) == EngineType::kSliceSampler) label = "LOOP SLICER";
            cv.text(mx + 8, ey + 2, label, sel ? palette::kAccentCyan : palette::kTextMain);
        }
    }

    // Модальне контекстне меню вибору таргету модуляції (MOD TARGET PICKER)
    if (app.mod_picker_open) {
        const int mw = 220;
        const int mh = 148;
        const int mx = (logical_w_ - mw) / 2;
        const int my = (logical_h_ - mh) / 2;
        cv.fill(mx, my, mw, mh, palette::kPanelBg);
        cv.rect(mx, my, mw, mh, palette::kAccentPurple);
        cv.fill(mx + 1, my + 1, mw - 2, 11, palette::kHeaderSel);
        std::string hdr = "MOD TARGET: " + app.mod_picker_param_id;
        cv.textClip(mx + 4, my + 3, hdr, (mw - 8) / Canvas::kCharW, palette::kAccentPurple);

        const int kVis = 11;
        const int total_targets = static_cast<int>(ModTarget::kCount);
        int scroll = std::clamp(app.mod_picker_sel - kVis / 2, 0, std::max(0, total_targets - kVis));

        for (int i = 0; i < kVis && (scroll + i) < total_targets; ++i) {
            int t_idx = scroll + i;
            const int ey = my + 15 + i * 11;
            const bool sel = (t_idx == app.mod_picker_sel);
            if (sel) {
                cv.fill(mx + 3, ey, mw - 12, 10, palette::kHeaderSel);
                cv.rect(mx + 3, ey, mw - 12, 10, palette::kAccentPurple);
            }
            auto tgt = static_cast<ModTarget>(t_idx);
            char row_str[64];
            std::snprintf(row_str, sizeof(row_str), "[%-3s] %-5s %s", getModTargetCategory(tgt), getModTargetShortName(tgt), getModTargetLongName(tgt));
            cv.textClip(mx + 6, ey + 2, row_str, (mw - 18) / Canvas::kCharW, sel ? palette::kAccentPurple : palette::kTextMain);
        }

        // Вертикальна смуга скролу
        if (total_targets > kVis) {
            const int bar_x = mx + mw - 6;
            const int bar_y0 = my + 15;
            const int bar_h = kVis * 11;
            cv.fill(bar_x, bar_y0, 2, bar_h, palette::kPanelBg);
            const int thumb_h = std::max(8, bar_h * kVis / total_targets);
            const int thumb_y = bar_y0 + (bar_h - thumb_h) * scroll / std::max(1, total_targets - kVis);
            cv.fill(bar_x, thumb_y, 2, thumb_h, palette::kAccentPurple);
        }
    }

    // Модальне вікно вибору семплів (Sample Pool & Filesystem)
    if (app.sample_browser_open) {
        const int bw = 260;
        const int bh = 144;
        const int bx = (logical_w_ - bw) / 2;
        const int by = (logical_h_ - bh) / 2;

        cv.fill(bx, by, bw, bh, palette::kPanelBg);
        cv.rect(bx, by, bw, bh, palette::kAccentCyan);
        cv.fill(bx + 1, by + 1, bw - 2, 11, palette::kHeaderSel);

        std::string disp_dir = app.sample_browser_dir;
        auto pos = disp_dir.find("samples");
        if (pos != std::string::npos) disp_dir = disp_dir.substr(pos);
        char hdr[80];
        std::snprintf(hdr, sizeof(hdr), "SAMPLE POOL: %s", disp_dir.c_str());
        cv.text(bx + 4, by + 3, hdr, palette::kAccentCyan);

        const int max_vis = 8;
        const int total_items = static_cast<int>(app.sample_browser_items.size());
        int scroll = 0;
        if (app.sample_browser_sel >= max_vis) {
            scroll = app.sample_browser_sel - max_vis + 1;
        }
        scroll = std::clamp(scroll, 0, std::max(0, total_items - max_vis));

        for (int i = 0; i < max_vis && scroll + i < total_items; ++i) {
            const int idx = scroll + i;
            const int iy = by + 16 + i * 13;
            const bool sel = (idx == app.sample_browser_sel);
            if (sel) {
                cv.fill(bx + 3, iy, bw - 6, 12, palette::kHeaderSel);
                cv.rect(bx + 3, iy, bw - 6, 12, palette::kAccentCyan);
            }
            const std::string& item = app.sample_browser_items[idx];
            bool is_dir = (item.rfind("[DIR] ", 0) == 0 || item.rfind("[..]", 0) == 0);
            ColorRGB col = sel ? palette::kAccentCyan : (is_dir ? palette::kAccentOrange : palette::kTextMain);
            cv.text(bx + 8, iy + 3, item, col);
        }
    }

    // Модальне вікно збереження / завантаження проектів (Save As / Load Project)
    if (app.project_modal_open) {
        const int bw = 260;
        const int bh = 144;
        const int bx = (logical_w_ - bw) / 2;
        const int by = (logical_h_ - bh) / 2;

        cv.fill(bx, by, bw, bh, palette::kPanelBg);
        cv.rect(bx, by, bw, bh, palette::kAccentCyan);
        cv.fill(bx + 1, by + 1, bw - 2, 11, palette::kHeaderSel);

        char hdr[80];
        std::snprintf(hdr, sizeof(hdr), "PROJECT: %s", app.project_modal_is_save ? "SAVE AS" : "LOAD PROJECT");
        cv.text(bx + 4, by + 3, hdr, palette::kAccentCyan);

        const int max_vis = 8;
        const int total_items = static_cast<int>(app.project_files.size());
        int scroll = 0;
        if (app.project_modal_sel >= max_vis) {
            scroll = app.project_modal_sel - max_vis + 1;
        }
        scroll = std::clamp(scroll, 0, std::max(0, total_items - max_vis));

        if (total_items == 0) {
            cv.text(bx + 12, by + 40, "(No existing projects found)", palette::kTextDim);
            if (app.project_modal_is_save) {
                cv.text(bx + 12, by + 56, "[A] Create 'project_01.xgproj'", palette::kLevelOk);
            }
        } else {
            for (int i = 0; i < max_vis && scroll + i < total_items; ++i) {
                const int idx = scroll + i;
                const int iy = by + 16 + i * 13;
                const bool sel = (idx == app.project_modal_sel);
                if (sel) {
                    cv.fill(bx + 3, iy, bw - 6, 12, palette::kHeaderSel);
                    cv.rect(bx + 3, iy, bw - 6, 12, palette::kAccentCyan);
                }
                const std::string& item = app.project_files[idx];
                cv.text(bx + 8, iy + 3, item, sel ? palette::kAccentCyan : palette::kTextMain);
            }
        }
    }

    // 4. Нижній інформаційний рядок (HUD)
    renderHud(cv, ctx);

    SDL_RenderPresent(renderer_);
}

} // namespace groovebox
