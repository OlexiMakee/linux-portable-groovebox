#include "ui/views.hpp"

#include "ui/ui_shell.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace groovebox {

namespace {
constexpr int kHeaderH = 10;   // висота рядка заголовка робочої області
constexpr int kTableRowH = 22; // висота рядка таблиці параметрів
constexpr int kTitleW = 28;    // ширина колонки назв рядків
constexpr int kListRowH = 13;
constexpr int kPopupRowH = 10;

int visibleRows(const GridSpec& s, Rect area) {
    switch (s.style) {
        case GridStyle::kTable: return std::max(1, (area.h - kHeaderH) / kTableRowH);
        case GridStyle::kList:  return std::max(1, (area.h - kHeaderH - 2) / kListRowH);
        case GridStyle::kPopup: return std::max(1, (area.h - 24) / kPopupRowH);
    }
    return 1;
}

// Робоча область однакова для всіх вікон (задається UiShell)
Rect workspaceRect() { return Rect{0, UiShell::kTabBarH, 320, UiShell::kWorkspaceH}; }
} // namespace

// =====================================================================================
// GridView
// =====================================================================================
GridView::GridView(GridSpec spec) : IView(spec.id), spec_(std::move(spec)) {
    const auto& reg = ParamRegistry::instance();
    resolved_.reserve(spec_.rows.size());
    for (const auto& row : spec_.rows) {
        std::vector<const Param*> r;
        for (const auto& id : row.cells) {
            const Param* p = id.empty() ? nullptr : reg.find(id);
            if (!id.empty() && !p) std::fprintf(stderr, "[UI] unknown param id '%s' in view %s\n", id.c_str(), viewName(spec_.id));
            r.push_back(p);
        }
        resolved_.push_back(std::move(r));
    }
}

std::string GridView::tabTitle(const UiContext& c) const {
    return spec_.tab_title ? spec_.tab_title(c) : std::string(viewName(id()));
}

const Param* GridView::cellParam(int row, int col) const {
    if (row < 0 || row >= static_cast<int>(resolved_.size())) return nullptr;
    const auto& r = resolved_[row];
    if (col < 0 || col >= static_cast<int>(r.size())) return nullptr;
    return r[col];
}

int GridView::rowOf(const UiContext& c) const {
    const int n = static_cast<int>(resolved_.size());
    return n ? std::clamp(c.app.grid_row[static_cast<int>(id())], 0, n - 1) : 0;
}

int GridView::colOf(const UiContext& c) const {
    const int r = rowOf(c);
    const int n = resolved_.empty() ? 0 : static_cast<int>(resolved_[r].size());
    return n ? std::clamp(c.app.grid_col[static_cast<int>(id())], 0, n - 1) : 0;
}

const Param* GridView::focusedParam(const UiContext& c) const {
    return cellParam(rowOf(c), colOf(c));
}

std::string GridView::focusId(const UiContext& c) const {
    const Param* p = focusedParam(c);
    return p ? p->id : std::string(viewName(id()));
}

void GridView::navigate(UiContext& c, int dx, int dy, bool coarse) {
    const int rows = static_cast<int>(resolved_.size());
    if (rows == 0) return;
    const int vi = static_cast<int>(id());
    int row = rowOf(c);
    int col = colOf(c);

    if (dy != 0) {
        const int jump = coarse ? 3 : 1;
        row = (row + dy * jump % rows + rows) % rows;
        col = std::min(col, static_cast<int>(resolved_[row].size()) - 1);
    }
    if (dx != 0) {
        const int cols = static_cast<int>(resolved_[row].size());
        if (cols > 0) col = (col + dx + cols) % cols;
    }
    c.app.grid_row[vi] = row;
    c.app.grid_col[vi] = std::max(0, col);

    // Автоскрол, щоб курсор завжди був видимим
    const int vis = visibleRows(spec_, workspaceRect());
    int& scroll = c.app.grid_scroll[vi];
    if (row < scroll) scroll = row;
    if (row >= scroll + vis) scroll = row - vis + 1;
    scroll = std::clamp(scroll, 0, std::max(0, rows - vis));

    if (dy != 0 && spec_.on_focus) spec_.on_focus(c, row);
}

