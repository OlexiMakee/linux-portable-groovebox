#include "ui/actions.hpp"
#include "ui/params.hpp"
#include "ui/views.hpp"
#include "ui/ui_shell.hpp"

#include <algorithm>
#include <cstdio>

namespace groovebox {

ActionRegistry& ActionRegistry::instance() {
    static ActionRegistry reg;
    return reg;
}

const Action* ActionRegistry::find(std::string_view id) const {
    auto it = map_.find(std::string(id));
    return it == map_.end() ? nullptr : &it->second;
}

void ActionRegistry::add(Action a) {
    std::string key = a.id;
    map_[key] = std::move(a);
}

bool ActionRegistry::run(std::string_view id, UiContext& ctx) const {
    const Action* a = find(id);
    if (!a) {
        std::fprintf(stderr, "[ACTION] unknown action '%s'\n", std::string(id).c_str());
        return false;
    }
    a->execute(ctx);
    ctx.app.last_action = a->label;
    return true;
}

std::vector<std::string> ActionRegistry::ids() const {
    std::vector<std::string> out;
    out.reserve(map_.size());
    for (const auto& kv : map_) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

ActionRegistry::ActionRegistry() {
    // ---------------- Навігація ----------------
    add(Action{"NAV.UP", "UP", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, 0, -1, false);
    }});
    add(Action{"NAV.DOWN", "DOWN", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, 0, 1, false);
    }});
    add(Action{"NAV.LEFT", "LEFT", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, -1, 0, false);
    }});
    add(Action{"NAV.RIGHT", "RIGHT", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, 1, 0, false);
    }});
    add(Action{"NAV.PAGE_UP", "PG.UP", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, 0, -1, true);
    }});
    add(Action{"NAV.PAGE_DOWN", "PG.DN", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, 0, 1, true);
    }});
    add(Action{"NAV.BAR_LEFT", "BAR.L", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, -1, 0, true);
    }});
    add(Action{"NAV.BAR_RIGHT", "BAR.R", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, 1, 0, true);
    }});

    // ---------------- Редагування параметра у фокусі ----------------
    add(Action{"PARAM.INC", "VAL+", [](UiContext& c) {
        if (c.shell) {
            const Param* p = c.shell->currentView(c).focusedParam(c);
            if (p && p->step) p->step(c, 1, false);
        }
    }});
    add(Action{"PARAM.DEC", "VAL-", [](UiContext& c) {
        if (c.shell) {
            const Param* p = c.shell->currentView(c).focusedParam(c);
            if (p && p->step) p->step(c, -1, false);
        }
    }});
    add(Action{"PARAM.INC_FAST", "VAL++", [](UiContext& c) {
        if (c.shell) {
            const Param* p = c.shell->currentView(c).focusedParam(c);
            if (p && p->step) p->step(c, 1, true);
        }
    }});
    add(Action{"PARAM.DEC_FAST", "VAL--", [](UiContext& c) {
        if (c.shell) {
            const Param* p = c.shell->currentView(c).focusedParam(c);
            if (p && p->step) p->step(c, -1, true);
        }
    }});

    // ---------------- Секвенсор: кроки, ноти, патерни ----------------
    add(Action{"SEQ.TRIG", "TRIG", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (c.app.cursor_row >= act) {
            if (act < kTotalRows) {
                c.audio.setActiveRowCount(act + kVoicesPerTrack);
                c.app.cursor_row = act;
                c.app.selected_track = act / kVoicesPerTrack;
            }
        } else {
            c.audio.toggleStep(c.app.cursor_row, c.app.cursor_step);
        }
    }});
    add(Action{"SEQ.CLEAR_STEP", "DEL", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.clearStep(c.app.cursor_row, c.app.cursor_step);
        }
    }});
    add(Action{"SEQ.CLEAR_ROW", "CLEAR", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.clearRow(c.app.cursor_row);
        }
    }});
    add(Action{"SEQ.RANDOMIZE", "RANDOM", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.randomizeRow(c.app.cursor_row);
        }
    }});
    add(Action{"SEQ.NOTE_UP", "NOTE+", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.adjustStepNote(c.app.cursor_row, c.app.cursor_step, +1);
        }
    }});
    add(Action{"SEQ.NOTE_DOWN", "NOTE-", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.adjustStepNote(c.app.cursor_row, c.app.cursor_step, -1);
        }
    }});
    add(Action{"SEQ.OCT_UP", "OCT+", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.adjustStepNote(c.app.cursor_row, c.app.cursor_step, +12);
        }
    }});
    add(Action{"SEQ.OCT_DOWN", "OCT-", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.adjustStepNote(c.app.cursor_row, c.app.cursor_step, -12);
        }
    }});
    add(Action{"SEQ.CYCLE_LEN", "LEN", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            const uint32_t next = c.audio.cycleRowLength(c.app.cursor_row);
            if (c.app.cursor_step >= static_cast<int>(next)) c.app.cursor_step = static_cast<int>(next - 1);
            c.app.seq_page = c.app.cursor_step / static_cast<int>(kStepsPerPage);
        }
    }});
    add(Action{"SEQ.LEN_INC", "LEN+", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            uint32_t len = c.audio.getRowLength(c.app.cursor_row);
            uint32_t next = std::min(128u, ((len / 16u) + 1u) * 16u);
            c.audio.setRowLength(c.app.cursor_row, next);
            if (c.app.cursor_step >= static_cast<int>(next)) c.app.cursor_step = static_cast<int>(next - 1);
            c.app.seq_page = c.app.cursor_step / static_cast<int>(kStepsPerPage);
        }
    }});
    add(Action{"SEQ.LEN_DEC", "LEN-", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            uint32_t len = c.audio.getRowLength(c.app.cursor_row);
            uint32_t next = len > 16u ? ((len - 1u) / 16u) * 16u : 16u;
            c.audio.setRowLength(c.app.cursor_row, next);
            if (c.app.cursor_step >= static_cast<int>(next)) c.app.cursor_step = static_cast<int>(next - 1);
            c.app.seq_page = c.app.cursor_step / static_cast<int>(kStepsPerPage);
        }
    }});
    add(Action{"SEQ.CYCLE_PAGE", "PAGE", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            const uint32_t len = c.audio.getRowLength(c.app.cursor_row);
            const uint32_t max_p = (len + kStepsPerPage - 1) / kStepsPerPage;
            if (max_p > 1) {
                c.app.seq_page = (c.app.seq_page + 1) % max_p;
                c.app.cursor_step = c.app.seq_page * static_cast<int>(kStepsPerPage) + (c.app.cursor_step % static_cast<int>(kStepsPerPage));
                if (c.app.cursor_step >= static_cast<int>(len)) c.app.cursor_step = static_cast<int>(len - 1);
            }
        }
    }});
    add(Action{"SEQ.ADD_TRACK", "ADD", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (act < kTotalRows) {
            c.audio.setActiveRowCount(act + kVoicesPerTrack);
            c.app.cursor_row = act;
            c.app.selected_track = act / kVoicesPerTrack;
        }
    }});
    add(Action{"SEQ.DEL_TRACK", "DEL", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (act > kVoicesPerTrack) {
            c.audio.setActiveRowCount(act - kVoicesPerTrack);
            if (c.app.cursor_row >= act - kVoicesPerTrack) {
                c.app.cursor_row = act - kVoicesPerTrack - 1;
            }
            if (c.app.selected_track >= (act - kVoicesPerTrack) / kVoicesPerTrack) {
                c.app.selected_track = std::max(0, (act - kVoicesPerTrack) / kVoicesPerTrack - 1);
            }
        }
    }});

    // ---------------- Транспорт, прев'ю, темп, гучність ----------------
    add(Action{"PLAY.TOGGLE", "PLAY", [](UiContext& c) {
        c.app.transport_playing = !c.app.transport_playing;
        if (c.app.transport_playing) {
            c.app.current_step = 0;
            c.app.last_step_ms = c.now_ms;
            c.audio.triggerSequencerStep(0);
        } else {
            c.audio.panic();
        }
    }});
    add(Action{"STEP.PREVIEW", "PREV", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.previewStep(c.app.cursor_row, c.app.cursor_step);
        }
    }});
    add(Action{"TRACK.PREVIEW_NOTE", "PREV", [](UiContext& c) {
        c.audio.triggerNote(c.app.selected_track, 48, 0.90f);
    }});
    add(Action{"TRACK.PREV", "TRK-", [](UiContext& c) {
        const int n = std::max(1, (c.audio.getActiveRowCount() + kVoicesPerTrack - 1) / kVoicesPerTrack);
        c.app.selected_track = (c.app.selected_track + n - 1) % n;
    }});
    add(Action{"TRACK.NEXT", "TRK+", [](UiContext& c) {
        const int n = std::max(1, (c.audio.getActiveRowCount() + kVoicesPerTrack - 1) / kVoicesPerTrack);
        c.app.selected_track = (c.app.selected_track + 1) % n;
    }});
    add(Action{"BPM.INC", "BPM+", [](UiContext& c) {
        c.app.bpm = std::min(200.0f, c.app.bpm + 1.0f);
    }});
    add(Action{"BPM.DEC", "BPM-", [](UiContext& c) {
        c.app.bpm = std::max(60.0f, c.app.bpm - 1.0f);
    }});
    add(Action{"VOL.INC", "VOL+", [](UiContext& c) {
        c.audio.adjustMasterVolume(+0.05f);
    }});
    add(Action{"VOL.DEC", "VOL-", [](UiContext& c) {
        c.audio.adjustMasterVolume(-0.05f);
    }});

    // ---------------- Швидкі MUTE (L2-оверлей у секвенсорі) ----------------
    auto toggleMuteTrk = [](UiContext& c, int t) {
        auto tp = c.audio.getTrackParams(t);
        tp.muted = !tp.muted;
        c.audio.setTrackParams(t, tp);
    };
    add(Action{"MUTE.T1", "M.T1", [=](UiContext& c) { toggleMuteTrk(c, 0); }});
    add(Action{"MUTE.T2", "M.T2", [=](UiContext& c) { toggleMuteTrk(c, 1); }});
    add(Action{"MUTE.T3", "M.T3", [=](UiContext& c) { toggleMuteTrk(c, 2); }});
    add(Action{"MUTE.T4", "M.T4", [=](UiContext& c) { toggleMuteTrk(c, 3); }});
    add(Action{"MUTE.CUR", "MUTE", [=](UiContext& c) { toggleMuteTrk(c, c.app.selected_track); }});

    // ---------------- Перемикання вкладок ----------------
    add(Action{"TAB.L1", "TAB.L1", [](UiContext& c) {
        c.app.active_view = ViewId::kSeq;
    }});
    add(Action{"TAB.L2", "TAB.L2", [](UiContext& c) {
        c.app.active_view = ViewId::kSynth;
        const int act = c.audio.getActiveRowCount();
        if (c.app.cursor_row < act) {
            c.app.selected_track = c.audio.getRowTrack(c.app.cursor_row);
        }
    }});
    add(Action{"TAB.R2", "TAB.R2", [](UiContext& c) {
        if (c.app.active_view == ViewId::kMix) {
            c.app.active_view = ViewId::kFx;
        } else {
            c.app.active_view = ViewId::kMix;
        }
    }});
    add(Action{"TAB.R1", "TAB.R1", [](UiContext& c) {
        c.app.active_view = ViewId::kPerf;
    }});
    add(Action{"TAB.G", "TAB.G", [](UiContext& c) {
        if (c.app.active_view != ViewId::kSys) {
            c.app.previous_view = c.app.active_view;
            c.app.active_view = ViewId::kSys;
        } else {
            c.app.active_view = c.app.previous_view;
        }
    }});
    add(Action{"VIEW.BACK", "BACK", [](UiContext& c) {
        if (c.app.step_inspector_open) {
            c.app.step_inspector_open = false;
        } else if (c.app.active_view == ViewId::kSys) {
            c.app.active_view = c.app.previous_view;
        }
    }});

    // ---------------- Модальні вікна (INSPECT) ----------------
    add(Action{"INSPECT.OPEN", "INSPECT", [](UiContext& c) {
        if (c.app.active_view == ViewId::kSeq && c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.app.step_inspector_open = true;
        }
    }});
    add(Action{"INSPECT.CLOSE", "CLOSE", [](UiContext& c) {
        c.app.step_inspector_open = false;
    }});

    // ---------------- РЕЖИМ ПЕРФОРМАНСУ (PERF.*) ----------------
    add(Action{"PERF.OCT_UP", "OCT+", [](UiContext& c) {
        c.app.perf_octave = std::min(6, c.app.perf_octave + 1);
    }});
    add(Action{"PERF.OCT_DOWN", "OCT-", [](UiContext& c) {
        c.app.perf_octave = std::max(1, c.app.perf_octave - 1);
    }});

    auto triggerPad = [](UiContext& c, int pad_idx) {
        int t = c.app.selected_track;
        auto tp = c.audio.getTrackParams(t);
        if (tp.engine == EngineType::kDrumSyn) {
            static constexpr uint8_t kDrumNotes[4] = {36, 38, 42, 46};
            int v = std::clamp(pad_idx, 0, 3);
            c.audio.triggerVoice(t * 4 + v, kDrumNotes[v], 0.92f);
        } else {
            // Квантування ноти за поточною тонікою та ладом арпеджиратора
            int base_root = c.app.perf_octave * 12 + (c.app.arpeggiator.config().root_note % 12);
            auto chord = getChordIntervals(ChordType::kSeventh, c.app.arpeggiator.config().scale);
            int offset = (pad_idx < static_cast<int>(chord.size())) ? chord[pad_idx] : (pad_idx * 2);
            int note = quantizeToScale(base_root + offset, base_root, c.app.arpeggiator.config().scale);
            c.audio.triggerNote(t, note, 0.90f);
        }
    };
    add(Action{"PERF.PAD_A", "PAD A", [=](UiContext& c) { triggerPad(c, 0); }});
    add(Action{"PERF.PAD_B", "PAD B", [=](UiContext& c) { triggerPad(c, 1); }});
    add(Action{"PERF.PAD_X", "PAD X", [=](UiContext& c) { triggerPad(c, 2); }});
    add(Action{"PERF.PAD_Y", "PAD Y", [=](UiContext& c) { triggerPad(c, 3); }});

    // ---------------- АРПЕДЖИРАТОР (ARP.*) ----------------
    add(Action{"ARP.OPEN", "ARP", [](UiContext& c) {
        c.app.previous_view = c.app.active_view;
        c.app.active_view = ViewId::kArp;
    }});
    add(Action{"ARP.COMMIT", "COMMIT", [](UiContext& c) {
        c.app.arpeggiator.commitToTrack(c.audio, c.app.cursor_row, 0, 16);
        c.app.last_action = "ARP COMMITTED";
    }});
}

} // namespace groovebox
