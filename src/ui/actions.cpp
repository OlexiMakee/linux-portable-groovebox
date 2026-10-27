#include "ui/actions.hpp"
#include "ui/params.hpp"
#include "ui/views.hpp"
#include "ui/ui_shell.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

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
        if (c.app.sample_browser_open) {
            ActionRegistry::instance().run("SAMPLE_BROWSER.UP", c);
            return;
        }
        if (c.app.engine_menu_open) {
            c.app.engine_menu_sel = (c.app.engine_menu_sel - 1 + static_cast<int>(EngineType::kCount)) % static_cast<int>(EngineType::kCount);
            return;
        }
        if (c.shell) c.shell->currentView(c).navigate(c, 0, -1, false);
    }});
    add(Action{"NAV.DOWN", "DOWN", [](UiContext& c) {
        if (c.app.sample_browser_open) {
            ActionRegistry::instance().run("SAMPLE_BROWSER.DOWN", c);
            return;
        }
        if (c.app.engine_menu_open) {
            c.app.engine_menu_sel = (c.app.engine_menu_sel + 1) % static_cast<int>(EngineType::kCount);
            return;
        }
        if (c.shell) c.shell->currentView(c).navigate(c, 0, 1, false);
    }});
    add(Action{"NAV.LEFT", "LEFT", [](UiContext& c) {
        if (c.app.sample_browser_open || c.app.engine_menu_open) return;
        if (c.shell) c.shell->currentView(c).navigate(c, -1, 0, false);
    }});
    add(Action{"NAV.RIGHT", "RIGHT", [](UiContext& c) {
        if (c.app.sample_browser_open || c.app.engine_menu_open) return;
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
        if (c.app.sample_browser_open) {
            ActionRegistry::instance().run("SAMPLE_BROWSER.CONFIRM", c);
            return;
        }
        if (c.app.engine_menu_open) {
            ActionRegistry::instance().run("ENGINE.MENU_CONFIRM", c);
            return;
        }
        if (c.shell) {
            const Param* p = c.shell->currentView(c).focusedParam(c);
            if (p && p->step) p->step(c, 1, false);
        }
    }});
    add(Action{"PARAM.DEC", "VAL-", [](UiContext& c) {
        if (c.app.sample_browser_open) {
            ActionRegistry::instance().run("SAMPLE_BROWSER.CLOSE", c);
            return;
        }
        if (c.app.engine_menu_open) {
            ActionRegistry::instance().run("ENGINE.MENU_CLOSE", c);
            return;
        }
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
        if (c.app.rec_mode) {
            int t = c.app.selected_track;
            int step = c.app.cursor_step;
            auto st = c.audio.getStep(t, step);

            // Уніфікований хроматичний Piano Roll для всіх движків (включаючи Sampler)
            int pitch = c.app.rec_octave * 12 + (11 - std::clamp(c.app.rec_cursor_lane, 0, 11));
            if (st.enabled && st.note == pitch) {
                st.enabled = false;
            } else {
                st.enabled = true;
                st.is_param_trig = false;
                st.note = static_cast<uint8_t>(pitch);
                c.audio.triggerNote(t, pitch, 0.90f);
            }
            c.audio.setStep(t, step, st);
            return;
        }

        const int act = c.audio.getActiveRowCount();
        if (c.app.cursor_row >= act) {
            if (act < kTotalRows) {
                c.app.engine_menu_open = true;
                c.app.engine_menu_is_add = true;
                c.app.engine_menu_sel = 0;
                c.app.last_action = "SELECT ENGINE";
            }
        } else {
            c.audio.toggleStep(c.app.cursor_row, c.app.cursor_step);
        }
    }});
    add(Action{"SEQ.REC_TOGGLE", "REC", [](UiContext& c) {
        c.app.rec_mode = !c.app.rec_mode;
        c.app.last_action = c.app.rec_mode ? "REC EXPANDED" : "SEQ OVERVIEW";
    }});
    add(Action{"SEQ.ADD_TRACK_SINGLE", "ADD TRK", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (act < kTotalRows) {
            c.app.engine_menu_open = true;
            c.app.engine_menu_is_add = true;
            c.app.engine_menu_sel = 0;
            c.app.last_action = "SELECT ENGINE";
        }
    }});
    add(Action{"SEQ.DEL_TRACK_SAFE", "DEL TRK", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (act > 1 && c.app.cursor_row < act) {
            c.audio.deleteRow(c.app.cursor_row);
            if (c.app.cursor_row >= c.audio.getActiveRowCount()) {
                c.app.cursor_row = c.audio.getActiveRowCount() - 1;
            }
            c.app.last_action = "TRACK DELETED";
        }
    }});
    add(Action{"SEQ.CLEAR_STEP", "DEL", [](UiContext& c) {
        if (c.app.rec_mode) {
            c.audio.clearStep(c.app.selected_track, c.app.cursor_step);
        } else if (c.app.cursor_row < c.audio.getActiveRowCount()) {
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
    add(Action{"SEQ.NOTE_UP", "OCT+/NOTE+", [](UiContext& c) {
        if (c.app.rec_mode) {
            c.app.rec_octave = std::min(6, c.app.rec_octave + 1);
            c.app.last_action = "REC OCTAVE " + std::to_string(c.app.rec_octave);
        } else if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.adjustStepNote(c.app.cursor_row, c.app.cursor_step, +1);
        }
    }});
    add(Action{"SEQ.NOTE_DOWN", "OCT-/NOTE-", [](UiContext& c) {
        if (c.app.rec_mode) {
            c.app.rec_octave = std::max(1, c.app.rec_octave - 1);
            c.app.last_action = "REC OCTAVE " + std::to_string(c.app.rec_octave);
        } else if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.adjustStepNote(c.app.cursor_row, c.app.cursor_step, -1);
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
        }
    }});
    add(Action{"SEQ.LEN_INC", "LEN+", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            uint32_t len = c.audio.getRowLength(c.app.cursor_row);
            uint32_t next = std::min(128u, ((len / 4u) + 1u) * 4u);
            c.audio.setRowLength(c.app.cursor_row, next);
            if (c.app.cursor_step >= static_cast<int>(next)) c.app.cursor_step = static_cast<int>(next - 1);
        }
    }});
    add(Action{"SEQ.LEN_DEC", "LEN-", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            uint32_t len = c.audio.getRowLength(c.app.cursor_row);
            uint32_t next = len > 4u ? ((len - 1u) / 4u) * 4u : 4u;
            c.audio.setRowLength(c.app.cursor_row, next);
            if (c.app.cursor_step >= static_cast<int>(next)) c.app.cursor_step = static_cast<int>(next - 1);
        }
    }});
    add(Action{"SEQ.CYCLE_PAGE", "ADD TRK", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (act < kTotalRows) {
            c.audio.setActiveRowCount(act + 1);
            c.app.cursor_row = act;
            c.app.selected_track = act;
            c.app.last_action = "TRACK ADDED";
        }
    }});
    add(Action{"SEQ.ADD_TRACK", "ADD", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (act < kTotalRows) {
            c.audio.setActiveRowCount(act + 1);
            c.app.cursor_row = act;
            c.app.selected_track = act;
            c.app.last_action = "TRACK ADDED";
        }
    }});
    add(Action{"SEQ.DEL_TRACK", "DEL", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (act > 1) {
            c.audio.setActiveRowCount(act - 1);
            if (c.app.cursor_row >= act - 1) {
                c.app.cursor_row = act - 2;
            }
            if (c.app.selected_track >= act - 1) {
                c.app.selected_track = std::max(0, act - 2);
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
        const int n = std::max(1, c.audio.getActiveRowCount());
        c.app.selected_track = (c.app.selected_track + n - 1) % n;
        c.app.last_action = "TRK " + std::to_string(c.app.selected_track + 1) + ": " + c.audio.getTrackName(c.app.selected_track);
    }});
    add(Action{"TRACK.NEXT", "TRK+", [](UiContext& c) {
        const int n = std::max(1, c.audio.getActiveRowCount());
        c.app.selected_track = (c.app.selected_track + 1) % n;
        c.app.last_action = "TRK " + std::to_string(c.app.selected_track + 1) + ": " + c.audio.getTrackName(c.app.selected_track);
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
    add(Action{"TAB.R2", "MIX/FX", [](UiContext& c) {
        if (c.app.active_view == ViewId::kMix) {
            c.app.active_view = ViewId::kFx;
            c.app.last_action = "VIEW: FX";
        } else {
            c.app.active_view = ViewId::kMix;
            c.app.last_action = "VIEW: MIX";
        }
    }});
    add(Action{"TAB.R1", "SAMPLE", [](UiContext& c) {
        c.app.active_view = ViewId::kSample;
        c.app.last_action = "VIEW: SAMPLE";
    }});
    add(Action{"TAB.G", "SYS", [](UiContext& c) {
        if (c.app.active_view != ViewId::kSys) {
            c.app.previous_view = c.app.active_view;
            c.app.active_view = ViewId::kSys;
        } else {
            c.app.active_view = c.app.previous_view;
        }
    }});

    // Швидкі комбінації SELECT + Shoulder (альтернативні екрани)
    add(Action{"TAB.SEL_L1", "INSPECT", [](UiContext& c) {
        c.app.step_inspector_open = !c.app.step_inspector_open;
        c.app.last_action = c.app.step_inspector_open ? "INSPECT OPEN" : "INSPECT CLOSED";
    }});
    add(Action{"TAB.SEL_L2", "ARP", [](UiContext& c) {
        c.app.arp_modal_open = !c.app.arp_modal_open;
        c.app.last_action = c.app.arp_modal_open ? "ARP OPENED" : "ARP CLOSED";
    }});
    add(Action{"TAB.SEL_R2", "SAMPLE", [](UiContext& c) {
        if (c.app.active_view == ViewId::kSample) {
            c.app.active_view = c.app.previous_view;
            c.app.last_action = "SAMPLE CLOSED";
        } else {
            c.app.previous_view = c.app.active_view;
            c.app.active_view = ViewId::kSample;
            c.app.last_action = "VIEW: SAMPLE";
        }
    }});
    add(Action{"TAB.SEL_R1", "PERF", [](UiContext& c) {
        if (c.app.active_view == ViewId::kPerf) {
            c.app.active_view = c.app.previous_view;
            c.app.last_action = "PERF CLOSED";
        } else {
            c.app.previous_view = c.app.active_view;
            c.app.active_view = ViewId::kPerf;
            c.app.last_action = "VIEW: PERF PADS";
        }
    }});

    add(Action{"VIEW.BACK", "BACK", [](UiContext& c) {
        if (c.app.sample_browser_open) {
            c.app.sample_browser_open = false;
            c.app.last_action = "BROWSER CLOSED";
        } else if (c.app.arp_modal_open) {
            c.app.arp_modal_open = false;
        } else if (c.app.engine_menu_open) {
            c.app.engine_menu_open = false;
        } else if (c.app.step_inspector_open) {
            c.app.step_inspector_open = false;
        } else if (c.app.active_view == ViewId::kSys || c.app.active_view == ViewId::kSample || c.app.active_view == ViewId::kPerf) {
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

    // ---------------- АРПЕДЖИРАТОР ТА РАНДОМАЙЗЕР (ARP.* / RND.* / FILL.*) ----------------
    add(Action{"FILL.ON", "FILL", [](UiContext& c) {
        c.audio.setFillMode(true);
        c.app.last_action = "ELEKTRON FILL ON";
    }});
    add(Action{"FILL.OFF", "NORM", [](UiContext& c) {
        c.audio.setFillMode(false);
        c.app.last_action = "FILL OFF";
    }});

    add(Action{"ARP.OPEN", "ARP", [](UiContext& c) {
        c.app.arp_modal_open = true;
        c.app.last_action = "ARP OPENED";
    }});
    add(Action{"ARP.CLOSE", "CLOSE", [](UiContext& c) {
        c.app.arp_modal_open = false;
        c.app.last_action = "ARP CLOSED";
    }});
    add(Action{"ARP.TOGGLE", "ARP", [](UiContext& c) {
        c.app.arp_modal_open = !c.app.arp_modal_open;
        c.app.last_action = c.app.arp_modal_open ? "ARP OPENED" : "ARP CLOSED";
    }});
    add(Action{"ENGINE.MENU_OPEN", "ENGINES", [](UiContext& c) {
        c.app.engine_menu_open = true;
        c.app.engine_menu_sel = static_cast<int>(c.audio.getTrackParams(c.app.selected_track).engine);
        c.app.last_action = "ENGINE SELECTOR";
    }});
    add(Action{"ENGINE.MENU_CLOSE", "CLOSE", [](UiContext& c) {
        c.app.engine_menu_open = false;
        c.app.last_action = "ENGINE CLOSED";
    }});
    add(Action{"ENGINE.MENU_CONFIRM", "APPLY", [](UiContext& c) {
        auto eng = static_cast<EngineType>(std::clamp(c.app.engine_menu_sel, 0, static_cast<int>(EngineType::kCount) - 1));
        if (c.app.engine_menu_is_add) {
            c.app.engine_menu_is_add = false;
            c.app.engine_menu_open = false;
            int new_idx = c.audio.addTrack(eng);
            if (new_idx >= 0) {
                c.app.cursor_row = new_idx;
                c.app.selected_track = new_idx;
                c.app.last_action = "ADDED: " + c.audio.getTrackName(new_idx);
                if (eng == EngineType::kSampleOneShot || eng == EngineType::kSliceSampler) {
                    ActionRegistry::instance().run("SAMPLE_BROWSER.OPEN", c);
                }
            }
            return;
        }
        auto tp = c.audio.getTrackParams(c.app.selected_track);
        tp.engine = eng;
        c.audio.setTrackParams(c.app.selected_track, tp);
        c.app.engine_menu_open = false;
        c.app.last_action = std::string("ENGINE: ") + getEngineTypeName(tp.engine);
        if (eng == EngineType::kSampleOneShot || eng == EngineType::kSliceSampler) {
            ActionRegistry::instance().run("SAMPLE_BROWSER.OPEN", c);
        }
    }});

    // ---------------- Семпл-браузер (пул семплів та доступ до ФС) ----------------
    auto getSamplesRootPath = []() -> std::string {
        static const char* kCandidates[] = {
            "/storage/roms/groovebox/samples",
            "/storage/dev/groovebox/res/samples",
            "res/samples",
            "../res/samples"
        };
        for (const char* p : kCandidates) {
            std::error_code ec;
            if (fs::exists(p, ec) && fs::is_directory(p, ec)) {
                return std::string(p);
            }
        }
        return "res/samples";
    };

    auto refreshSampleBrowser = [getSamplesRootPath](UiContext& c) {
        c.app.sample_browser_items.clear();
        std::string root = getSamplesRootPath();
        if (c.app.sample_browser_dir.empty()) {
            c.app.sample_browser_dir = root;
        }
        std::error_code ec;
        if (fs::equivalent(c.app.sample_browser_dir, root, ec) == false &&
            c.app.sample_browser_dir != root) {
            c.app.sample_browser_items.push_back("[..] PARENT DIR");
        }
        std::vector<std::string> dirs;
        std::vector<std::string> wavs;
        for (const auto& entry : fs::directory_iterator(c.app.sample_browser_dir, ec)) {
            if (entry.is_directory(ec)) {
                dirs.push_back("[DIR] " + entry.path().filename().string());
            } else if (entry.is_regular_file(ec)) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".wav") {
                    wavs.push_back(entry.path().filename().string());
                }
            }
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(wavs.begin(), wavs.end());
        for (auto& d : dirs) c.app.sample_browser_items.push_back(std::move(d));
        for (auto& w : wavs) c.app.sample_browser_items.push_back(std::move(w));

        if (c.app.sample_browser_items.empty()) {
            c.app.sample_browser_items.push_back("(EMPTY DIRECTORY)");
        }
        c.app.sample_browser_sel = std::clamp(c.app.sample_browser_sel, 0, static_cast<int>(c.app.sample_browser_items.size()) - 1);
    };

    auto auditionCurrentBrowserItem = [](UiContext& c) {
        if (c.app.sample_browser_items.empty()) return;
        int sel = std::clamp(c.app.sample_browser_sel, 0, static_cast<int>(c.app.sample_browser_items.size()) - 1);
        const std::string& item = c.app.sample_browser_items[sel];
        if (item.rfind("[DIR] ", 0) == 0 || item.rfind("[..]", 0) == 0 || item[0] == '(') {
            return;
        }
        std::string full_path = c.app.sample_browser_dir + "/" + item;
        int t = c.app.sample_browser_target_track;
        if (c.audio.sampler().loadWavFile(full_path, c.audio.sampler().trackSample(t))) {
            c.audio.triggerNote(t, 48, 0.90f);
        }
    };

    add(Action{"SAMPLE_BROWSER.OPEN", "SAMPLES", [=](UiContext& c) {
        c.app.sample_browser_open = true;
        c.app.sample_browser_target_track = c.app.selected_track;
        c.app.sample_browser_dir = getSamplesRootPath();
        c.app.sample_browser_sel = 0;
        refreshSampleBrowser(c);
        c.app.last_action = "SAMPLE BROWSER";
    }});
    add(Action{"SAMPLE_BROWSER.UP", "UP", [=](UiContext& c) {
        const int n = static_cast<int>(c.app.sample_browser_items.size());
        if (n > 0) {
            c.app.sample_browser_sel = (c.app.sample_browser_sel - 1 + n) % n;
            auditionCurrentBrowserItem(c);
        }
    }});
    add(Action{"SAMPLE_BROWSER.DOWN", "DOWN", [=](UiContext& c) {
        const int n = static_cast<int>(c.app.sample_browser_items.size());
        if (n > 0) {
            c.app.sample_browser_sel = (c.app.sample_browser_sel + 1) % n;
            auditionCurrentBrowserItem(c);
        }
    }});
    add(Action{"SAMPLE_BROWSER.CONFIRM", "ENTER", [=](UiContext& c) {
        if (c.app.sample_browser_items.empty()) return;
        int sel = std::clamp(c.app.sample_browser_sel, 0, static_cast<int>(c.app.sample_browser_items.size()) - 1);
        const std::string& item = c.app.sample_browser_items[sel];
        if (item.rfind("[..]", 0) == 0) {
            fs::path p(c.app.sample_browser_dir);
            c.app.sample_browser_dir = p.parent_path().string();
            c.app.sample_browser_sel = 0;
            refreshSampleBrowser(c);
            return;
        }
        if (item.rfind("[DIR] ", 0) == 0) {
            std::string sub = item.substr(6);
            c.app.sample_browser_dir = c.app.sample_browser_dir + "/" + sub;
            c.app.sample_browser_sel = 0;
            refreshSampleBrowser(c);
            return;
        }
        if (item[0] == '(') return;
        std::string full_path = c.app.sample_browser_dir + "/" + item;
        int t = c.app.sample_browser_target_track;
        if (c.audio.sampler().loadWavFile(full_path, c.audio.sampler().trackSample(t))) {
            std::string stem = fs::path(full_path).stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(), ::toupper);
            c.audio.setTrackName(t, stem);
            auto tp = c.audio.getTrackParams(t);
            tp.engine = EngineType::kSampleOneShot;
            c.audio.setTrackParams(t, tp);
            c.audio.triggerNote(t, 48, 0.90f);
            c.app.sample_browser_open = false;
            c.app.last_action = "LOADED: " + stem;
        } else {
            c.app.last_action = "ERR: LOAD FAILED";
        }
    }});
    add(Action{"SAMPLE_BROWSER.PARENT", "PARENT", [=](UiContext& c) {
        std::string root = getSamplesRootPath();
        std::error_code ec;
        if (!c.app.sample_browser_dir.empty() && !fs::equivalent(c.app.sample_browser_dir, root, ec) && c.app.sample_browser_dir != root) {
            fs::path p(c.app.sample_browser_dir);
            c.app.sample_browser_dir = p.parent_path().string();
            c.app.sample_browser_sel = 0;
            refreshSampleBrowser(c);
        } else {
            c.app.sample_browser_open = false;
            c.app.last_action = "BROWSER CLOSED";
        }
    }});
    add(Action{"SAMPLE_BROWSER.CLOSE", "CLOSE", [=](UiContext& c) {
        c.app.sample_browser_open = false;
        c.app.last_action = "BROWSER CLOSED";
    }});
    add(Action{"ARP.COMMIT", "APPLY", [](UiContext& c) {
        const int row = c.app.cursor_row;
        const int start = c.app.cursor_step;
        const int row_len = static_cast<int>(c.audio.getRowLength(row));
        int count = row_len - start;
        if (count <= 0) count = 16;
        c.app.arpeggiator.commitToTrack(c.audio, row, start, count);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "ARP APPLIED @%02d..%02d", start + 1, std::min(row_len, start + count));
        c.app.last_action = buf;
    }});

    // Музично-зважений рандомайзер синтезатора (Smart Musical Synth Randomizer)
    add(Action{"SYNTH.RANDOMIZE", "RND SNT", [](UiContext& c) {
        const int t = c.app.selected_track;
        auto tp = c.audio.getTrackParams(t);
        static uint32_t s_seed = 1234567;
        auto rndF = [&](float min_v, float max_v) {
            s_seed = s_seed * 1664525u + 1013904223u;
            float r = static_cast<float>((s_seed >> 8) & 0xFFFF) / 65535.0f;
            return min_v + r * (max_v - min_v);
        };
        tp.attack = rndF(0.002f, 0.08f);
        tp.decay = rndF(0.06f, 0.60f);
        tp.sustain = rndF(0.10f, 0.85f);
        tp.release = rndF(0.05f, 0.90f);
        tp.p[0] = rndF(0.20f, 0.90f); // Cutoff
        tp.p[1] = rndF(0.15f, 0.70f); // Resonance
        tp.p[2] = rndF(0.10f, 0.80f); // Env Mod / Wave
        tp.p[3] = rndF(0.00f, 0.60f); // Drive / Detune
        tp.p[4] = rndF(0.00f, 0.50f); // Sub / Noise
        tp.p[5] = rndF(0.20f, 0.80f); // Tone / FM Ratio
        tp.delay_send = rndF(0.00f, 0.35f);
        tp.reverb_send = rndF(0.05f, 0.40f);
        c.audio.setTrackParams(t, tp);
        c.app.last_action = "T" + std::to_string(t + 1) + " SYNTH MUTATED";
    }});

    // Музично-зважений рандомайзер арпеджиатора (Smart Musical Arp Randomizer)
    add(Action{"ARP.RANDOMIZE", "RND ARP", [](UiContext& c) {
        auto& cfg = c.app.arpeggiator.config();
        static uint32_t a_seed = 7654321;
        auto rndI = [&](int min_v, int max_v) {
            a_seed = a_seed * 1664525u + 1013904223u;
            int span = max_v - min_v + 1;
            return min_v + static_cast<int>(((a_seed >> 8) & 0x7FFF) % span);
        };
        cfg.scale = static_cast<ScaleType>(rndI(1, 9));
        cfg.arp1_div = static_cast<Arp1Division>(rndI(0, 3));
        cfg.chord_type = static_cast<ChordType>(rndI(0, 4));
        cfg.progression_id = rndI(0, 3);
        cfg.octaves = rndI(1, 3);
        cfg.arp2_div = static_cast<Arp2Division>(rndI(2, 5));
        cfg.pattern = static_cast<ArpPattern>(rndI(0, 8));
        cfg.arp2_gate = static_cast<float>(rndI(30, 90)) / 100.0f;
        cfg.euclid_hits = rndI(3, 11);
        cfg.mutation_pct = rndI(10, 50);
        c.app.last_action = "ARP RANDOMIZED";
    }});

    add(Action{"PARAM.DEFAULT", "INIT", [](UiContext& c) {
        c.app.last_action = "PARAM INIT";
    }});
    add(Action{"PARAM.RANDOMIZE", "RANDOM", [](UiContext& c) {
        if (c.app.active_view == ViewId::kArp) {
            ActionRegistry::instance().run("ARP.RANDOMIZE", c);
        } else if (c.app.active_view == ViewId::kSynth) {
            ActionRegistry::instance().run("SYNTH.RANDOMIZE", c);
        } else if (c.app.active_view == ViewId::kSeq) {
            ActionRegistry::instance().run("SEQ.RANDOMIZE", c);
        } else {
            c.app.last_action = "RANDOMIZE N/A";
        }
    }});
}

} // namespace groovebox