void GridView::render(Canvas& cv, const UiContext& c, Rect area) const {
    switch (spec_.style) {
        case GridStyle::kTable: renderTable(cv, c, area); break;
        case GridStyle::kList:  renderList(cv, c, area, false); break;
        case GridStyle::kPopup: renderList(cv, c, area, true); break;
    }
}

void GridView::renderTable(Canvas& cv, const UiContext& c, Rect area) const {
    const int vi = static_cast<int>(id());
    if (spec_.header) cv.text(area.x + 4, area.y + 3, spec_.header(c), spec_.accent);

    const int rows = static_cast<int>(resolved_.size());
    const int vis = visibleRows(spec_, area);
    const int scroll = std::clamp(c.app.grid_scroll[vi], 0, std::max(0, rows - vis));
    if (rows > vis) {
        char b[16];
        std::snprintf(b, sizeof(b), "%d-%d/%d", scroll + 1, std::min(rows, scroll + vis), rows);
        cv.text(area.x + area.w - Canvas::textWidth(b) - 4, area.y + 3, b, palette::kTextDim);
    }

    const int sel_r = rowOf(c);
    const int sel_c = colOf(c);
    const int cells_x = area.x + kTitleW;
    const int cells_w = area.w - kTitleW - 3;

    for (int i = 0; i < vis && scroll + i < rows; ++i) {
        const int r = scroll + i;
        const int y = area.y + kHeaderH + i * kTableRowH;
        cv.text(area.x + 4, y + 8, spec_.rows[r].title, r == sel_r ? spec_.accent : palette::kTextDim);

        const auto& cells = resolved_[r];
        const int n = std::max(1, static_cast<int>(cells.size()));
        const int cw = cells_w / n;
        for (int k = 0; k < static_cast<int>(cells.size()); ++k) {
            const Rect cell{cells_x + k * cw, y, cw - 2, kTableRowH - 2};
            const Param* p = cells[k];
            if (!p) {
                cv.rect(cell.x, cell.y, cell.w, cell.h, palette::kGridDim);
                continue;
            }
            const float norm = p->norm ? p->norm(c) : -1.0f;
            widgets::paramCell(cv, cell, p->labelFor(c), p->value(c), norm,
                               r == sel_r && k == sel_c, p->editable(), spec_.accent);
        }
    }

    if (spec_.id == ViewId::kMix) {
        // Компактний інтегрований осцилограф (26 px) та майстер VU-метри безпосередньо під мікшером
        const int mix_scope_y = area.y + kHeaderH + rows * kTableRowH + 4;
        cv.text(area.x + 4, mix_scope_y + 8, "SCOPE", palette::kTextDim);
        widgets::scope(cv, Rect{area.x + 30, mix_scope_y - 1, area.w - 34, 26}, c.tel.scope_samples, palette::kAccentCyan);

        const int vu_y = mix_scope_y + 28;
        cv.text(area.x + 4, vu_y + 1, "OUT L", palette::kTextDim);
        widgets::meter(cv, Rect{area.x + 28, vu_y, (area.w - 44) / 2, 7}, c.tel.vu_left);
        cv.text(area.x + 32 + (area.w - 44) / 2, vu_y + 1, "R", palette::kTextDim);
        widgets::meter(cv, Rect{area.x + 40 + (area.w - 44) / 2, vu_y, (area.w - 44) / 2 - 4, 7}, c.tel.vu_right);
    }
}

