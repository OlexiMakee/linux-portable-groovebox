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
GridView::GridView(GridSpec spec) : IView(spec.id), spec_(std::move(spec)) {}

std::vector<std::pair<std::string, std::vector<const Param*>>> GridView::resolveRows(const UiContext& c) const {
    const auto& reg = ParamRegistry::instance();
    std::vector<GridRow> rlist = spec_.dynamic_rows ? spec_.dynamic_rows(c) : spec_.rows;
    std::vector<std::pair<std::string, std::vector<const Param*>>> out;
    out.reserve(rlist.size());
    for (const auto& row : rlist) {
        std::vector<const Param*> r;
        r.reserve(row.cells.size());
        for (const auto& id : row.cells) {
            r.push_back(id.empty() ? nullptr : reg.find(id));
        }
        out.emplace_back(row.title, std::move(r));
    }
    return out;
}

std::string GridView::tabTitle(const UiContext& c) const {
    return spec_.tab_title ? spec_.tab_title(c) : std::string(viewName(id()));
}

const Param* GridView::cellParam(const UiContext& c, int row, int col) const {
    auto rows = resolveRows(c);
    if (row < 0 || row >= static_cast<int>(rows.size())) return nullptr;
    const auto& r = rows[row].second;
    if (col < 0 || col >= static_cast<int>(r.size())) return nullptr;
    return r[col];
}

int GridView::rowOf(const UiContext& c) const {
    auto rows = resolveRows(c);
    const int n = static_cast<int>(rows.size());
    return n ? std::clamp(c.app.grid_row[static_cast<int>(id())], 0, n - 1) : 0;
}

int GridView::colOf(const UiContext& c) const {
    auto rows = resolveRows(c);
    const int r = rowOf(c);
    const int n = rows.empty() ? 0 : static_cast<int>(rows[r].second.size());
    return n ? std::clamp(c.app.grid_col[static_cast<int>(id())], 0, n - 1) : 0;
}

const Param* GridView::focusedParam(const UiContext& c) const {
    return cellParam(c, rowOf(c), colOf(c));
}

std::string GridView::focusId(const UiContext& c) const {
    const Param* p = focusedParam(c);
    return p ? p->id : std::string(viewName(id()));
}

