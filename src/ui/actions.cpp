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
    ctx.app.last_action = a->label;
    a->execute(ctx);
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
        if (c.app.mod_picker_open) {
            ActionRegistry::instance().run("MOD_PICKER.PREV", c);
            return;
        }
        if (c.app.project_modal_open) {
            ActionRegistry::instance().run("PROJECT.MODAL_UP", c);
            return;
        }
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
        if (c.app.mod_picker_open) {
            ActionRegistry::instance().run("MOD_PICKER.NEXT", c);
            return;
        }
        if (c.app.project_modal_open) {
            ActionRegistry::instance().run("PROJECT.MODAL_DOWN", c);
            return;
        }
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
        if (c.app.mod_picker_open || c.app.project_modal_open || c.app.sample_browser_open || c.app.engine_menu_open) return;
        if (c.shell) c.shell->currentView(c).navigate(c, -1, 0, false);
    }});
    add(Action{"NAV.RIGHT", "RIGHT", [](UiContext& c) {
        if (c.app.mod_picker_open || c.app.project_modal_open || c.app.sample_browser_open || c.app.engine_menu_open) return;
        if (c.shell) c.shell->currentView(c).navigate(c, 1, 0, false);
    }});
    add(Action{"NAV.PAGE_UP", "PG.UP", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, 0, -1, true);
    }});
    add(Action{"NAV.PAGE_DOWN", "PG.DN", [](UiContext& c) {
        if (c.shell) c.shell->currentView(c).navigate(c, 0, 1, true);
    }});
    add(Action{"NAV.BAR_LEFT", "BAR.L", [](UiContext& c) {
        if (c.app.seq_submode == SeqSubmode::kSong) {
            c.app.song_col = (c.app.song_col - 1 + 5) % 5;
            return;
        }
        if (c.shell) c.shell->currentView(c).navigate(c, -1, 0, true);
    }});
    add(Action{"NAV.BAR_RIGHT", "BAR.R", [](UiContext& c) {
        if (c.app.seq_submode == SeqSubmode::kSong) {
            c.app.song_col = (c.app.song_col + 1) % 5;
            return;
        }
        if (c.shell) c.shell->currentView(c).navigate(c, 1, 0, true);
    }});

    // ---------------- Редагування параметра у фокусі ----------------
    auto isModTargetParam = [](const std::string& id) -> bool {
        if (id.size() >= 5 && id.substr(id.size() - 5) == ".DEST") return true;
        if (id.rfind("STK.", 0) == 0 && (id.find("_X") != std::string::npos || id.find("_Y") != std::string::npos) && id.find("AMT") == std::string::npos) return true;
        if (id.rfind("STICK.", 0) == 0) return true;
        return false;
    };

    add(Action{"PARAM.INC", "VAL+", [=](UiContext& c) {
        if (c.app.mod_picker_open) {
            ActionRegistry::instance().run("MOD_PICKER.CLOSE", c);
            return;
        }
        if (c.app.project_modal_open) {
            ActionRegistry::instance().run("PROJECT.MODAL_CANCEL", c);
            return;
        }
        if (c.app.sample_browser_open) {
            ActionRegistry::instance().run("SAMPLE_BROWSER.CONFIRM", c);
            return;
        }
        if (c.app.engine_menu_open) {
            ActionRegistry::instance().run("ENGINE.MENU_CONFIRM", c);
            return;
        }
        if (c.shell) {
            std::string fid = c.shell->currentView(c).focusId(c);
            if (isModTargetParam(fid)) {
                ActionRegistry::instance().run("MOD_PICKER.OPEN", c);
                return;
            }
            const Param* p = c.shell->currentView(c).focusedParam(c);
            if (p && p->step) p->step(c, 1, false);
        }
    }});
    add(Action{"PARAM.DEC", "VAL-", [=](UiContext& c) {
        if (c.app.mod_picker_open) {
            ActionRegistry::instance().run("MOD_PICKER.CONFIRM", c);
            return;
        }
        if (c.app.project_modal_open) {
            ActionRegistry::instance().run("PROJECT.MODAL_CONFIRM", c);
            return;
        }
        if (c.app.sample_browser_open) {
            ActionRegistry::instance().run("SAMPLE_BROWSER.CLOSE", c);
            return;
        }
        if (c.app.engine_menu_open) {
            ActionRegistry::instance().run("ENGINE.MENU_CLOSE", c);
            return;
        }
        if (c.shell) {
            std::string fid = c.shell->currentView(c).focusId(c);
            if (isModTargetParam(fid)) {
                ActionRegistry::instance().run("MOD_PICKER.OPEN", c);
                return;
            }
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

            if ((c.app.rec_scroll_pitch / 12 != c.app.rec_octave) && (c.app.rec_scroll_pitch % 12 == 0)) {
                c.app.rec_scroll_pitch = c.app.rec_octave * 12;
            }
            int pitch = c.app.rec_scroll_pitch + (11 - std::clamp(c.app.rec_cursor_lane, 0, 11));

            // Перевіряємо, чи ця нота вже присутня у кроці
            if (st.enabled) {
                int found_slot = -1;
                for (int i = 0; i < 4; ++i) {
                    if (st.chord_notes[i] == pitch) {
                        found_slot = i;
                        break;
                    }
                }
                if (found_slot == -1 && st.note == pitch) {
                    found_slot = 0;
                }

                if (found_slot >= 0) {
                    // Нота вже є — видаляємо її
                    st.chord_notes[found_slot] = 255;
                    std::vector<uint8_t> remaining;
                    for (int i = 0; i < 4; ++i) {
                        if (st.chord_notes[i] != 255) remaining.push_back(st.chord_notes[i]);
                    }
                    if (remaining.empty()) {
                        if (st.has_plock || st.is_param_trig) {
                            st.enabled = true;
                            st.is_param_trig = true;
                            st.note = 255;
                            st.chord_notes = {255, 255, 255, 255};
                        } else {
                            st.enabled = false;
                            st.chord_notes = {48, 255, 255, 255};
                        }
                    } else {
                        st.note = remaining[0];
                        for (size_t i = 0; i < 4; ++i) {
                            st.chord_notes[i] = (i < remaining.size()) ? remaining[i] : 255;
                        }
                    }
                    c.audio.setStep(t, step, st);
                    c.app.last_action = "NOTE REMOVED";
                    return;
                }

                // Рахуємо кількість уже зайнятих голосів
                int active_voices = 0;
                for (int i = 0; i < 4; ++i) {
                    if (st.chord_notes[i] != 255) active_voices++;
                }
                if (active_voices == 0 && st.note != 255) active_voices = 1;

                if (active_voices >= 4) {
                    // Ліміт голосів (максимум 4) вичерпано!
                    c.app.last_action = "VOICE LIMIT: MAX 4";
                    c.app.voice_limit_alert_until_ms = c.now_ms + 1500;
                    return;
                }

                // Додаємо ноту у вільний слот
                if (st.note == 255 && st.chord_notes[0] == 255) {
                    st.note = static_cast<uint8_t>(pitch);
                    st.chord_notes[0] = static_cast<uint8_t>(pitch);
                } else {
                    for (int i = 0; i < 4; ++i) {
                        if (st.chord_notes[i] == 255) {
                            st.chord_notes[i] = static_cast<uint8_t>(pitch);
                            break;
                        }
                    }
                }
                c.audio.triggerNote(t, pitch, 0.90f);
                c.audio.setStep(t, step, st);
                c.app.last_action = "NOTE ADDED";
                return;
            } else {
                st.enabled = true;
                st.is_param_trig = false;
                st.note = static_cast<uint8_t>(pitch);
                st.chord_notes = {static_cast<uint8_t>(pitch), 255, 255, 255};
                c.audio.triggerNote(t, pitch, 0.90f);
                c.audio.setStep(t, step, st);
                c.app.last_action = "NOTE PLACED";
                return;
            }
        }

        if (c.app.seq_submode == SeqSubmode::kSong) {
            if (c.app.song_cursor_row >= c.app.song_rows_count && c.app.song_rows_count < 16) {
                int new_r = c.app.song_rows_count;
                auto& prev = c.app.song_chain[std::max(0, new_r - 1)];
                c.app.song_chain[new_r].enabled = true;
                c.app.song_chain[new_r].bank = prev.bank;
                c.app.song_chain[new_r].pattern = (prev.pattern + 1) % 16;
                c.app.song_chain[new_r].repeats = 1;
                c.app.song_chain[new_r].length = 0;
                c.app.song_chain[new_r].bpm = 0;
                c.app.song_chain[new_r].mute_mask = 0;
                c.app.song_rows_count++;
                c.app.last_action = "SONG: ROW ADDED";
                return;
            }
            if (c.app.song_cursor_row < c.app.song_rows_count) {
                auto& cur_s = c.app.song_chain[c.app.song_cursor_row];
                if (c.app.song_col == 4) {
                    cur_s.mute_mask ^= (1 << c.app.song_mute_cursor_track);
                    bool is_m = (cur_s.mute_mask & (1 << c.app.song_mute_cursor_track)) != 0;
                    c.app.last_action = is_m ? "TRK MUTED IN ROW" : "TRK ACTIVE IN ROW";
                } else if (c.app.song_col == 0) {
                    int pat_total = (cur_s.bank * 16 + cur_s.pattern + 1) % 128;
                    cur_s.bank = static_cast<uint8_t>(pat_total / 16);
                    cur_s.pattern = static_cast<uint8_t>(pat_total % 16);
                    c.app.pattern_mgr.switchPatternNow(cur_s.bank, cur_s.pattern, c.audio, c.app.bpm);
                    c.app.last_action = "PAT: " + PatternManager::formatPatternCode(cur_s.bank, cur_s.pattern);
                } else if (c.app.song_col == 1) {
                    cur_s.repeats = (cur_s.repeats % 16) + 1;
                    c.app.last_action = "REPEATS: x" + std::to_string(cur_s.repeats);
                } else if (c.app.song_col == 2) {
                    cur_s.length = (cur_s.length == 0) ? 16 : ((cur_s.length >= 128) ? 0 : cur_s.length * 2);
                    c.app.last_action = (cur_s.length == 0) ? "LEN: PAT DEFAULT" : ("LEN: " + std::to_string(cur_s.length));
                } else if (c.app.song_col == 3) {
                    cur_s.bpm = (cur_s.bpm == 0) ? static_cast<uint16_t>(c.app.bpm) : 0;
                    c.app.last_action = (cur_s.bpm == 0) ? "BPM: PAT DEFAULT" : ("BPM: " + std::to_string(cur_s.bpm));
                } else if (c.app.song_col == 5) {
                    cur_s.enabled = !cur_s.enabled;
                    c.app.last_action = cur_s.enabled ? "STATUS: PLAY" : "STATUS: SKIP";
                }
            }
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
        if (c.app.seq_submode == SeqSubmode::kMatrix) {
            c.app.seq_submode = SeqSubmode::kPianoRoll;
            c.app.rec_mode = true;
            c.app.selected_track = c.audio.getRowTrack(c.app.cursor_row);

            // Знаходимо найнижчу ноту серед усіх активних кроків треку
            int min_pitch = 128;
            int trk_len = static_cast<int>(c.audio.getRowLength(c.app.selected_track));
            for (int s = 0; s < trk_len; ++s) {
                auto st = c.audio.getStep(c.app.selected_track, s);
                if (st.enabled) {
                    if (st.note > 0 && st.note < min_pitch) min_pitch = st.note;
                    for (int ch = 0; ch < 4; ++ch) {
                        if (st.chord_notes[ch] > 0 && st.chord_notes[ch] < min_pitch) {
                            min_pitch = st.chord_notes[ch];
                        }
                    }
                }
            }
            if (min_pitch < 128) {
                c.app.rec_scroll_pitch = std::clamp(min_pitch - 2, 0, 127 - 11);
                c.app.rec_cursor_lane = 11 - (min_pitch - c.app.rec_scroll_pitch);
                c.app.rec_octave = std::clamp(min_pitch / 12, 1, 8);
            } else {
                auto st = c.audio.getStep(c.app.selected_track, c.app.cursor_step);
                if (st.enabled && st.note != 255 && st.note > 0) {
                    c.app.rec_scroll_pitch = std::clamp(static_cast<int>(st.note) - 2, 0, 127 - 11);
                    c.app.rec_cursor_lane = 11 - (st.note - c.app.rec_scroll_pitch);
                    c.app.rec_octave = std::clamp(static_cast<int>(st.note / 12), 1, 8);
                } else {
                    c.app.rec_scroll_pitch = 48;
                    c.app.rec_cursor_lane = 11;
                    c.app.rec_octave = 3;
                }
            }
            c.app.last_seq_submode = SeqSubmode::kPianoRoll;
            c.app.last_action = "PIANO ROLL";
        } else if (c.app.seq_submode == SeqSubmode::kPianoRoll) {
            c.app.seq_submode = SeqSubmode::kSong;
            c.app.rec_mode = false;
            c.app.last_seq_submode = SeqSubmode::kSong;
            c.app.last_action = "SONG MODE";
        } else {
            c.app.seq_submode = SeqSubmode::kMatrix;
            c.app.rec_mode = false;
            c.app.last_seq_submode = SeqSubmode::kMatrix;
            c.app.last_action = "MATRIX SEQ";
        }
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
        if (c.app.seq_submode == SeqSubmode::kSong) {
            if (c.app.song_rows_count > 1 && c.app.song_cursor_row < c.app.song_rows_count) {
                for (int i = c.app.song_cursor_row; i < c.app.song_rows_count - 1; ++i) {
                    c.app.song_chain[i] = c.app.song_chain[i + 1];
                }
                c.app.song_rows_count--;
                if (c.app.song_cursor_row >= c.app.song_rows_count) {
                    c.app.song_cursor_row = c.app.song_rows_count - 1;
                }
                c.app.last_action = "SONG: ROW DELETED";
                return;
            }
        }
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
        if (c.app.seq_submode == SeqSubmode::kSong) {
            if (c.app.song_cursor_row < c.app.song_rows_count) {
                auto& cur_s = c.app.song_chain[c.app.song_cursor_row];
                if (c.app.song_col == 0) {
                    int pat_total = cur_s.bank * 16 + cur_s.pattern;
                    pat_total = (pat_total - 1 + 128) % 128;
                    cur_s.bank = static_cast<uint8_t>(pat_total / 16);
                    cur_s.pattern = static_cast<uint8_t>(pat_total % 16);
                    c.app.pattern_mgr.switchPatternNow(cur_s.bank, cur_s.pattern, c.audio, c.app.bpm);
                    c.app.last_action = "PAT: " + PatternManager::formatPatternCode(cur_s.bank, cur_s.pattern);
                } else if (c.app.song_col == 1) {
                    cur_s.repeats = (cur_s.repeats <= 1) ? 16 : (cur_s.repeats - 1);
                    c.app.last_action = "REPEAT DEC";
                } else if (c.app.song_col == 2) {
                    cur_s.length = (cur_s.length <= 4) ? 0 : (cur_s.length - 4);
                    c.app.last_action = (cur_s.length == 0) ? "LEN: PAT DEFAULT" : ("LEN: " + std::to_string(cur_s.length));
                } else if (c.app.song_col == 3) {
                    if (cur_s.bpm > 30) cur_s.bpm--;
                    else cur_s.bpm = 0;
                    c.app.last_action = (cur_s.bpm == 0) ? "BPM: PAT DEFAULT" : ("BPM: " + std::to_string(cur_s.bpm));
                } else if (c.app.song_col == 4) {
                    cur_s.mute_mask ^= (1 << c.app.song_mute_cursor_track);
                    bool is_m = (cur_s.mute_mask & (1 << c.app.song_mute_cursor_track)) != 0;
                    c.app.last_action = is_m ? "TRK MUTED IN ROW" : "TRK ACTIVE IN ROW";
                } else if (c.app.song_col == 5) {
                    cur_s.enabled = !cur_s.enabled;
                    c.app.last_action = cur_s.enabled ? "STATUS: PLAY" : "STATUS: SKIP";
                }
            }
            return;
        }
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
            c.app.rec_scroll_pitch = std::min(127 - 11, c.app.rec_scroll_pitch + 12);
            c.app.rec_octave = std::clamp((c.app.rec_scroll_pitch + (11 - c.app.rec_cursor_lane)) / 12, 1, 8);
            c.app.last_action = "REC OCTAVE " + std::to_string(c.app.rec_octave);
        } else if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            c.audio.adjustStepNote(c.app.cursor_row, c.app.cursor_step, +1);
        }
    }});
    add(Action{"SEQ.NOTE_DOWN", "OCT-/NOTE-", [](UiContext& c) {
        if (c.app.rec_mode) {
            c.app.rec_scroll_pitch = std::max(0, c.app.rec_scroll_pitch - 12);
            c.app.rec_octave = std::clamp((c.app.rec_scroll_pitch + (11 - c.app.rec_cursor_lane)) / 12, 1, 8);
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
            uint32_t next = (len >= 256u) ? 4u : std::min(256u, ((len / 4u) + 1u) * 4u);
            c.audio.setRowLength(c.app.cursor_row, next);
            if (c.app.cursor_step >= static_cast<int>(next)) c.app.cursor_step = static_cast<int>(next - 1);
        }
    }});
    add(Action{"SEQ.LEN_DEC", "LEN-", [](UiContext& c) {
        if (c.app.cursor_row < c.audio.getActiveRowCount()) {
            uint32_t len = c.audio.getRowLength(c.app.cursor_row);
            uint32_t next = (len <= 4u) ? 256u : (((len - 1u) / 4u) * 4u);
            c.audio.setRowLength(c.app.cursor_row, next);
            if (c.app.cursor_step >= static_cast<int>(next)) c.app.cursor_step = static_cast<int>(next - 1);
        }
    }});
    add(Action{"SEQ.CLEAR_TRACK", "CLEAR TRK", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (c.app.cursor_row >= 0 && c.app.cursor_row < act) {
            const uint32_t len = c.audio.getRowLength(c.app.cursor_row);
            for (uint32_t s = 0; s < len; ++s) {
                c.audio.clearStep(c.app.cursor_row, s);
            }
            c.app.last_action = "TRACK CLEARED";
        }
    }});
    add(Action{"STEP.COPY", "COPY TRIG", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (c.app.cursor_row >= 0 && c.app.cursor_row < act) {
            c.app.clipboard_step = c.audio.getStep(c.app.cursor_row, c.app.cursor_step);
            c.app.has_clipboard_step = true;
            c.app.last_action = "TRIG COPIED";
        }
    }});
    add(Action{"STEP.PASTE", "PASTE TRIG", [](UiContext& c) {
        const int act = c.audio.getActiveRowCount();
        if (c.app.has_clipboard_step && c.app.cursor_row >= 0 && c.app.cursor_row < act) {
            c.audio.setStep(c.app.cursor_row, c.app.cursor_step, c.app.clipboard_step);
            c.app.last_action = "TRIG PASTED";
        }
    }});
    add(Action{"PROJECT.TEMP_SAVE", "TEMP SAVE", [](UiContext& c) {
        c.app.temp_snapshot = PatternData{};
        c.app.temp_snapshot.bpm = c.app.bpm;
        c.app.temp_snapshot.pattern_length = c.audio.getPatternLength();
        for (int t = 0; t < kTracksCount; ++t) {
            c.app.temp_snapshot.tracks[t] = c.audio.getTrackParams(t);
        }
        const int act = c.audio.getActiveRowCount();
        for (int r = 0; r < act; ++r) {
            c.app.temp_snapshot.row_lengths[r] = c.audio.getRowLength(r);
            for (uint32_t s = 0; s < kMaxSteps; ++s) {
                c.app.temp_snapshot.steps[r][s] = c.audio.getStep(r, s);
            }
        }
        c.app.temp_snapshot.initialized = true;
        c.app.has_temp_snapshot = true;
        c.app.last_action = "TEMP SAVED [FUNC+YES]";
    }});
    add(Action{"PROJECT.TEMP_RELOAD", "TEMP RECALL", [](UiContext& c) {
        if (!c.app.has_temp_snapshot || !c.app.temp_snapshot.initialized) {
            c.app.last_action = "NO TEMP SNAPSHOT";
            return;
        }
        c.app.bpm = c.app.temp_snapshot.bpm;
        c.audio.setPatternLength(c.app.temp_snapshot.pattern_length);
        for (int t = 0; t < kTracksCount; ++t) {
            c.audio.setTrackParams(t, c.app.temp_snapshot.tracks[t]);
        }
        const int act = c.audio.getActiveRowCount();
        for (int r = 0; r < act; ++r) {
            c.audio.setRowLength(r, c.app.temp_snapshot.row_lengths[r]);
            for (uint32_t s = 0; s < kMaxSteps; ++s) {
                c.audio.setStep(r, s, c.app.temp_snapshot.steps[r][s]);
            }
        }
        c.app.last_action = "TEMP RECALL [FUNC+NO]";
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
            if (c.app.seq_submode == SeqSubmode::kSong) {
                const int total_rows = std::clamp(c.app.song_rows_count, 1, 16);
                int start_step = std::clamp(c.app.song_cursor_row, 0, total_rows - 1);
                c.app.song_play_step = start_step;
                c.app.song_play_repeat = 0;
                auto& s = c.app.song_chain[start_step];
                float play_bpm = (s.bpm > 0) ? static_cast<float>(s.bpm) : c.app.bpm;
                c.app.pattern_mgr.switchPatternNow(s.bank, s.pattern, c.audio, play_bpm);
                for (int t = 0; t < 16; ++t) {
                    auto tp = c.audio.getTrackParams(t);
                    tp.muted = ((s.mute_mask & (1 << t)) != 0);
                    c.audio.setTrackParams(t, tp);
                }
            }
            c.audio.triggerSequencerStep(0);
        } else {
            c.audio.panic();
        }
    }});
    add(Action{"REC.MASTER_TOGGLE", "REC", [](UiContext& c) {
        if (c.audio.isRecording()) {
            c.audio.stopRecording();
            c.app.last_action = "REC STOPPED";
        } else {
            bool ok = c.audio.startRecording();
            c.app.last_action = ok ? "REC STARTED" : "ERR: REC FAIL";
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
    add(Action{"SOLO.CUR", "SOLO", [](UiContext& c) {
        c.audio.toggleSoloTrack(c.app.selected_track);
        bool s = c.audio.isSoloed(c.app.selected_track);
        c.app.last_action = s ? "SOLO: ON" : "SOLO: OFF";
    }});

    // Швидке перемикання патернів (SELECT + Up / Down: A01..A16 -> B01..B16 -> ...)
    add(Action{"PATTERN.NEXT", "PTN+", [](UiContext& c) {
        int idx = c.app.pattern_mgr.currentBank() * 16 + c.app.pattern_mgr.currentPattern();
        idx = (idx + 1) % 128;
        int b = idx / 16;
        int p = idx % 16;
        c.app.pattern_mgr.switchPatternNow(b, p, c.audio, c.app.bpm);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "PATTERN: %c%02d", 'A' + b, p + 1);
        c.app.last_action = buf;
    }});

    add(Action{"PATTERN.PREV", "PTN-", [](UiContext& c) {
        int idx = c.app.pattern_mgr.currentBank() * 16 + c.app.pattern_mgr.currentPattern();
        idx = (idx - 1 + 128) % 128;
        int b = idx / 16;
        int p = idx % 16;
        c.app.pattern_mgr.switchPatternNow(b, p, c.audio, c.app.bpm);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "PATTERN: %c%02d", 'A' + b, p + 1);
        c.app.last_action = buf;
    }});

    // ---------------- Перемикання вкладок ----------------
    add(Action{"TAB.L1", "TAB.L1", [](UiContext& c) {
        closeAllModals(c.app);
        if (c.app.active_view != ViewId::kSeq) {
            c.app.active_view = ViewId::kSeq;
            c.app.seq_submode = c.app.last_seq_submode;
            c.app.rec_mode = (c.app.seq_submode == SeqSubmode::kPianoRoll);
            c.app.last_action = "VIEW: SEQ";
        } else {
            ActionRegistry::instance().run("SEQ.REC_TOGGLE", c);
        }
    }});
    add(Action{"TAB.L2", "TAB.L2", [](UiContext& c) {
        closeAllModals(c.app);
        const int act = c.audio.getActiveRowCount();
        if (c.app.cursor_row < act) {
            c.app.selected_track = c.audio.getRowTrack(c.app.cursor_row);
        }
        if (c.app.active_view != ViewId::kSynth) {
            c.app.active_view = ViewId::kSynth;
            c.app.synth_submode = c.app.last_synth_submode;
            c.app.last_action = "VIEW: SYNTH";
        } else {
            // Цикл L2: SYN -> MOD -> FX -> SYN
            if (c.app.synth_submode == SynthSubmode::kSynth) {
                c.app.synth_submode = SynthSubmode::kMod;
                c.app.last_synth_submode = SynthSubmode::kMod;
                c.app.last_action = "MODE: MOD";
            } else if (c.app.synth_submode == SynthSubmode::kMod) {
                c.app.synth_submode = SynthSubmode::kFx;
                c.app.last_synth_submode = SynthSubmode::kFx;
                c.app.last_action = "MODE: FX";
            } else {
                c.app.synth_submode = SynthSubmode::kSynth;
                c.app.last_synth_submode = SynthSubmode::kSynth;
                c.app.last_action = "MODE: SYNTH";
            }
        }
    }});
    add(Action{"TAB.R2", "MIX", [](UiContext& c) {
        closeAllModals(c.app);
        if (c.app.active_view != ViewId::kMix && c.app.active_view != ViewId::kFx) {
            c.app.active_view = (c.app.last_mix_submode == MixSubmode::kSend) ? ViewId::kFx : ViewId::kMix;
            c.app.mix_submode = c.app.last_mix_submode;
            c.app.last_action = "VIEW: MIX";
        } else {
            // Цикл R2: MIX -> SND -> MSTR -> MIX
            if (c.app.mix_submode == MixSubmode::kMix) {
                c.app.mix_submode = MixSubmode::kSend;
                c.app.last_mix_submode = MixSubmode::kSend;
                c.app.active_view = ViewId::kFx;
                c.app.last_action = "MODE: SEND";
            } else if (c.app.mix_submode == MixSubmode::kSend) {
                c.app.mix_submode = MixSubmode::kMaster;
                c.app.last_mix_submode = MixSubmode::kMaster;
                c.app.active_view = ViewId::kMix;
                c.app.last_action = "MODE: MASTER";
            } else {
                c.app.mix_submode = MixSubmode::kMix;
                c.app.last_mix_submode = MixSubmode::kMix;
                c.app.active_view = ViewId::kMix;
                c.app.last_action = "MODE: MIX";
            }
        }
    }});
    add(Action{"TAB.R1", "SAMPLE", [](UiContext& c) {
        closeAllModals(c.app);
        c.app.active_view = ViewId::kSample;
        c.app.last_action = "VIEW: SAMPLE";
    }});
    add(Action{"TAB.G", "SYS", [](UiContext& c) {
        closeAllModals(c.app);
        if (c.app.active_view != ViewId::kSys) {
            c.app.previous_view = c.app.active_view;
            c.app.active_view = ViewId::kSys;
        } else {
            c.app.active_view = c.app.previous_view;
        }
    }});

    // Швидкі комбінації SELECT + Shoulder (альтернативні екрани)
    add(Action{"TAB.SEL_L1", "INSPECT", [](UiContext& c) {
        if (c.app.active_view != ViewId::kSeq) {
            c.app.last_action = "INSPECT: SEQ ONLY";
            return;
        }
        c.app.step_inspector_open = !c.app.step_inspector_open;
        c.app.last_action = c.app.step_inspector_open ? "INSPECT OPEN" : "INSPECT CLOSED";
    }});
    add(Action{"TAB.SEL_L2", "ARP", [](UiContext& c) {
        if (c.app.active_view != ViewId::kSeq) {
            c.app.last_action = "ARP: SEQ ONLY";
            return;
        }
        c.app.arp_modal_open = !c.app.arp_modal_open;
        c.app.last_action = c.app.arp_modal_open ? "ARP OPENED" : "ARP CLOSED";
    }});
    add(Action{"TAB.SEL_R2", "SAMPLE", [](UiContext& c) {
        closeAllModals(c.app);
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
        closeAllModals(c.app);
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
        if (c.app.mod_picker_open || c.app.project_modal_open || c.app.sample_browser_open ||
            c.app.engine_menu_open || c.app.step_inspector_open || c.app.arp_modal_open) {
            closeAllModals(c.app);
            c.app.last_action = "MODAL CLOSED";
            return;
        }
        if (c.app.active_view == ViewId::kSys || c.app.active_view == ViewId::kSample || c.app.active_view == ViewId::kPerf) {
            c.app.active_view = c.app.previous_view;
        }
    }});

    // ---------------- Модальні вікна (INSPECT) ----------------
    add(Action{"INSPECT.OPEN", "INSPECT", [](UiContext& c) {
        if (c.app.active_view == ViewId::kSeq) {
            if (c.app.seq_submode == SeqSubmode::kSong) {
                auto& cur_s = c.app.song_chain[c.app.song_cursor_row];
                if (c.app.song_col == 0) {
                    int pat_total = cur_s.bank * 16 + cur_s.pattern;
                    pat_total = (pat_total + 1) % 128;
                    cur_s.bank = static_cast<uint8_t>(pat_total / 16);
                    cur_s.pattern = static_cast<uint8_t>(pat_total % 16);
                    c.app.pattern_mgr.switchPatternNow(cur_s.bank, cur_s.pattern, c.audio, c.app.bpm);
                    c.app.last_action = "PAT INC";
                } else if (c.app.song_col == 1) {
                    cur_s.repeats = (cur_s.repeats >= 16) ? 1 : (cur_s.repeats + 1);
                    c.app.last_action = "REPEAT INC";
                } else if (c.app.song_col == 2) {
                    cur_s.length = (cur_s.length >= 256) ? 0 : (cur_s.length + 4);
                    c.app.last_action = (cur_s.length == 0) ? "LEN: PAT DEFAULT" : ("LEN: " + std::to_string(cur_s.length));
                } else if (c.app.song_col == 3) {
                    cur_s.bpm = (cur_s.bpm == 0) ? static_cast<uint16_t>(c.app.bpm) : std::min(300, cur_s.bpm + 1);
                    c.app.last_action = "BPM: " + std::to_string(cur_s.bpm);
                } else if (c.app.song_col == 4) {
                    cur_s.mute_mask ^= (1 << c.app.song_mute_cursor_track);
                    bool is_m = (cur_s.mute_mask & (1 << c.app.song_mute_cursor_track)) != 0;
                    c.app.last_action = is_m ? "TRK MUTED IN ROW" : "TRK ACTIVE IN ROW";
                } else if (c.app.song_col == 5) {
                    cur_s.enabled = !cur_s.enabled;
                    c.app.last_action = cur_s.enabled ? "STATUS: PLAY" : "STATUS: SKIP";
                }
                return;
            }
            if (c.app.cursor_row < c.audio.getActiveRowCount()) {
                c.app.step_inspector_open = true;
                c.app.inspect_page = AppModel::InspectPage::kMidi;
                c.app.grid_row[static_cast<int>(ViewId::kInspect)] = 0;
                c.app.grid_scroll[static_cast<int>(ViewId::kInspect)] = 0;
            }
        }
    }});
    add(Action{"INSPECT.CYCLE", "CYCLE", [](UiContext& c) {
        if (!c.app.step_inspector_open) {
            c.app.step_inspector_open = true;
            c.app.inspect_page = AppModel::InspectPage::kMidi;
        } else {
            int p = (static_cast<int>(c.app.inspect_page) + 1) % static_cast<int>(AppModel::InspectPage::kCount);
            c.app.inspect_page = static_cast<AppModel::InspectPage>(p);
        }
        c.app.grid_row[static_cast<int>(ViewId::kInspect)] = 0;
        c.app.grid_scroll[static_cast<int>(ViewId::kInspect)] = 0;
    }});
    add(Action{"INSPECT.CLOSE", "CLOSE", [](UiContext& c) {
        c.app.step_inspector_open = false;
    }});

    // Універсальний рандомайзер SELECT + R3
    add(Action{"UNIVERSAL.RANDOMIZE", "RND", [](UiContext& c) {
        if (c.app.active_view == ViewId::kSeq) {
            if (c.app.rec_mode || c.app.seq_submode == SeqSubmode::kPianoRoll) {
                // Piano Roll: розумно розкидаємо ноти в межах октави виділеної ноти
                const int t = c.app.selected_track;
                const int len = static_cast<int>(c.audio.getRowLength(t));
                const int base_pitch = c.app.rec_octave * 12;
                for (int s = 0; s < len; ++s) {
                    auto st = c.audio.getStep(t, s);
                    if (st.enabled) {
                        st.note = static_cast<uint8_t>(base_pitch + (std::rand() % 12));
                        c.audio.setStep(t, s, st);
                    }
                }
                c.app.last_action = "PIANO: OCT RANDOM";
            } else {
                c.audio.randomizeRow(c.app.cursor_row);
                c.app.last_action = "TRACK TRIGS RANDOM";
            }
            return;
        }
        if (c.app.active_view == ViewId::kSynth) {
            int t = c.app.selected_track;
            auto tp = c.audio.getTrackParams(t);
            if (c.app.synth_submode == SynthSubmode::kSynth) {
                // Рандомізація чисельних параметрів поточного екрану синтезатора
                for (int i = 0; i < kParamsPerTrack; ++i) {
                    tp.p[i] = static_cast<float>(std::rand() % 1000) / 1000.0f;
                }
                tp.lpf_cutoff = 0.20f + static_cast<float>(std::rand() % 750) / 1000.0f;
                tp.lpf_resonance = static_cast<float>(std::rand() % 800) / 1000.0f;
                c.audio.setTrackParams(t, tp);
                c.app.last_action = "SYNTH: SCREEN RANDOM";
            } else if (c.app.synth_submode == SynthSubmode::kMod) {
                tp.lfo1.depth = static_cast<float>(std::rand() % 1000) / 1000.0f;
                tp.lfo1.rate = static_cast<float>(std::rand() % 1000) / 1000.0f;
                tp.lfo2.depth = static_cast<float>(std::rand() % 1000) / 1000.0f;
                tp.lfo2.rate = static_cast<float>(std::rand() % 1000) / 1000.0f;
                tp.env2.attack = 0.01f + static_cast<float>(std::rand() % 500) / 1000.0f;
                tp.env2.decay = 0.05f + static_cast<float>(std::rand() % 1000) / 1000.0f;
                c.audio.setTrackParams(t, tp);
                c.app.last_action = "MOD: SCREEN RANDOM";
            } else if (c.app.synth_submode == SynthSubmode::kFx) {
                tp.fx.dist_drive = static_cast<float>(std::rand() % 1000) / 1000.0f;
                tp.fx.dist_mix = static_cast<float>(std::rand() % 800) / 1000.0f;
                tp.fx.chorus_mix = static_cast<float>(std::rand() % 800) / 1000.0f;
                c.audio.setTrackParams(t, tp);
                c.app.last_action = "FX: SCREEN RANDOM";
            }
            return;
        }
        if (c.app.active_view == ViewId::kFx) {
            auto tp = c.audio.getTrackParams(c.app.selected_track);
            tp.delay_send = static_cast<float>(std::rand() % 800) / 1000.0f;
            tp.reverb_send = static_cast<float>(std::rand() % 800) / 1000.0f;
            tp.chorus_send = static_cast<float>(std::rand() % 800) / 1000.0f;
            c.audio.setTrackParams(c.app.selected_track, tp);
            c.app.last_action = "SENDS: RANDOM";
            return;
        }
        if (c.app.active_view == ViewId::kArp) {
            ActionRegistry::instance().run("ARP.RANDOMIZE", c);
            return;
        }
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
        if (c.app.active_view != ViewId::kSeq) {
            c.app.last_action = "ARP: SEQ ONLY";
            return;
        }
        c.app.arp_modal_open = true;
        c.app.last_action = "ARP OPENED";
    }});
    add(Action{"ARP.CLOSE", "CLOSE", [](UiContext& c) {
        c.app.arp_modal_open = false;
        c.app.last_action = "ARP CLOSED";
    }});
    add(Action{"ARP.TOGGLE", "ARP", [](UiContext& c) {
        if (c.app.active_view != ViewId::kSeq) {
            c.app.last_action = "ARP: SEQ ONLY";
            return;
        }
        c.app.arp_modal_open = !c.app.arp_modal_open;
        c.app.last_action = c.app.arp_modal_open ? "ARP OPENED" : "ARP CLOSED";
    }});
    add(Action{"ENGINE.MENU_OPEN", "ENGINES", [](UiContext& c) {
        if (c.app.active_view != ViewId::kSeq && c.app.active_view != ViewId::kSynth) {
            c.app.last_action = "ENGINE: SEQ/SYNTH ONLY";
            return;
        }
        c.app.engine_menu_open = true;
        auto cur_eng = c.audio.getTrackParams(c.app.selected_track).engine;
        int found_sel = 0;
        for (size_t i = 0; i < kSelectableEngines.size(); ++i) {
            if (kSelectableEngines[i] == cur_eng) {
                found_sel = static_cast<int>(i);
                break;
            }
        }
        c.app.engine_menu_sel = found_sel;
        c.app.last_action = "ENGINE SELECTOR";
    }});
    add(Action{"ENGINE.MENU_UP", "UP", [](UiContext& c) {
        const int n = static_cast<int>(kSelectableEngines.size());
        c.app.engine_menu_sel = (c.app.engine_menu_sel - 1 + n) % n;
    }});
    add(Action{"ENGINE.MENU_DOWN", "DOWN", [](UiContext& c) {
        const int n = static_cast<int>(kSelectableEngines.size());
        c.app.engine_menu_sel = (c.app.engine_menu_sel + 1) % n;
    }});
    add(Action{"ENGINE.MENU_CLOSE", "CLOSE", [](UiContext& c) {
        c.app.engine_menu_open = false;
        c.app.modal_cooldown_until_ms = c.now_ms + 300;
        c.app.last_action = "ENGINE CLOSED";
    }});
    add(Action{"ENGINE.MENU_CONFIRM", "APPLY", [](UiContext& c) {
        const int n = static_cast<int>(kSelectableEngines.size());
        auto eng = kSelectableEngines[std::clamp(c.app.engine_menu_sel, 0, n - 1)];
        if (c.app.engine_menu_is_add) {
            c.app.engine_menu_is_add = false;
            c.app.engine_menu_open = false;
            c.app.modal_cooldown_until_ms = c.now_ms + 300;
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
        c.app.modal_cooldown_until_ms = c.now_ms + 300;
        c.app.last_action = std::string("ENGINE: ") + getEngineTypeName(tp.engine);
        if (eng == EngineType::kSampleOneShot || eng == EngineType::kSliceSampler) {
            ActionRegistry::instance().run("SAMPLE_BROWSER.OPEN", c);
        }
    }});

    // ---------------- Модальне контекстне меню вибору таргету модуляції (MOD TARGET PICKER) ----------------
    auto isValidTargetForParam = [](const std::string& param_id, ModTarget tgt) -> bool {
        if (param_id == "ENV1.DEST") {
            if (tgt == ModTarget::kEnv1Attack || tgt == ModTarget::kEnv1Decay ||
                tgt == ModTarget::kEnv1Sustain || tgt == ModTarget::kEnv1Release) return false;
        } else if (param_id == "ENV2.DEST") {
            if (tgt == ModTarget::kEnv2Attack || tgt == ModTarget::kEnv2Decay ||
                tgt == ModTarget::kEnv2Sustain || tgt == ModTarget::kEnv2Release) return false;
        }
        return true;
    };

    add(Action{"MOD_PICKER.OPEN", "OPEN TGT", [=](UiContext& c) {
        if (!c.shell) return;
        std::string focus_id = c.shell->currentView(c).focusId(c);
        c.app.mod_picker_param_id = focus_id;
        auto tp = c.audio.getTrackParams(c.app.selected_track);
        ModTarget current = ModTarget::kNone;
        if (focus_id == "LFO1.DEST") current = tp.lfo1.dest;
        else if (focus_id == "LFO2.DEST") current = tp.lfo2.dest;
        else if (focus_id == "LFO3.DEST") current = tp.lfo3.dest;
        else if (focus_id == "LFO4.DEST") current = tp.lfo4.dest;
        else if (focus_id == "ENV1.DEST") current = tp.env1.dest;
        else if (focus_id == "ENV2.DEST") current = tp.env2.dest;
        else if (focus_id == "STK.L1_X" || focus_id == "STICK.LX") current = tp.stick_l_m1.dest_x;
        else if (focus_id == "STK.L1_Y" || focus_id == "STICK.LY") current = tp.stick_l_m1.dest_y;
        else if (focus_id == "STK.L2_X") current = tp.stick_l_m2.dest_x;
        else if (focus_id == "STK.L2_Y") current = tp.stick_l_m2.dest_y;
        else if (focus_id == "STK.R1_X" || focus_id == "STICK.RX") current = tp.stick_r_m1.dest_x;
        else if (focus_id == "STK.R1_Y" || focus_id == "STICK.RY") current = tp.stick_r_m1.dest_y;
        else if (focus_id == "STK.R2_X") current = tp.stick_r_m2.dest_x;
        else if (focus_id == "STK.R2_Y") current = tp.stick_r_m2.dest_y;
        c.app.mod_picker_sel = static_cast<int>(current);
        c.app.mod_picker_open = true;
        c.app.last_action = "TARGET MENU OPEN";
    }});
    add(Action{"MOD_PICKER.PREV", "UP", [=](UiContext& c) {
        const int count = static_cast<int>(ModTarget::kCount);
        int sel = c.app.mod_picker_sel;
        for (int i = 0; i < count; ++i) {
            sel = (sel - 1 + count) % count;
            if (isValidTargetForParam(c.app.mod_picker_param_id, static_cast<ModTarget>(sel))) {
                c.app.mod_picker_sel = sel;
                break;
            }
        }
    }});
    add(Action{"MOD_PICKER.NEXT", "DOWN", [=](UiContext& c) {
        const int count = static_cast<int>(ModTarget::kCount);
        int sel = c.app.mod_picker_sel;
        for (int i = 0; i < count; ++i) {
            sel = (sel + 1) % count;
            if (isValidTargetForParam(c.app.mod_picker_param_id, static_cast<ModTarget>(sel))) {
                c.app.mod_picker_sel = sel;
                break;
            }
        }
    }});
    add(Action{"MOD_PICKER.CLOSE", "CANCEL", [](UiContext& c) {
        c.app.mod_picker_open = false;
        c.app.modal_cooldown_until_ms = c.now_ms + 250;
        c.app.last_action = "PICKER CLOSED";
    }});
    add(Action{"MOD_PICKER.CONFIRM", "SELECT", [](UiContext& c) {
        auto tgt = static_cast<ModTarget>(std::clamp(c.app.mod_picker_sel, 0, static_cast<int>(ModTarget::kCount) - 1));
        int t = c.app.selected_track;
        auto tp = c.audio.getTrackParams(t);
        const std::string& id = c.app.mod_picker_param_id;
        if (id == "LFO1.DEST") tp.lfo1.dest = tgt;
        else if (id == "LFO2.DEST") tp.lfo2.dest = tgt;
        else if (id == "LFO3.DEST") tp.lfo3.dest = tgt;
        else if (id == "LFO4.DEST") tp.lfo4.dest = tgt;
        else if (id == "ENV1.DEST") tp.env1.dest = tgt;
        else if (id == "ENV2.DEST") tp.env2.dest = tgt;
        else if (id == "STK.L1_X" || id == "STICK.LX") { tp.stick_l_m1.dest_x = tgt; tp.stick_left_x = tgt; }
        else if (id == "STK.L1_Y" || id == "STICK.LY") { tp.stick_l_m1.dest_y = tgt; tp.stick_left_y = tgt; }
        else if (id == "STK.L2_X") tp.stick_l_m2.dest_x = tgt;
        else if (id == "STK.L2_Y") tp.stick_l_m2.dest_y = tgt;
        else if (id == "STK.R1_X" || id == "STICK.RX") { tp.stick_r_m1.dest_x = tgt; tp.stick_right_x = tgt; }
        else if (id == "STK.R1_Y" || id == "STICK.RY") { tp.stick_r_m1.dest_y = tgt; tp.stick_right_y = tgt; }
        else if (id == "STK.R2_X") tp.stick_r_m2.dest_x = tgt;
        else if (id == "STK.R2_Y") tp.stick_r_m2.dest_y = tgt;
        c.audio.setTrackParams(t, tp);
        c.app.mod_picker_open = false;
        c.app.modal_cooldown_until_ms = c.now_ms + 250;
        c.app.last_action = "TARGET: " + std::string(getModTargetShortName(tgt));
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
            c.app.modal_cooldown_until_ms = c.now_ms + 300;
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
            c.app.modal_cooldown_until_ms = c.now_ms + 300;
            c.app.last_action = "BROWSER CLOSED";
        }
    }});
    add(Action{"SAMPLE_BROWSER.CLOSE", "CLOSE", [=](UiContext& c) {
        c.app.sample_browser_open = false;
        c.app.modal_cooldown_until_ms = c.now_ms + 300;
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

    add(Action{"PROJECT.SAVE", "SAVE PROJ", [](UiContext& c) {
        std::string path = c.app.pattern_mgr.currentProjectPath();
        if (path.empty()) path = PatternManager::defaultProjectPath();
        bool ok = c.app.pattern_mgr.saveProject(path, c.audio, c.app.bpm, &c.app.song_chain, c.app.song_rows_count);
        c.app.last_action = ok ? "PROJECT SAVED" : "ERR: SAVE FAILED";
    }});
    add(Action{"PROJECT.NEW", "NEW PROJ", [](UiContext& c) {
        c.app.pattern_mgr = PatternManager();
        c.app.song_rows_count = 1;
        c.app.song_cursor_row = 0;
        c.app.song_chain = []() {
            std::array<SongStep, 16> sc{};
            sc[0].bank = 0;
            sc[0].pattern = 0;
            sc[0].repeats = 1;
            sc[0].enabled = true;
            for (int i = 1; i < 16; ++i) {
                sc[i].bank = 0;
                sc[i].pattern = i;
                sc[i].repeats = 1;
                sc[i].enabled = false;
            }
            return sc;
        }();
        c.audio.setMasterFx(MasterFxParameters{});
        c.app.bpm = 120.0f;
        c.app.pattern_mgr.applyCurrent(c.audio, c.app.bpm);
        c.app.last_action = "NEW PROJECT CREATED";
    }});
    add(Action{"PROJECT.SAVE_AS_OPEN", "SAVE AS", [](UiContext& c) {
        if (c.app.active_view != ViewId::kSys) {
            c.app.last_action = "PROJ: SYS ONLY";
            return;
        }
        c.app.project_modal_open = true;
        c.app.project_modal_is_save = true;
        c.app.project_files = PatternManager::listProjectFiles();
        c.app.project_files.insert(c.app.project_files.begin(), "[+] NEW PROJECT");
        c.app.project_modal_sel = 0;
        c.app.last_action = "SAVE AS MODAL";
    }});
    add(Action{"PROJECT.LOAD_OPEN", "LOAD PROJ", [](UiContext& c) {
        if (c.app.active_view != ViewId::kSys) {
            c.app.last_action = "PROJ: SYS ONLY";
            return;
        }
        c.app.project_modal_open = true;
        c.app.project_modal_is_save = false;
        c.app.project_files = PatternManager::listProjectFiles();
        c.app.project_modal_sel = 0;
        c.app.last_action = "LOAD PROJECT MODAL";
    }});
    add(Action{"PROJECT.MODAL_CONFIRM", "CONFIRM", [](UiContext& c) {
        if (!c.app.project_modal_open) return;
        if (c.app.project_modal_is_save) {
            std::string fname;
            if (c.app.project_modal_sel == 0 || c.app.project_files.empty()) {
                int num = std::max(1, static_cast<int>(c.app.project_files.size()));
                char b[32]; std::snprintf(b, sizeof(b), "project_%02d.xgproj", num);
                fname = b;
            } else {
                fname = c.app.project_files[c.app.project_modal_sel];
            }
            std::string full_path = PatternManager::projectsDirectory() + "/" + fname;
            bool ok = c.app.pattern_mgr.saveProject(full_path, c.audio, c.app.bpm, &c.app.song_chain, c.app.song_rows_count);
            c.app.project_modal_open = false;
            c.app.modal_cooldown_until_ms = c.now_ms + 300;
            c.app.last_action = ok ? ("SAVED: " + fname) : "ERR: SAVE FAILED";
        } else {
            if (!c.app.project_files.empty() && c.app.project_modal_sel < static_cast<int>(c.app.project_files.size())) {
                std::string fname = c.app.project_files[c.app.project_modal_sel];
                std::string full_path = PatternManager::projectsDirectory() + "/" + fname;
                bool ok = c.app.pattern_mgr.loadProject(full_path, c.audio, c.app.bpm, &c.app.song_chain, &c.app.song_rows_count);
                c.app.project_modal_open = false;
                c.app.modal_cooldown_until_ms = c.now_ms + 300;
                c.app.last_action = ok ? ("LOADED: " + fname) : "ERR: LOAD FAILED";
            }
        }
    }});
    add(Action{"PROJECT.MODAL_CLOSE", "CANCEL", [](UiContext& c) {
        c.app.project_modal_open = false;
        c.app.modal_cooldown_until_ms = c.now_ms + 300;
        c.app.last_action = "MODAL CLOSED";
    }});
    add(Action{"PROJECT.MODAL_CANCEL", "CANCEL", [](UiContext& c) {
        c.app.project_modal_open = false;
        c.app.modal_cooldown_until_ms = c.now_ms + 300;
        c.app.last_action = "MODAL CLOSED";
    }});
    add(Action{"PROJECT.MODAL_UP", "UP", [](UiContext& c) {
        if (c.app.project_modal_sel > 0) c.app.project_modal_sel--;
    }});
    add(Action{"PROJECT.MODAL_DOWN", "DOWN", [](UiContext& c) {
        if (c.app.project_modal_sel + 1 < static_cast<int>(c.app.project_files.size())) {
            c.app.project_modal_sel++;
        }
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