void GridView::renderList(Canvas& cv, const UiContext& c, Rect area, bool popup) const {
    const int vi = static_cast<int>(id());
    const int rows = static_cast<int>(resolved_.size());
    const int sel_r = rowOf(c);

    Rect box = area;
    int row_h = kListRowH;
    int value_x = 110;
    int y0 = area.y + kHeaderH + 2;
    if (popup) {
        row_h = kPopupRowH;
        value_x = 64;
        box.w = 180;
        box.h = std::min(area.h - 8, rows * row_h + 18);
        box.x = area.x + (area.w - box.w) / 2;
        box.y = area.y + 6;
        cv.fill(box.x, box.y, box.w, box.h, palette::kPanelBg);
        cv.rect(box.x, box.y, box.w, box.h, spec_.accent);
        cv.fill(box.x + 1, box.y + 1, box.w - 2, 10, palette::kHeaderSel);
        if (spec_.header) cv.text(box.x + 4, box.y + 3, spec_.header(c), spec_.accent);
        y0 = box.y + 14;
    } else if (spec_.header) {
        cv.text(area.x + 4, area.y + 3, spec_.header(c), spec_.accent);
    }

    const int vis = visibleRows(spec_, area);
    const int scroll = std::clamp(c.app.grid_scroll[vi], 0, std::max(0, rows - vis));
    for (int i = 0; i < vis && scroll + i < rows; ++i) {
        const int r = scroll + i;
        const Param* p = resolved_[r].empty() ? nullptr : resolved_[r][0];
        if (!p) continue;
        const Rect rr{box.x + 3, y0 + i * row_h, box.w - 6, row_h - 1};
        widgets::listRow(cv, rr, p->labelFor(c), p->value(c), r == sel_r, p->editable(), value_x, spec_.accent);
    }
}

// =====================================================================================
// SeqView
// =====================================================================================
namespace {

const char* voiceLabel(EngineType e, int voice) {
    static constexpr const char* kDrum[4] = {"BD", "SD", "CH", "OH"};
    static constexpr const char* kVoice[4] = {"V1", "V2", "V3", "V4"};
    return e == EngineType::kDrumSyn ? kDrum[std::clamp(voice, 0, 3)] : kVoice[std::clamp(voice, 0, 3)];
}

constexpr const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void clampStepToRow(UiContext& c) {
    const uint32_t len = c.audio.getRowLength(c.app.cursor_row);
    if (c.app.cursor_step >= static_cast<int>(len)) c.app.cursor_step = static_cast<int>(len) - 1;
    c.app.seq_page = c.app.cursor_step / static_cast<int>(kStepsPerPage);
}

} // namespace

void SeqView::navigate(UiContext& c, int dx, int dy, bool coarse) {
    const int active = c.audio.getActiveRowCount();
    const int slots = active < kTotalRows ? active + 1 : active;
    if (dy != 0) {
        const int jump = coarse ? kVoicesPerTrack : 1;
        c.app.cursor_row = ((c.app.cursor_row + dy * jump) % slots + slots) % slots;
        if (c.app.cursor_row < active) {
            c.app.selected_track = c.audio.getRowTrack(c.app.cursor_row);
            clampStepToRow(c);
        }
    }
    if (dx != 0 && c.app.cursor_row < active) {
        const int len = static_cast<int>(c.audio.getRowLength(c.app.cursor_row));
        const int jump = coarse ? 16 : 1;
        c.app.cursor_step = ((c.app.cursor_step + dx * jump) % len + len) % len;
        c.app.seq_page = c.app.cursor_step / static_cast<int>(kStepsPerPage);
    }
}

std::string SeqView::focusId(const UiContext& c) const {
    if (c.app.cursor_row >= c.audio.getActiveRowCount()) return "SEQ.ADD";
    char b[24];
    std::snprintf(b, sizeof(b), "SEQ.R%02d.S%03d", c.app.cursor_row + 1, c.app.cursor_step + 1);
    return b;
}

void SeqView::tick(UiContext& c) {
    // Elektron P-Lock: затиснута A + рух лівого стіка по X -> живий запис cutoff у крок
    const int active = c.audio.getActiveRowCount();
    if (c.in.btn_a && c.app.cursor_row < active && std::fabs(c.in.lx) > 0.15f) {
        c.app.stick_plock_tweaked = true;
        c.app.plock_cutoff_live = std::clamp(0.50f + c.in.lx * 0.45f, 0.05f, 0.95f);
        c.audio.lockStepCutoff(c.app.cursor_row, c.app.cursor_step, c.app.plock_cutoff_live);
        if (c.shell) c.shell->router().consume(Button::kA);
    }
    if (!c.in.btn_a) {
        c.app.stick_plock_tweaked = false;
        c.app.plock_cutoff_live = -1.0f;
    }
}