void GridView::navigate(UiContext& c, int dx, int dy, bool coarse) {
    auto rows = resolveRows(c);
    const int num_rows = static_cast<int>(rows.size());
    if (num_rows == 0) return;
    const int vi = static_cast<int>(id());
    int row = rowOf(c);
    int col = colOf(c);

    if (dy != 0) {
        const int jump = coarse ? 3 : 1;
        row = (row + dy * jump % num_rows + num_rows) % num_rows;
        col = std::min(col, static_cast<int>(rows[row].second.size()) - 1);
    }
    if (dx != 0) {
        const int cols = static_cast<int>(rows[row].second.size());
        if (cols > 0) col = (col + dx + cols) % cols;
    }
    c.app.grid_row[vi] = row;
    c.app.grid_col[vi] = std::max(0, col);

    // Автоскрол, щоб курсор завжди був видимим
    const int vis = visibleRows(spec_, workspaceRect());
    int& scroll = c.app.grid_scroll[vi];
    if (row < scroll) scroll = row;
    if (row >= scroll + vis) scroll = row - vis + 1;
    scroll = std::clamp(scroll, 0, std::max(0, num_rows - vis));

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
    Rect box = area;
    int table_y0 = area.y + kHeaderH;
    int cells_x = area.x + kTitleW;
    int cells_w = area.w - kTitleW - 3;

    // Якщо це арпеджиатор — рендеримо його як центроване модальне вікно з рамкою
    if (spec_.id == ViewId::kArp) {
        box.w = 260;
        box.h = 132;
        box.x = area.x + (area.w - box.w) / 2;
        box.y = area.y + (area.h - box.h) / 2 - 2;

        cv.fill(box.x, box.y, box.w, box.h, palette::kPanelBg);
        cv.rect(box.x, box.y, box.w, box.h, spec_.accent);
        cv.fill(box.x + 1, box.y + 1, box.w - 2, 10, palette::kHeaderSel);
        if (spec_.header) cv.text(box.x + 4, box.y + 3, spec_.header(c), spec_.accent);
        cv.text(box.x + box.w - 38, box.y + 3, "[L3:X]", palette::kTextDim);

        table_y0 = box.y + 14;
        cells_x = box.x + kTitleW;
        cells_w = box.w - kTitleW - 4;
    } else if (spec_.header) {
        cv.text(area.x + 4, area.y + 3, spec_.header(c), spec_.accent);
    }

    auto rows = resolveRows(c);
    const int total_rows = static_cast<int>(rows.size());
    // У вкладці MIX показуємо тільки стільки смуг, скільки реально існує + рядок Master/Bus
    const int act_tracks = (spec_.id == ViewId::kMix) ? std::min(total_rows - 1, c.audio.getActiveRowCount()) : total_rows;
    const int rows_to_draw = (spec_.id == ViewId::kMix) ? act_tracks + 1 : total_rows;

    const int sel_r = rowOf(c);
    const int sel_c = colOf(c);

    for (int i = 0; i < rows_to_draw; ++i) {
        // Для MIX останній рядок — завжди MST (індекс total_rows - 1)
        const int r = (spec_.id == ViewId::kMix && i == act_tracks) ? (total_rows - 1) : i;
        const int y = table_y0 + i * kTableRowH;
        cv.text(box.x + 4, y + 8, rows[r].first, r == sel_r ? spec_.accent : palette::kTextDim);

        const auto& cells = rows[r].second;
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
        // Нижня панель індикації: вертикальні міні-VU для кожного треку зліва направо + квадратний Scope (24x24) + Master L/R VU
        const int lower_y = table_y0 + rows_to_draw * kTableRowH + 4;
        const int lower_h = (area.y + area.h) - lower_y - 2;

        if (lower_h >= 16) {
            // 1. Вертикальні міні-VU для кожної доріжки зліва направо
            for (int t = 0; t < act_tracks; ++t) {
                const int mx = area.x + 8 + t * 15;
                char tb[8];
                std::snprintf(tb, sizeof(tb), "T%d", t + 1);
                cv.text(mx, lower_y + lower_h - 6, tb, palette::kTextDim);
                widgets::verticalMeter(cv, Rect{mx, lower_y, 9, lower_h - 8}, c.tel.track_levels[t]);
            }

            // 2. Справа: Master L та R вертикальні міні-VU
            const int mst_x = area.x + area.w - 22;
            widgets::verticalMeter(cv, Rect{mst_x, lower_y, 8, lower_h - 8}, c.tel.vu_left);
            widgets::verticalMeter(cv, Rect{mst_x + 10, lower_y, 8, lower_h - 8}, c.tel.vu_right);
            cv.text(mst_x + 3, lower_y + lower_h - 6, "M", palette::kAccentOrange);

            // 3. Ліворуч від Master шкали: маленький квадратний осцилограф (без напису SCOPE!)
            const int sc_dim = std::min(32, lower_h - 8);
            const int sc_x = mst_x - sc_dim - 10;
            widgets::scope(cv, Rect{sc_x, lower_y, sc_dim, sc_dim}, c.tel.scope_samples, palette::kAccentCyan);
        }
    }
}

