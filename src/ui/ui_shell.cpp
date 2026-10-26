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
        IView* active_in_slot = nullptr;
        for (ViewId vid : slot.views) {
            if (ctx.app.active_view == vid) {
                is_active = true;
                active_in_slot = findView(vid);
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

        // Формуємо підпис: наприклад "[L1] SYNTH" або "[R2] MIX"
        std::string title = slot.key_label;
        if (is_active && active_in_slot) {
            title += " " + active_in_slot->tabTitle(ctx);
        } else if (!slot.views.empty()) {
            IView* v0 = findView(slot.views[0]);
            if (v0) title += " " + v0->tabTitle(ctx);
        }

        const int text_w = Canvas::textWidth(title);
        const int text_x = tx + (tw - text_w) / 2;

        if (is_active) {
            ColorRGB accent = active_in_slot ? active_in_slot->accent() : palette::kAccentOrange;
            cv.fill(tx + 1, 1, tw - 2, kTabBarH - 3, accent);
            cv.text(text_x, 5, title, palette::kBgDark);
        } else if (is_held) {
            cv.fill(tx + 1, 1, tw - 2, kTabBarH - 3, palette::kAccentCyan);
            cv.text(text_x, 5, title, palette::kBgDark);
        } else {
            cv.rect(tx + 1, 1, tw - 2, kTabBarH - 3, palette::kGridDim);
            cv.text(text_x, 5, title, (slot.key_label == "[G]") ? palette::kAccentOrange : palette::kTextMain);
        }

        tx += tw;
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

    // Рядок 1: Транспорт, BPM, Аудіо, Шар
    const char* play_str = ctx.app.transport_playing ? "PLAY" : "STOP";
    ColorRGB play_col = ctx.app.transport_playing ? palette::kLevelOk : palette::kAccentOrange;
    cv.text(60, hud_y + 3, play_str, play_col);

    char bpm_buf[16];
    std::snprintf(bpm_buf, sizeof(bpm_buf), "%3.0f BPM", ctx.app.bpm);
    cv.text(94, hud_y + 3, bpm_buf, palette::kTextMain);

    const char* hp_str = ctx.in.headphones_plugged ? "[HP]" : "[SPK]";
    cv.text(148, hud_y + 3, hp_str, ctx.in.headphones_plugged ? palette::kAccentCyan : palette::kTextDim);

    const Layer cur_layer = router_.activeLayer(ctx);
    if (cur_layer == Layer::kHoldL2) {
        cv.text(174, hud_y + 3, "[MUTE]", palette::kLevelWarnClip);
    } else if (cur_layer == Layer::kSelect) {
        cv.text(174, hud_y + 3, "[SEL]", palette::kParamTrig);
    }

    // Рядок 2: Фокус активного вікна/параметра
    IView* cur_v = ctx.app.step_inspector_open ? findView(ViewId::kInspect) : findView(ctx.app.active_view);
    std::string f_id = cur_v ? cur_v->focusId(ctx) : "GROOVEBOX";
    cv.textClip(60, hud_y + 13, f_id, 34, palette::kAccentOrange);

    // Рядок 3: Останній статус/дія
    std::string act_info = "ACT: " + ctx.app.last_action;
    cv.textClip(60, hud_y + 22, act_info, 34, palette::kTextDim);

    // 4. R-Stick Box з динамічною мішенню
    cv.text(206, hud_y + 2, r_dest, palette::kAccentOrange);
    widgets::stickBox(cv, 212, hud_y + 12, ctx.in.rx, ctx.in.ry, ctx.in.r3, palette::kAccentOrange);

    // 5. ABXY Diamond Графіка
    widgets::abxy(cv, 244, hud_y + 18, ctx.in);

    // 6. Динамічні підписи кнопок (генеруються суворо з Keymap!)
    const ViewId active_v_id = ctx.app.step_inspector_open ? ViewId::kInspect : ctx.app.active_view;
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

    // Якщо відкрито модальний інспектор — малюємо його поверх робочої області!
    if (app.step_inspector_open) {
        IView* ins_v = findView(ViewId::kInspect);
        if (ins_v) {
            ins_v->render(cv, ctx, ws);
        }
    }

    // 4. Нижній інформаційний рядок (HUD)
    renderHud(cv, ctx);

    SDL_RenderPresent(renderer_);
}

} // namespace groovebox