void SeqView::render(Canvas& cv, const UiContext& c, Rect area) const {
    const int active = c.audio.getActiveRowCount();
    const bool on_add = c.app.cursor_row >= active;
    const int cur_r = on_add ? active - 1 : c.app.cursor_row;
    const auto tp = c.audio.getTrackParams(c.audio.getRowTrack(cur_r));
    const auto st = c.audio.getStep(cur_r, c.app.cursor_step);
    const uint32_t cur_len = c.audio.getRowLength(cur_r);

    // Заголовок: лише контекст курсора (транспорт/BPM — у HUD, підказки — у HUD)
    char hdr[96];
    if (on_add) {
        std::snprintf(hdr, sizeof(hdr), "[+] NEW TRACK SLOT (%d/%d ROWS USED)", active, kTotalRows);
        cv.text(area.x + 4, area.y + 3, hdr, palette::kAccentCyan);
    } else {
        const char* trig = !st.enabled ? "---" : (st.is_param_trig ? "P-LOCK" : "NOTE");
        std::snprintf(hdr, sizeof(hdr), "R%02d %s | ST%03d %s%d %s%s | P%d/%u L%u",
                      cur_r + 1, getEngineTypeName(tp.engine), c.app.cursor_step + 1,
                      kNoteNames[st.note % 12], st.note / 12 - 1, trig,
                      (st.slide && tp.engine != EngineType::kDrumSyn) ? " SLD" : "",
                      c.app.seq_page + 1, (cur_len + kStepsPerPage - 1) / kStepsPerPage, cur_len);
        cv.text(area.x + 4, area.y + 3, hdr, st.is_param_trig ? palette::kParamTrig : palette::kAccentOrange);
    }

    const int grid_x = area.x + 28;
    const int step_w = 8;
    const int grid_y = area.y + kHeaderH + 2;
    const int slots = active < kTotalRows ? active + 1 : active;
    const int row_h = std::clamp((area.h - kHeaderH - 4) / std::max(1, slots), 8, 13);
    const uint32_t page_off = static_cast<uint32_t>(c.app.seq_page) * kStepsPerPage;
    const int len_x = grid_x + static_cast<int>(kStepsPerPage) * step_w + 4;

    for (int r = 0; r < active; ++r) {
        const int t = c.audio.getRowTrack(r);
        const int v = c.audio.getRowVoice(r);
        const auto trk = c.audio.getTrackParams(t);
        const int y = grid_y + r * row_h;
        const bool is_cur = !on_add && r == cur_r;
        const uint32_t len = c.audio.getRowLength(r);

        if (v == 0 && r > 0) cv.line(area.x + 2, y - 1, area.x + area.w - 3, y - 1, palette::kGridDim);
        if (is_cur) cv.fill(area.x + 2, y, 24, row_h - 1, palette::kHeaderSel);

        // Одна (не дубльована) мітка рядка: номер + голос
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "%02d%s", r + 1, voiceLabel(trk.engine, v));
        const ColorRGB lc = is_cur ? palette::kAccentOrange : (trk.muted ? palette::kGridDim : palette::kTextDim);
        cv.text(area.x + 4, y + (row_h - 5) / 2, lbl, lc);

        const uint32_t play = c.app.transport_playing ? (c.app.current_step % len) : UINT32_MAX;
        const int th = row_h - 3;
        for (uint32_t s = 0; s < kStepsPerPage; ++s) {
            const uint32_t idx = page_off + s;
            const int x = grid_x + static_cast<int>(s) * step_w;
            if (idx < len) {
                const auto sd = c.audio.getStep(r, static_cast<int>(idx));
                if (sd.enabled) {
                    const ColorRGB col = sd.is_param_trig ? palette::kParamTrig
                                       : (trk.muted ? palette::kGridDim
                                       : (t % 2 == 0 ? palette::kLevelOk : palette::kAccentCyan));
                    cv.fill(x, y + 1, step_w - 2, th, col);
                } else if (s % 4 == 0) {
                    cv.fill(x + 2, y + row_h / 2 - 1, 2, 2, s % 16 == 0 ? palette::kTextDim : palette::kGridDim);
                }
                if (idx == play) cv.line(x + 3, y, x + 3, y + row_h - 2, palette::kCursorWhite);
            } else if (idx == len) {
                cv.line(x, y, x, y + row_h - 2, palette::kAccentOrange);
            }
            if (is_cur && idx == static_cast<uint32_t>(c.app.cursor_step)) {
                cv.rect(x - 1, y, step_w, row_h, palette::kCursorWhite);
            }
        }

        char ltxt[8];
        std::snprintf(ltxt, sizeof(ltxt), "%u", len);
        cv.text(len_x, y + (row_h - 5) / 2, ltxt, is_cur ? palette::kAccentOrange : palette::kGridDim);
    }

    if (active < kTotalRows) {
        const int y = grid_y + active * row_h;
        if (on_add) {
            cv.fill(area.x + 2, y, area.w - 4, row_h - 1, palette::kHeaderSel);
            cv.rect(area.x + 2, y, area.w - 4, row_h - 1, palette::kAccentOrange);
        }
        cv.text(area.x + 4, y + (row_h - 5) / 2, "[+]", on_add ? palette::kAccentOrange : palette::kTextDim);
        cv.text(grid_x, y + (row_h - 5) / 2, "ADD TRACK", on_add ? palette::kAccentCyan : palette::kGridDim);
    }

    if (c.app.stick_plock_tweaked) {
        cv.text(area.x + 4, area.y + area.h - 8, "P-LOCK: STICK -> CUTOFF (RELEASE A TO KEEP)", palette::kParamTrig);
    }
}