void GridView::renderList(Canvas& cv, const UiContext& c, Rect area, bool popup) const {
    const int vi = static_cast<int>(id());
    auto rows = resolveRows(c);
    const int num_rows = static_cast<int>(rows.size());
    const int sel_r = rowOf(c);

    Rect box = area;
    int row_h = kListRowH;
    int value_x = 110;
    int y0 = area.y + kHeaderH + 2;
    if (popup) {
        row_h = kPopupRowH;
        value_x = 54;
        box.w = 236; // Ширше вікно для відображення піано-ролу акордів
        box.h = std::min(area.h - 8, num_rows * row_h + 18);
        box.x = area.x + (area.w - box.w) / 2;
        box.y = area.y + 6;
        cv.fill(box.x, box.y, box.w, box.h, palette::kPanelBg);
        cv.rect(box.x, box.y, box.w, box.h, spec_.accent);
        cv.fill(box.x + 1, box.y + 1, box.w - 2, 10, palette::kHeaderSel);
        if (spec_.header) cv.text(box.x + 4, box.y + 3, spec_.header(c), spec_.accent);
        y0 = box.y + 14;

        // Міні-піанорол акорду праворуч (Digitakt chord mini-roll)
        const int pr_x = box.x + 154;
        const int pr_y = y0;
        const int pr_w = box.w - 158;
        const int pr_h = box.h - 18;

        cv.rect(pr_x, pr_y, pr_w, pr_h, palette::kGridDim);
        cv.text(pr_x + 4, pr_y + 2, "CHORD ROLL", palette::kTextDim);

        // Отримуємо активні ноти на поточному кроці
        const int cur_r = c.app.cursor_row;
        const int cur_s = c.app.cursor_step;
        const auto st = (cur_r < c.audio.getActiveRowCount()) ? c.audio.getStep(cur_r, cur_s) : SequencerStep{};

        const int base_oct = st.note / 12;
        const int key_h = std::max(6, (pr_h - 16) / 12);
        static constexpr bool is_black[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
        static constexpr const char* kKeyNames[12] = {"B", "A#", "A", "G#", "G", "F#", "F", "E", "D#", "D", "C#", "C"};
        static constexpr int kKeyPitch[12] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

        for (int k = 0; k < 12; ++k) {
            int ky = pr_y + 12 + k * key_h;
            int pitch = base_oct * 12 + kKeyPitch[k];
            bool active = (st.note == pitch);
            for (int cn = 1; cn < 4; ++cn) {
                if (st.chord_notes[cn] == pitch) active = true;
            }

            ColorRGB bg = active ? palette::kAccentCyan : (is_black[k] ? ColorRGB{20, 24, 32} : ColorRGB{52, 60, 72});
            cv.fill(pr_x + 2, ky, pr_w - 4, key_h - 1, bg);
            ColorRGB tc = active ? palette::kBgDark : (is_black[k] ? palette::kTextDim : palette::kTextMain);
            cv.text(pr_x + 4, ky + 1, kKeyNames[k], tc);
        }
    } else if (spec_.header) {
        cv.text(area.x + 4, area.y + 3, spec_.header(c), spec_.accent);
    }

    const int vis = visibleRows(spec_, area);
    const int scroll = std::clamp(c.app.grid_scroll[vi], 0, std::max(0, num_rows - vis));
    for (int i = 0; i < vis && scroll + i < num_rows; ++i) {
        const int r = scroll + i;
        const Param* p = rows[r].second.empty() ? nullptr : rows[r].second[0];
        if (!p) continue;
        const int row_w = popup ? 148 : box.w - 6;
        const Rect rr{box.x + 3, y0 + i * row_h, row_w, row_h - 1};
        widgets::listRow(cv, rr, p->labelFor(c), p->value(c), r == sel_r, p->editable(), value_x, spec_.accent);
    }
}

// =====================================================================================
// SeqView
// =====================================================================================
namespace {

const char* voiceLabel(EngineType e, int voice) {
    (void)voice;
    switch (e) {
        case EngineType::kSampleOneShot: return "SMP";
        case EngineType::kDrumRack:     return "DRM";
        case EngineType::kAcid303:      return "303";
        case EngineType::kDx7:          return "DX7";
        case EngineType::kChordPad:     return "PAD";
        case EngineType::kFm2Op:        return "FM2";
        case EngineType::kVaMono:       return "VAM";
        case EngineType::kSliceSampler: return "SLC";
        case EngineType::kMidiOut:      return "MID";
        default:                        return "TRK";
    }
}

constexpr const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

} // namespace

void SeqView::navigate(UiContext& c, int dx, int dy, bool coarse) {
    const int active = c.audio.getActiveRowCount();
    const int slots = active < kTotalRows ? active + 1 : active;
    if (dy != 0) {
        c.app.cursor_row = ((c.app.cursor_row + dy) % slots + slots) % slots;
        if (c.app.cursor_row < active) {
            c.app.selected_track = c.audio.getRowTrack(c.app.cursor_row);
            const uint32_t len = c.audio.getRowLength(c.app.cursor_row);
            if (c.app.cursor_step >= static_cast<int>(len)) c.app.cursor_step = static_cast<int>(len) - 1;
        }
    }
    if (dx != 0 && c.app.cursor_row < active) {
        const int len = static_cast<int>(c.audio.getRowLength(c.app.cursor_row));
        const int jump = coarse ? 4 : 1;
        c.app.cursor_step = ((c.app.cursor_step + dx * jump) % len + len) % len;
    }
    // Плавний горизонтальний скрол: сітка слідує за курсором по 1 кроку
    const int vis_steps = static_cast<int>(kStepsPerPage);
    if (c.app.cursor_step < c.app.seq_scroll_step) {
        c.app.seq_scroll_step = c.app.cursor_step;
    } else if (c.app.cursor_step >= c.app.seq_scroll_step + vis_steps) {
        c.app.seq_scroll_step = c.app.cursor_step - vis_steps + 1;
    }
    const int cur_len = (c.app.cursor_row < active) ? static_cast<int>(c.audio.getRowLength(c.app.cursor_row)) : 16;
    c.app.seq_scroll_step = std::clamp(c.app.seq_scroll_step, 0, std::max(0, cur_len - vis_steps));
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
        std::snprintf(hdr, sizeof(hdr), "R%02d %s | ST%03d %s%d %s%s | L%u",
                      cur_r + 1, getEngineTypeName(tp.engine), c.app.cursor_step + 1,
                      kNoteNames[st.note % 12], st.note / 12 - 1, trig,
                      (st.slide && tp.engine != EngineType::kDrumSyn) ? " SLD" : "",
                      cur_len);
        cv.text(area.x + 4, area.y + 3, hdr, st.is_param_trig ? palette::kParamTrig : palette::kAccentOrange);
    }

    const int grid_x = area.x + 28;
    const int step_w = 8;
    const int grid_y = area.y + kHeaderH + 2;
    const int slots = active < kTotalRows ? active + 1 : active;
    const int row_h = std::clamp((area.h - kHeaderH - 4) / std::max(1, slots), 10, 20);
    const uint32_t scroll_off = static_cast<uint32_t>(c.app.seq_scroll_step);
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
            const uint32_t idx = scroll_off + s;
            const int x = grid_x + static_cast<int>(s) * step_w;
            if (idx < len) {
                // Шахматка відтінків сірого: чергування 4-крокових долей (бітів) та рядків
                const bool beat_alt = (((s / 4) + r) % 2 == 1);
                const ColorRGB bg_col = beat_alt ? ColorRGB{26, 26, 32} : ColorRGB{16, 16, 20};
                cv.fill(x, y + 1, step_w - 1, th, bg_col);

                const auto sd = c.audio.getStep(r, static_cast<int>(idx));
                if (sd.enabled) {
                    const ColorRGB col = sd.is_param_trig ? palette::kParamTrig
                                       : (trk.muted ? palette::kGridDim
                                       : (t % 2 == 0 ? palette::kLevelOk : palette::kAccentCyan));
                    cv.fill(x + 1, y + 2, step_w - 3, th - 2, col);
                } else {
                    if (s % 4 == 0) {
                        // Контрастний маркер початку долі (чверті)
                        cv.fill(x + 2, y + row_h / 2 - 1, 2, 2, (s % 16 == 0) ? palette::kTextDim : palette::kGridDim);
                    } else {
                        // Делікатна крапка порожнього кроку
                        cv.fill(x + 3, y + row_h / 2, 1, 1, ColorRGB{42, 42, 50});
                    }
                }
                if (idx == play) cv.line(x + 3, y, x + 3, y + row_h - 2, palette::kCursorWhite);
            } else if (idx == len) {
                // Чітка контрастна лінія межі довжини треку
                cv.line(x, y, x, y + row_h - 2, palette::kAccentOrange);
            } else {
                // За межами активної довжини: приглушена фонова сітка
                const bool beat_alt = (((s / 4) + r) % 2 == 1);
                const ColorRGB dim_bg = beat_alt ? ColorRGB{12, 12, 16} : ColorRGB{8, 8, 10};
                cv.fill(x, y + 1, step_w - 1, th, dim_bg);
                if (s % 4 == 0) {
                    cv.fill(x + 3, y + row_h / 2, 1, 1, ColorRGB{22, 22, 28});
                }
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
// SampleView — станція семплювання та лупера
// =====================================================================================
static const char* kSampleParams[2][4] = {
    {"SMP.SRC", "SMP.ARM", "SMP.MODE", "SMP.THRS"},
    {"SMP.TRIM_ST", "SMP.TRIM_END", "SMP.ZOOM", "SMP.ASSIGN"}
};

void SampleView::navigate(UiContext& c, int dx, int dy, bool) {
    if (dy != 0) {
        cursor_row_ = (cursor_row_ + dy + 2) % 2;
    }
    if (dx != 0) {
        cursor_col_ = (cursor_col_ + dx + 4) % 4;
    }
    c.app.grid_row[static_cast<int>(ViewId::kSample)] = cursor_row_;
    c.app.grid_col[static_cast<int>(ViewId::kSample)] = cursor_col_;
}

const Param* SampleView::focusedParam(const UiContext&) const {
    int r = std::clamp(cursor_row_, 0, 1);
    int col = std::clamp(cursor_col_, 0, 3);
    return ParamRegistry::instance().find(kSampleParams[r][col]);
}

std::string SampleView::focusId(const UiContext& c) const {
    const Param* p = focusedParam(c);
    return p ? p->id : "SAMPLE";
}

void SampleView::render(Canvas& cv, const UiContext& c, Rect area) const {
    // 1. Заголовок
    char hdr[64];
    auto st = c.app.sampler.recordingState();
    const char* st_name = (st == RecState::kRecording) ? "RECORDING..." : (st == RecState::kArmed ? "ARMED (WAITING)" : "STANDBY");
    std::snprintf(hdr, sizeof(hdr), "SAMPLING WORKSTATION [%s]", st_name);
    cv.text(area.x + 4, area.y + 3, hdr, (st == RecState::kRecording) ? palette::kLevelWarnClip : palette::kAccentCyan);

    // 2. Велике вікно візуалізації хвилеформи (Elektron Digitakt style)
    const int wf_x = area.x + 4;
    const int wf_y = area.y + kHeaderH + 2;
    const int wf_w = area.w - 8;
    const int wf_h = 70;

    cv.fill(wf_x, wf_y, wf_w, wf_h, palette::kPanelBg);
    cv.rect(wf_x, wf_y, wf_w, wf_h, palette::kGridDim);

    // Підсвітка активної зони обрізки (Trim Start .. Trim End)
    const int st_px = wf_x + static_cast<int>(c.app.sample_trim_start * (wf_w - 2));
    const int end_px = wf_x + static_cast<int>(c.app.sample_trim_end * (wf_w - 2));
    cv.fill(st_px, wf_y + 1, std::max(2, end_px - st_px), wf_h - 2, ColorRGB{16, 28, 36});

    // Центральна нульова вісь
    const int mid_y = wf_y + wf_h / 2;
    cv.line(wf_x + 1, mid_y, wf_x + wf_w - 2, mid_y, ColorRGB{32, 40, 52});

    const auto& sbuf = c.app.sampler.recordedSample();
    const uint32_t total_frames = sbuf.lengthFrames();

    if (total_frames > 64) {
        // Відмальовуємо піки хвилі з буфера
        const float step_frames = static_cast<float>(total_frames) / static_cast<float>(wf_w - 4);
        for (int px = 0; px < wf_w - 4; ++px) {
            uint32_t f_idx = static_cast<uint32_t>(px * step_frames);
            if (f_idx >= total_frames) break;
            float amp = std::abs(sbuf.data_l[f_idx]);
            int bar_h = std::clamp(static_cast<int>(amp * (wf_h / 2 - 2)), 1, wf_h / 2 - 2);
            cv.line(wf_x + 2 + px, mid_y - bar_h, wf_x + 2 + px, mid_y + bar_h, palette::kAccentCyan);
        }
    } else {
        // Якщо буфер порожній — відображаємо живий монітор сигналу
        widgets::scope(cv, Rect{wf_x + 2, wf_y + 2, wf_w - 4, wf_h - 4}, c.tel.scope_samples, palette::kAccentCyan);
    }

    // Маркери TRIM START та TRIM END
    cv.line(st_px, wf_y + 1, st_px, wf_y + wf_h - 2, palette::kAccentOrange);
    cv.text(st_px + 2, wf_y + 3, "ST", palette::kAccentOrange);

    cv.line(end_px, wf_y + 1, end_px, wf_y + wf_h - 2, palette::kAccentCyan);
    cv.text(std::max(wf_x + 2, end_px - 18), wf_y + wf_h - 9, "END", palette::kAccentCyan);

    // 3. Таблиця параметрів під хвилеформою (2 рядки по 4 параметри)
    const int table_y = wf_y + wf_h + 4;
    const int cell_w = (wf_w - 20) / 4;
    const int cell_h = 24;

    for (int r = 0; r < 2; ++r) {
        for (int col = 0; col < 4; ++col) {
            const Param* p = ParamRegistry::instance().find(kSampleParams[r][col]);
            if (!p) continue;
            const int cx = wf_x + col * (cell_w + 2);
            const int cy = table_y + r * (cell_h + 2);
            const bool is_sel = (r == cursor_row_ && col == cursor_col_);
            widgets::paramCell(cv, Rect{cx, cy, cell_w, cell_h}, p->labelFor(c), p->value(c), p->norm ? p->norm(c) : -1.0f, is_sel, p->editable(), palette::kAccentCyan);
        }
    }

    // 4. Вертикальний VU-метр входу праворуч
    const int meter_x = wf_x + 4 * (cell_w + 2) + 2;
    widgets::verticalMeter(cv, Rect{meter_x, table_y, 8, cell_h * 2 + 2}, c.app.sampler.inputLevelL());
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
