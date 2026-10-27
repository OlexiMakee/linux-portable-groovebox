#include "ui/keymap.hpp"
#include "ui/actions.hpp"

#include <cstdio>

namespace groovebox {

Keymap::Keymap() {
    const ViewId ALL = ViewId::kCount;

    // =========================================================================
    // 1. ГЛОБАЛЬНІ ПРИВ'ЯЗКИ (ALL VIEWS)
    // =========================================================================
    // Перемикання вкладок верхнього ряду: L1, L2, R2, R1, FN (SYS)
    // Зверніть увагу: R2 стоїть лівіше за R1 згідно з ергономічним запитом!
    bind(ALL, Layer::kBase, Button::kL1, Gesture::kPress, "TAB.L1");
    bind(ALL, Layer::kBase, Button::kL2, Gesture::kPress, "TAB.L2");
    bind(ALL, Layer::kBase, Button::kR2, Gesture::kPress, "TAB.R2");
    bind(ALL, Layer::kBase, Button::kR1, Gesture::kPress, "TAB.R1");
    bind(ALL, Layer::kBase, Button::kFn, Gesture::kPress, "TAB.G");

    bind(ALL, Layer::kBase, Button::kStart, Gesture::kPress, "PLAY.TOGGLE");
    bind(ALL, Layer::kBase, Button::kVolUp, Gesture::kPress, "VOL.INC");
    bind(ALL, Layer::kBase, Button::kVolDown, Gesture::kPress, "VOL.DEC");

    // Аналогові стіки: L3 (клік лівого стіка) та R3 (клік правого стіка)
    bind(ALL, Layer::kBase, Button::kL3, Gesture::kHold, "FILL.ON");
    bind(ALL, Layer::kBase, Button::kL3, Gesture::kTap,  "ARP.TOGGLE");
    bind(ALL, Layer::kBase, Button::kR3, Gesture::kPress, "PARAM.DEFAULT");
    bind(ALL, Layer::kSelect, Button::kR3, Gesture::kPress, "PARAM.RANDOMIZE");

    // D-Pad навігація за замовчуванням
    bind(ALL, Layer::kBase, Button::kDpadUp,    Gesture::kPress, "NAV.UP");
    bind(ALL, Layer::kBase, Button::kDpadDown,  Gesture::kPress, "NAV.DOWN");
    bind(ALL, Layer::kBase, Button::kDpadLeft,  Gesture::kPress, "NAV.LEFT");
    bind(ALL, Layer::kBase, Button::kDpadRight, Gesture::kPress, "NAV.RIGHT");

    // Швидка навігація та темп при затиснутому SELECT
    bind(ALL, Layer::kSelect, Button::kVolUp,    Gesture::kPress, "BPM.INC");
    bind(ALL, Layer::kSelect, Button::kVolDown,  Gesture::kPress, "BPM.DEC");
    bind(ALL, Layer::kSelect, Button::kDpadUp,    Gesture::kPress, "NAV.PAGE_UP");
    bind(ALL, Layer::kSelect, Button::kDpadDown,  Gesture::kPress, "NAV.PAGE_DOWN");
    bind(ALL, Layer::kSelect, Button::kDpadLeft,  Gesture::kPress, "NAV.BAR_LEFT");
    bind(ALL, Layer::kSelect, Button::kDpadRight, Gesture::kPress, "NAV.BAR_RIGHT");

    // Швидкі комбінації SELECT + Shoulder (альтернативні екрани)
    bind(ALL, Layer::kSelect, Button::kL1, Gesture::kPress, "TAB.SEL_L1");
    bind(ALL, Layer::kSelect, Button::kL2, Gesture::kPress, "TAB.SEL_L2");
    bind(ALL, Layer::kSelect, Button::kR2, Gesture::kPress, "TAB.SEL_R2");
    bind(ALL, Layer::kSelect, Button::kR1, Gesture::kPress, "TAB.SEL_R1");

    // =========================================================================
    // 2. СЕКВЕНСОР (ViewId::kSeq)
    // =========================================================================
    // Базовий шар
    bind(ViewId::kSeq, Layer::kBase, Button::kA, Gesture::kTap,   "SEQ.TRIG");
    bind(ViewId::kSeq, Layer::kBase, Button::kA, Gesture::kHold,  "INSPECT.OPEN");
    bind(ViewId::kSeq, Layer::kBase, Button::kB, Gesture::kPress, "SEQ.CLEAR_STEP");
    bind(ViewId::kSeq, Layer::kBase, Button::kX, Gesture::kPress, "SEQ.NOTE_DOWN");
    bind(ViewId::kSeq, Layer::kBase, Button::kY, Gesture::kPress, "SEQ.NOTE_UP");

    // SELECT шар у секвенсорі (Elektron Pattern / Track Operations & Polymetry)
    bind(ViewId::kSeq, Layer::kSelect, Button::kA,         Gesture::kPress, "STEP.PREVIEW");
    bind(ViewId::kSeq, Layer::kSelect, Button::kB,         Gesture::kPress, "SEQ.DEL_TRACK_SAFE");
    bind(ViewId::kSeq, Layer::kSelect, Button::kX,         Gesture::kPress, "SEQ.CYCLE_LEN");
    bind(ViewId::kSeq, Layer::kSelect, Button::kY,         Gesture::kPress, "SEQ.CYCLE_PAGE");
    bind(ViewId::kSeq, Layer::kSelect, Button::kDpadLeft,  Gesture::kPress, "SEQ.LEN_DEC");
    bind(ViewId::kSeq, Layer::kSelect, Button::kDpadRight, Gesture::kPress, "SEQ.LEN_INC");
    bind(ViewId::kSeq, Layer::kSelect, Button::kStart,     Gesture::kPress, "ARP.OPEN");

    // HOLD L2 шар у секвенсорі: Elektron Live Quick Mute Mode для треків 1-4!
    bind(ViewId::kSeq, Layer::kHoldL2, Button::kX, Gesture::kPress, "MUTE.T1");
    bind(ViewId::kSeq, Layer::kHoldL2, Button::kY, Gesture::kPress, "MUTE.T2");
    bind(ViewId::kSeq, Layer::kHoldL2, Button::kA, Gesture::kPress, "MUTE.T3");
    bind(ViewId::kSeq, Layer::kHoldL2, Button::kB, Gesture::kPress, "MUTE.T4");

    // =========================================================================
    // 3. СИНТЕЗАТОР (ViewId::kSynth)
    // =========================================================================
    bind(ViewId::kSynth, Layer::kBase, Button::kB, Gesture::kPress, "PARAM.INC");
    bind(ViewId::kSynth, Layer::kBase, Button::kA, Gesture::kPress, "PARAM.DEC");
    bind(ViewId::kSynth, Layer::kBase, Button::kX, Gesture::kPress, "PARAM.DEC_FAST");
    bind(ViewId::kSynth, Layer::kBase, Button::kY, Gesture::kPress, "PARAM.INC_FAST");

    bind(ViewId::kSynth, Layer::kSelect, Button::kA, Gesture::kPress, "TRACK.PREVIEW_NOTE");
    bind(ViewId::kSynth, Layer::kSelect, Button::kX, Gesture::kPress, "TRACK.PREV");
    bind(ViewId::kSynth, Layer::kSelect, Button::kY, Gesture::kPress, "TRACK.NEXT");
    bind(ViewId::kSynth, Layer::kSelect, Button::kR3, Gesture::kPress, "SYNTH.RANDOMIZE");
    bind(ViewId::kSynth, Layer::kBase, Button::kR3, Gesture::kPress, "PARAM.DEFAULT");
    bind(ViewId::kSynth, Layer::kBase, Button::kL3, Gesture::kTap, "ARP.TOGGLE");

    // =========================================================================
    // 4. ЕФЕКТИ (ViewId::kFx)
    // =========================================================================
    bind(ViewId::kFx, Layer::kBase, Button::kB, Gesture::kPress, "PARAM.INC");
    bind(ViewId::kFx, Layer::kBase, Button::kA, Gesture::kPress, "PARAM.DEC");
    bind(ViewId::kFx, Layer::kBase, Button::kX, Gesture::kPress, "PARAM.DEC_FAST");
    bind(ViewId::kFx, Layer::kBase, Button::kY, Gesture::kPress, "PARAM.INC_FAST");

    // =========================================================================
    // 5. МІКШЕР (ViewId::kMix)
    // =========================================================================
    bind(ViewId::kMix, Layer::kBase, Button::kB, Gesture::kPress, "PARAM.INC");
    bind(ViewId::kMix, Layer::kBase, Button::kA, Gesture::kPress, "PARAM.DEC");
    bind(ViewId::kMix, Layer::kBase, Button::kX, Gesture::kPress, "MUTE.CUR");
    bind(ViewId::kMix, Layer::kBase, Button::kY, Gesture::kPress, "MUTE.CUR");

    // =========================================================================
    // 6. ОСЦИЛОГРАФ (ViewId::kScope)
    // =========================================================================
    bind(ViewId::kScope, Layer::kBase, Button::kA, Gesture::kPress, "MUTE.CUR");
    bind(ViewId::kScope, Layer::kBase, Button::kB, Gesture::kPress, "VIEW.BACK");

    // =========================================================================
    // 7. СИСТЕМНІ НАЛАШТУВАННЯ (ViewId::kSys)
    // =========================================================================
    bind(ViewId::kSys, Layer::kBase, Button::kB, Gesture::kPress, "PARAM.INC");
    bind(ViewId::kSys, Layer::kBase, Button::kA, Gesture::kPress, "PARAM.DEC");
    bind(ViewId::kSys, Layer::kBase, Button::kX, Gesture::kPress, "PARAM.DEC_FAST");
    bind(ViewId::kSys, Layer::kBase, Button::kY, Gesture::kPress, "PARAM.INC_FAST");
    bind(ViewId::kSys, Layer::kSelect, Button::kB, Gesture::kPress, "VIEW.BACK");

    // =========================================================================
    // 8. STEP INSPECTOR (ViewId::kInspect)
    // =========================================================================
    bind(ViewId::kInspect, Layer::kBase, Button::kB, Gesture::kPress, "PARAM.INC");
    bind(ViewId::kInspect, Layer::kBase, Button::kA, Gesture::kPress, "PARAM.DEC");
    bind(ViewId::kInspect, Layer::kBase, Button::kX, Gesture::kPress, "PARAM.DEC_FAST");
    bind(ViewId::kInspect, Layer::kBase, Button::kY, Gesture::kPress, "PARAM.INC_FAST");

    // =========================================================================
    // 9. ЖИВИЙ ПЕРФОРМАНС (ViewId::kPerf)
    // =========================================================================
    bind(ViewId::kPerf, Layer::kBase, Button::kDpadUp,    Gesture::kPress, "PERF.OCT_UP");
    bind(ViewId::kPerf, Layer::kBase, Button::kDpadDown,  Gesture::kPress, "PERF.OCT_DOWN");
    bind(ViewId::kPerf, Layer::kBase, Button::kDpadLeft,  Gesture::kPress, "TRACK.PREV");
    bind(ViewId::kPerf, Layer::kBase, Button::kDpadRight, Gesture::kPress, "TRACK.NEXT");
    bind(ViewId::kPerf, Layer::kBase, Button::kA,         Gesture::kPress, "PERF.PAD_A");
    bind(ViewId::kPerf, Layer::kBase, Button::kB,         Gesture::kPress, "PERF.PAD_B");
    bind(ViewId::kPerf, Layer::kBase, Button::kX,         Gesture::kPress, "PERF.PAD_X");
    bind(ViewId::kPerf, Layer::kBase, Button::kY,         Gesture::kPress, "PERF.PAD_Y");
    bind(ViewId::kPerf, Layer::kSelect, Button::kB,       Gesture::kPress, "VIEW.BACK");

    // =========================================================================
    // 10. АРПЕДЖИРАТОР ТА ПРОЦЕДУРНИЙ ГЕНЕРАТОР (ViewId::kArp)
    // =========================================================================
    bind(ViewId::kArp, Layer::kBase, Button::kB,         Gesture::kPress, "PARAM.INC");
    bind(ViewId::kArp, Layer::kBase, Button::kA,         Gesture::kPress, "PARAM.DEC");
    bind(ViewId::kArp, Layer::kBase, Button::kX,         Gesture::kPress, "PARAM.DEC_FAST");
    bind(ViewId::kArp, Layer::kBase, Button::kY,         Gesture::kPress, "PARAM.INC_FAST");
    bind(ViewId::kArp, Layer::kBase, Button::kL3,        Gesture::kTap,   "ARP.TOGGLE");
    bind(ViewId::kArp, Layer::kBase, Button::kR3,        Gesture::kPress, "ARP.RANDOMIZE");
    bind(ViewId::kArp, Layer::kSelect, Button::kR3,      Gesture::kPress, "ARP.RANDOMIZE");
    bind(ViewId::kArp, Layer::kSelect, Button::kA,       Gesture::kPress, "ARP.COMMIT");
    bind(ViewId::kArp, Layer::kSelect, Button::kB,       Gesture::kPress, "ARP.CLOSE");
}

void Keymap::bind(KeyBinding b) {
    bindings_.push_back(std::move(b));
}

void Keymap::bind(ViewId v, Layer l, Button b, Gesture g, std::string action_id) {
    bind(KeyBinding{v, l, b, g, std::move(action_id)});
}

const std::string* Keymap::lookup(ViewId view, Layer layer, Button button, Gesture gesture) const {
    // 1. Точний пошук для поточного вікна, шару та жесту
    for (const auto& b : bindings_) {
        if (b.view == view && b.layer == layer && b.button == button && b.gesture == gesture) {
            return &b.action_id;
        }
    }
    // 2. Якщо жест Tap/Hold не знайдено для кнопки, дивимось чи є звичайний Press
    if (gesture != Gesture::kPress) {
        for (const auto& b : bindings_) {
            if (b.view == view && b.layer == layer && b.button == button && b.gesture == Gesture::kPress) {
                return &b.action_id;
            }
        }
    }
    // 3. Fallback: глобальна прив'язка (ViewId::kCount) для цього шару
    for (const auto& b : bindings_) {
        if (b.view == ViewId::kCount && b.layer == layer && b.button == button && b.gesture == gesture) {
            return &b.action_id;
        }
    }
    // 4. Fallback: глобальний Press
    if (gesture != Gesture::kPress) {
        for (const auto& b : bindings_) {
            if (b.view == ViewId::kCount && b.layer == layer && b.button == button && b.gesture == Gesture::kPress) {
                return &b.action_id;
            }
        }
    }
    return nullptr;
}

std::string Keymap::actionLabel(ViewId view, Layer layer, Button button) const {
    const std::string* act_id = lookup(view, layer, button, Gesture::kPress);
    if (!act_id) {
        act_id = lookup(view, layer, button, Gesture::kTap);
    }
    if (act_id) {
        const Action* a = ActionRegistry::instance().find(*act_id);
        if (a) return a->label;
    }
    return "--";
}

// =========================================================================
// InputRouter
// =========================================================================
InputRouter::InputRouter(Keymap keymap) : keymap_(std::move(keymap)) {}

Layer InputRouter::activeLayer(const UiContext& ctx) const {
    if (ctx.app.active_view == ViewId::kSeq && ctx.in.l2) {
        return Layer::kHoldL2; // MUTE режим
    }
    if (ctx.in.btn_select) {
        return Layer::kSelect;
    }
    return Layer::kBase;
}

void InputRouter::consume(Button b) {
    consumed_[static_cast<int>(b)] = true;
}

void InputRouter::dispatch(UiContext& ctx, ViewId view, Layer layer, Button btn, Gesture gest) {
    const std::string* act_id = keymap_.lookup(view, layer, btn, gest);
    if (act_id) {
        ActionRegistry::instance().run(*act_id, ctx);
    }
}

void InputRouter::processInput(UiContext& ctx) {
    const auto& in = ctx.in;
    ViewId view = ctx.app.active_view;
    if (ctx.app.step_inspector_open) {
        view = ViewId::kInspect;
    } else if (ctx.app.arp_modal_open) {
        view = ViewId::kArp;
    }
    const Layer layer = activeLayer(ctx);

    // Масив поточного фізичного стану кнопок
    struct BtnEvent {
        Button id;
        bool is_down;
        bool pressed_edge;
        bool released_edge;
    };

    const BtnEvent events[kButtonCount] = {
        {Button::kDpadUp,    in.dpad_up,    in.up_pressed,    !in.dpad_up && is_down_[0]},
        {Button::kDpadDown,  in.dpad_down,  in.down_pressed,  !in.dpad_down && is_down_[1]},
        {Button::kDpadLeft,  in.dpad_left,  in.left_pressed,  !in.dpad_left && is_down_[2]},
        {Button::kDpadRight, in.dpad_right, in.right_pressed, !in.dpad_right && is_down_[3]},
        {Button::kA,         in.btn_a,      in.a_pressed,     !in.btn_a && is_down_[4]},
        {Button::kB,         in.btn_b,      in.b_pressed,     !in.btn_b && is_down_[5]},
        {Button::kX,         in.btn_x,      in.x_pressed,     !in.btn_x && is_down_[6]},
        {Button::kY,         in.btn_y,      in.y_pressed,     !in.btn_y && is_down_[7]},
        {Button::kL1,        in.l1,         in.l1_pressed,    !in.l1 && is_down_[8]},
        {Button::kL2,        in.l2,         in.l2_pressed,    !in.l2 && is_down_[9]},
        {Button::kR1,        in.r1,         in.r1_pressed,    !in.r1 && is_down_[10]},
        {Button::kR2,        in.r2,         in.r2_pressed,    !in.r2 && is_down_[11]},
        {Button::kL3,        in.l3,         in.l3_pressed,    !in.l3 && is_down_[12]},
        {Button::kR3,        in.r3,         in.r3_pressed,    !in.r3 && is_down_[13]},
        {Button::kStart,     in.btn_start,  in.start_pressed, !in.btn_start && is_down_[14]},
        {Button::kSelect,    in.btn_select, in.select_pressed,!in.btn_select && is_down_[15]},
        {Button::kFn,        in.btn_fn,     in.fn_pressed,    !in.btn_fn && is_down_[16]},
        {Button::kVolUp,     in.vol_up,     in.vol_up_pressed,!in.vol_up && is_down_[17]},
        {Button::kVolDown,   in.vol_down,   in.vol_down_pressed,!in.vol_down && is_down_[18]},
    };

    for (int i = 0; i < kButtonCount; ++i) {
        const auto& ev = events[i];
        const int idx = static_cast<int>(ev.id);

        if (ev.pressed_edge) {
            press_time_ms_[idx] = ctx.now_ms;
            hold_fired_[idx] = false;
            consumed_[idx] = false;

            // Якщо для кнопки є жест Tap або Hold — чекаємо, інакше викликаємо Press одразу!
            bool has_tap_or_hold = (keymap_.lookup(view, layer, ev.id, Gesture::kTap) != nullptr) ||
                                   (keymap_.lookup(view, layer, ev.id, Gesture::kHold) != nullptr);

            if (!has_tap_or_hold) {
                dispatch(ctx, view, layer, ev.id, Gesture::kPress);
            }
        }

        // Перевірка тривалого утримання (>= 200 мс)
        if (ev.is_down && !hold_fired_[idx] && !consumed_[idx] && (ctx.now_ms - press_time_ms_[idx] >= 200)) {
            hold_fired_[idx] = true;
            dispatch(ctx, view, layer, ev.id, Gesture::kHold);
        }

        // Автоповтор та довге утримання для параметрів A / B (крім SEQ)
        if (ev.is_down && (ev.id == Button::kA || ev.id == Button::kB) && view != ViewId::kSeq) {
            uint32_t elapsed = ctx.now_ms - press_time_ms_[idx];
            if (elapsed >= 250) {
                // Якщо утримуємо A або B на виборі ENGINE у SYNTH — відкриваємо селектор рушіїв
                bool on_engine = (view == ViewId::kSynth && ctx.app.grid_row[static_cast<int>(ViewId::kSynth)] == 0 && ctx.app.grid_col[static_cast<int>(ViewId::kSynth)] == 0);
                if (on_engine) {
                    if (!hold_fired_[idx]) {
                        hold_fired_[idx] = true;
                        ActionRegistry::instance().run("ENGINE.MENU_OPEN", ctx);
                    }
                } else {
                    if (ctx.now_ms - repeat_last_ms_[idx] >= 60) {
                        repeat_last_ms_[idx] = ctx.now_ms;
                        dispatch(ctx, view, layer, ev.id, Gesture::kPress);
                    }
                }
            }
        }

        // Відпускання кнопки
        if (ev.released_edge) {
            if (!hold_fired_[idx] && !consumed_[idx]) {
                dispatch(ctx, view, layer, ev.id, Gesture::kTap);
            }
            if (ev.id == Button::kL3 && hold_fired_[idx]) {
                ActionRegistry::instance().run("FILL.OFF", ctx);
            }
            if (ev.id == Button::kA && ctx.app.step_inspector_open) {
                // Закриваємо інспектор при відпусканні кнопки A
                ActionRegistry::instance().run("INSPECT.CLOSE", ctx);
            }
        }

        is_down_[idx] = ev.is_down;
    }
}

} // namespace groovebox