// =====================================================================================
// ScopeView
// =====================================================================================
void ScopeView::render(Canvas& cv, const UiContext& c, Rect area) const {
    cv.text(area.x + 4, area.y + 3, "OUTPUT SCOPE & LEVELS", palette::kAccentCyan);
    widgets::scope(cv, Rect{area.x + 4, area.y + kHeaderH + 2, area.w - 8, 96}, c.tel.scope_samples, palette::kAccentCyan);

    const int y0 = area.y + kHeaderH + 104;
    const int tracks = std::max(1, (c.audio.getActiveRowCount() + kVoicesPerTrack - 1) / kVoicesPerTrack);
    for (int t = 0; t < kTracksCount; ++t) {
        const int y = y0 + t * 12;
        const auto tp = c.audio.getTrackParams(t);
        const bool on = t < tracks;
        char b[32];
        std::snprintf(b, sizeof(b), "T%d %-11s", t + 1, on ? getEngineTypeName(tp.engine) : "OFF");
        cv.text(area.x + 6, y + 2, b, on ? (tp.muted ? palette::kGridDim : palette::kTextMain) : palette::kGridDim);
        widgets::meter(cv, Rect{area.x + 70, y, area.w - 78, 9}, on && !tp.muted ? c.tel.track_levels[t] * 1.4f : 0.0f);
    }
    const int yv = y0 + 4 * 12 + 2;
    cv.text(area.x + 6, yv + 2, "OUT L", palette::kTextDim);
    widgets::meter(cv, Rect{area.x + 34, yv, (area.w - 50) / 2, 9}, c.tel.vu_left);
    cv.text(area.x + 38 + (area.w - 50) / 2, yv + 2, "R", palette::kTextDim);
    widgets::meter(cv, Rect{area.x + 46 + (area.w - 50) / 2, yv, (area.w - 50) / 2 - 4, 9}, c.tel.vu_right);
}

// =====================================================================================
// PerfView: живий перформанс та гра на педах
// =====================================================================================
void PerfView::navigate(UiContext& c, int dx, int dy, bool) {
    if (dx != 0) {
        const int n = std::max(1, (c.audio.getActiveRowCount() + kVoicesPerTrack - 1) / kVoicesPerTrack);
        c.app.selected_track = (c.app.selected_track + dx + n) % n;
    }
    if (dy != 0) {
        c.app.perf_octave = std::clamp(c.app.perf_octave - dy, 1, 6);
    }
}

std::string PerfView::focusId(const UiContext& c) const {
    char b[32];
    std::snprintf(b, sizeof(b), "PERF.T%d.C%d", c.app.selected_track + 1, c.app.perf_octave);
    return std::string(b);
}

void PerfView::render(Canvas& cv, const UiContext& c, Rect area) const {
    const int t = c.app.selected_track;
    const auto tp = c.audio.getTrackParams(t);
    const bool is_drum = (tp.engine == EngineType::kDrumSyn);
    const auto scale = c.app.arpeggiator.config().scale;
    const int root_semi = c.app.arpeggiator.config().root_note % 12;

    // 1. Заголовок
    char hdr[80];
    std::snprintf(hdr, sizeof(hdr), "PERF LIVE PLAY [T%d %s] OCT:%d SCALE:%s",
                  t + 1, getEngineTypeName(tp.engine), c.app.perf_octave, getScaleName(scale));
    cv.text(area.x + 4, area.y + 3, hdr, palette::kParamTrig);

    // 2. Чотири великі педи (Diamond Layout)
    struct PadInfo {
        const char* btn;
        std::string label;
        bool pressed;
        int x, y, w, h;
    };

    std::string n0, n1, n2, n3;
    if (is_drum) {
        n0 = "BD KICK";
        n1 = "SD SNARE";
        n2 = "CH CL.HAT";
        n3 = "OH OP.HAT";
    } else {
        auto chord = getChordIntervals(ChordType::kSeventh, scale);
        int base_root = c.app.perf_octave * 12 + root_semi;
        auto getNoteStr = [&](int offset, const char* deg) {
            int n = quantizeToScale(base_root + offset, base_root, scale);
            char b[24];
            constexpr const char* kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            std::snprintf(b, sizeof(b), "%s%d (%s)", kNotes[n % 12], n / 12 - 1, deg);
            return std::string(b);
        };
        n0 = getNoteStr(chord.size() > 0 ? chord[0] : 0, "ROOT");
        n1 = getNoteStr(chord.size() > 1 ? chord[1] : 4, "3RD");
        n2 = getNoteStr(chord.size() > 2 ? chord[2] : 7, "5TH");
        n3 = getNoteStr(chord.size() > 3 ? chord[3] : 11, "7TH");
    }

    const int cx = area.x + 105;
    const int cy = area.y + 70;
    const int pw = 68;
    const int ph = 30;

    PadInfo pads[4] = {
        {"[A]", n0, c.in.btn_a, cx - pw / 2, cy + 22, pw, ph},         // Bottom
        {"[B]", n1, c.in.btn_b, cx + 38,      cy - ph / 2, pw, ph},     // Right
        {"[X]", n2, c.in.btn_x, cx - pw - 38, cy - ph / 2, pw, ph},     // Left
        {"[Y]", n3, c.in.btn_y, cx - pw / 2, cy - ph - 22, pw, ph}      // Top
    };

    for (int i = 0; i < 4; ++i) {
        const auto& p = pads[i];
        ColorRGB border = p.pressed ? palette::kParamTrig : palette::kHeaderSel;
        ColorRGB bg = p.pressed ? palette::kParamTrig : palette::kBgDark;
        ColorRGB fg = p.pressed ? palette::kBgDark : palette::kTextMain;
        ColorRGB bfg = p.pressed ? palette::kBgDark : palette::kAccentOrange;

        if (p.pressed) {
            cv.fill(p.x, p.y, p.w, p.h, bg);
        } else {
            cv.fill(p.x, p.y, p.w, p.h, bg);
            cv.rect(p.x, p.y, p.w, p.h, border);
        }
        cv.text(p.x + 4, p.y + 4, p.btn, bfg);
        cv.text(p.x + 4, p.y + p.h - 9, p.label, fg);
    }

    // 3. Права колонка: Живі аналогові макро-контролери (Стіки)
    const int rx = area.x + 220;
    const int ry = area.y + 16;
    cv.text(rx, ry, "LIVE MACROS", palette::kAccentCyan);

    // Лівий стік
    cv.text(rx, ry + 10, "L: PITCH/CUT", palette::kTextDim);
    widgets::stickBox(cv, rx + 20, ry + 22, c.in.lx, c.in.ly, c.in.l3, palette::kAccentCyan);

    // Правий стік
    cv.text(rx, ry + 64, "R: RES/FX", palette::kTextDim);
    widgets::stickBox(cv, rx + 20, ry + 76, c.in.rx, c.in.ry, c.in.r3, palette::kAccentOrange);

    // 4. Нижня смужка підказок для швидкого керування
    const int by = area.y + area.h - 10;
    cv.text(area.x + 4, by, "DPAD: < > TRK  ^ v OCT  | HOLD L2: QUICK MUTE", palette::kGridDim);
}

} // namespace groovebox
