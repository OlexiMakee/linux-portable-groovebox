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
    if (s.id == ViewId::kMix || s.id == ViewId::kFx) return 7;
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

        ColorRGB effective_accent = spec_.accent;
        if (spec_.id == ViewId::kSynth) {
            if (c.app.synth_submode == SynthSubmode::kMod) effective_accent = palette::kAccentPurple;
            else if (c.app.synth_submode == SynthSubmode::kFx) effective_accent = palette::kLevelOk;
            else effective_accent = palette::kAccentCyan;
        }

        cv.fill(box.x, box.y, box.w, box.h, palette::kPanelBg);
        cv.rect(box.x, box.y, box.w, box.h, effective_accent);
        cv.fill(box.x + 1, box.y + 1, box.w - 2, 10, palette::kHeaderSel);
        if (spec_.header) cv.text(box.x + 4, box.y + 3, spec_.header(c), effective_accent);
        cv.text(box.x + box.w - 38, box.y + 3, "[L3:X]", palette::kTextDim);

        table_y0 = box.y + 14;
        cells_x = box.x + kTitleW;
        cells_w = box.w - kTitleW - 4;
    } else if (spec_.header) {
        ColorRGB effective_accent = spec_.accent;
        if (spec_.id == ViewId::kSynth) {
            if (c.app.synth_submode == SynthSubmode::kMod) effective_accent = palette::kAccentPurple;
            else if (c.app.synth_submode == SynthSubmode::kFx) effective_accent = palette::kLevelOk;
            else effective_accent = palette::kAccentCyan;
        }
        cv.text(area.x + 4, area.y + 3, spec_.header(c), effective_accent);
    }

    ColorRGB effective_accent = spec_.accent;
    if (spec_.id == ViewId::kSynth) {
        if (c.app.synth_submode == SynthSubmode::kMod) effective_accent = palette::kAccentPurple;
        else if (c.app.synth_submode == SynthSubmode::kFx) effective_accent = palette::kLevelOk;
        else effective_accent = palette::kAccentCyan;
    }

    auto rows = resolveRows(c);
    const int total_rows = static_cast<int>(rows.size());
    const int sel_r = rowOf(c);
    const int sel_c = colOf(c);

    const bool has_pinned_bottom = (spec_.id == ViewId::kMix || spec_.id == ViewId::kFx);
    const int vis = has_pinned_bottom ? 7 : visibleRows(spec_, area);
    const int scroll = std::clamp(c.app.grid_scroll[static_cast<int>(spec_.id)], 0, std::max(0, total_rows - vis));
    const int rows_to_draw = std::min(vis, total_rows - scroll);

    for (int i = 0; i < rows_to_draw; ++i) {
        const int r = scroll + i;
        const int y = table_y0 + i * kTableRowH;
        cv.text(box.x + 4, y + 8, rows[r].first, r == sel_r ? effective_accent : palette::kTextDim);

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

            ModTarget tgt = ModTarget::kNone;
            const std::string& pid = p->id;
            if (pid == "TRK.LPF_CUT") tgt = ModTarget::kCutoff;
            else if (pid == "TRK.LPF_RES") tgt = ModTarget::kResonance;
            else if (pid == "TRK.LPF_ENV") tgt = ModTarget::kEnvMod;
            else if (pid == "TRK.LPF_DEC") tgt = ModTarget::kLpfDecay;
            else if (pid == "TRK.HPF_CUT") tgt = ModTarget::kHpfCutoff;
            else if (pid == "TRK.HPF_RES") tgt = ModTarget::kHpfResonance;
            else if (pid == "TRK.HPF_ENV") tgt = ModTarget::kHpfEnv;
            else if (pid == "TRK.HPF_DEC") tgt = ModTarget::kHpfDecay;
            else if (pid == "TRK.GAIN_OVER") tgt = ModTarget::kGainOver;
            else if (pid == "TRK.GLIDE") tgt = ModTarget::kGlide;
            else if (pid == "TRK.P1") tgt = ModTarget::kParam1;
            else if (pid == "TRK.P2") tgt = ModTarget::kParam2;
            else if (pid == "TRK.P3") tgt = ModTarget::kParam3;
            else if (pid == "TRK.P4") tgt = ModTarget::kParam4;
            else if (pid == "TRK.P5") tgt = ModTarget::kParam5;
            else if (pid == "TRK.P6") tgt = ModTarget::kParam6;
            else if (pid == "TRK.ATK") tgt = ModTarget::kAmpAttack;
            else if (pid == "TRK.DEC") tgt = ModTarget::kAmpDecay;
            else if (pid == "TRK.SUS") tgt = ModTarget::kAmpSustain;
            else if (pid == "TRK.REL") tgt = ModTarget::kAmpRelease;
            else if (pid == "TRK.PAN") tgt = ModTarget::kPan;
            else if (pid == "TRK.LVL") tgt = ModTarget::kLevel;
            else if (pid == "TRK.DLY") tgt = ModTarget::kDelaySend;
            else if (pid == "TRK.REV") tgt = ModTarget::kReverbSend;
            else if (pid == "LFO1.RATE") tgt = ModTarget::kLfo1Rate;
            else if (pid == "LFO1.DEPTH") tgt = ModTarget::kLfo1Depth;
            else if (pid == "LFO2.RATE") tgt = ModTarget::kLfo2Rate;
            else if (pid == "LFO2.DEPTH") tgt = ModTarget::kLfo2Depth;
            else if (pid == "LFO3.RATE") tgt = ModTarget::kLfo3Rate;
            else if (pid == "LFO3.DEPTH") tgt = ModTarget::kLfo3Depth;
            else if (pid == "LFO4.RATE") tgt = ModTarget::kLfo4Rate;
            else if (pid == "LFO4.DEPTH") tgt = ModTarget::kLfo4Depth;
            else if (pid == "ENV1.ATK") tgt = ModTarget::kEnv1Attack;
            else if (pid == "ENV1.DEC") tgt = ModTarget::kEnv1Decay;
            else if (pid == "ENV1.SUS") tgt = ModTarget::kEnv1Sustain;
            else if (pid == "ENV1.REL") tgt = ModTarget::kEnv1Release;
            else if (pid == "ENV2.ATK") tgt = ModTarget::kEnv2Attack;
            else if (pid == "ENV2.DEC") tgt = ModTarget::kEnv2Decay;
            else if (pid == "ENV2.SUS") tgt = ModTarget::kEnv2Sustain;
            else if (pid == "ENV2.REL") tgt = ModTarget::kEnv2Release;

            bool is_mod_target = false;
            float live_norm = -1.0f;
            if (tgt != ModTarget::kNone && norm >= 0.0f) {
                int trk = c.app.selected_track;
                const auto& tp = c.audio.getTrackParams(trk);
                if ((tp.lfo1.dest == tgt && tp.lfo1.depth > 0.001f) ||
                    (tp.lfo2.dest == tgt && tp.lfo2.depth > 0.001f) ||
                    (tp.lfo3.dest == tgt && tp.lfo3.depth > 0.001f) ||
                    (tp.lfo4.dest == tgt && tp.lfo4.depth > 0.001f) ||
                    (tp.env1.dest == tgt && std::fabs(tp.env1.amount) > 0.001f) ||
                    (tp.env2.dest == tgt && std::fabs(tp.env2.amount) > 0.001f) ||
                    (tp.stick_l_m1.dest_x == tgt || tp.stick_l_m1.dest_y == tgt ||
                     tp.stick_l_m2.dest_x == tgt || tp.stick_l_m2.dest_y == tgt ||
                     tp.stick_r_m1.dest_x == tgt || tp.stick_r_m1.dest_y == tgt ||
                     tp.stick_r_m2.dest_x == tgt || tp.stick_r_m2.dest_y == tgt)) {
                    is_mod_target = true;
                    live_norm = c.audio.getLiveModValue(trk, tgt, norm);
                }
            }

            widgets::paramCell(cv, cell, p->labelFor(c), p->value(c), norm,
                               r == sel_r && k == sel_c, p->editable(), effective_accent,
                               live_norm, is_mod_target);
        }
    }

    // Тонка смуга вертикального скролу для таблиці
    if (total_rows > vis) {
        const int bar_x = area.x + area.w - 3;
        const int bar_y0 = table_y0;
        const int bar_h = vis * kTableRowH;
        cv.fill(bar_x, bar_y0, 2, bar_h, palette::kPanelBg);
        const int thumb_h = std::max(8, bar_h * vis / total_rows);
        const int thumb_y = bar_y0 + (bar_h - thumb_h) * scroll / std::max(1, total_rows - vis);
        cv.fill(bar_x, thumb_y, 2, thumb_h, spec_.accent);
    }

    // Візуалізатор форми хвилі для семплерних движків (Digitakt style: START, LOOP, END + Live Playhead)
    if (spec_.id == ViewId::kSynth && c.app.synth_submode == SynthSubmode::kSynth) {
        int trk_idx = c.app.selected_track;
        const auto tp = c.audio.getTrackParams(trk_idx);
        if (tp.engine == EngineType::kSampleOneShot || tp.engine == EngineType::kSliceSampler) {
            const int wf_x = area.x + 4;
            const int wf_y = 114;
            const int wf_w = area.w - 8;
            const int wf_h = (area.y + area.h) - wf_y - 2;

            cv.fill(wf_x, wf_y, wf_w, wf_h, palette::kPanelBg);
            cv.rect(wf_x, wf_y, wf_w, wf_h, palette::kGridDim);

            const auto& sb = (tp.engine == EngineType::kSampleOneShot)
                             ? c.audio.sampler().trackSample(trk_idx)
                             : c.audio.sampler().recordedSample();

            if (sb.loaded && sb.lengthFrames() > 0) {
                const uint32_t total_frames = sb.lengthFrames();
                const int mid_y = wf_y + wf_h / 2 + 5;
                const int max_amp_h = (wf_h / 2) - 10;

                cv.line(wf_x + 1, mid_y, wf_x + wf_w - 2, mid_y, palette::kGridDim);

                float st_norm = std::clamp(tp.sample.start, 0.0f, 0.999f);
                float lp_norm = std::clamp(tp.sample.loop_point, 0.0f, 0.999f);
                float end_norm = std::clamp(tp.sample.end, 0.001f, 1.0f);
                if (end_norm <= st_norm) end_norm = std::min(1.0f, st_norm + 0.01f);

                int px_start = wf_x + 1 + static_cast<int>(st_norm * (wf_w - 2));
                int px_loop  = wf_x + 1 + static_cast<int>(lp_norm * (wf_w - 2));
                int px_end   = wf_x + 1 + static_cast<int>(end_norm * (wf_w - 2));

                if (tp.sample.loop_mode != SampleLoopMode::kOff && px_end > px_loop) {
                    cv.fill(px_loop, wf_y + 12, px_end - px_loop, wf_h - 13, ColorRGB{20, 36, 46});
                }

                const int wave_w = wf_w - 2;
                for (int x = 0; x < wave_w; ++x) {
                    uint32_t f_start = static_cast<uint32_t>(static_cast<uint64_t>(x) * total_frames / wave_w);
                    uint32_t f_end   = static_cast<uint32_t>(static_cast<uint64_t>(x + 1) * total_frames / wave_w);
                    f_end = std::clamp(f_end, f_start + 1, total_frames);

                    float min_v = 0.0f, max_v = 0.0f;
                    for (uint32_t f = f_start; f < f_end; ++f) {
                        float s = sb.data_l[f];
                        if (s < min_v) min_v = s;
                        if (s > max_v) max_v = s;
                    }
                    int y1 = mid_y - static_cast<int>(max_v * max_amp_h);
                    int y2 = mid_y - static_cast<int>(min_v * max_amp_h);
                    if (y1 == y2) y2 = y1 + 1;

                    int cur_px = wf_x + 1 + x;
                    ColorRGB wave_col = (cur_px >= px_start && cur_px <= px_end) ? palette::kAccentCyan : palette::kTextDim;
                    cv.line(cur_px, y1, cur_px, y2, wave_col);
                }

                // START (жовтий, Elektron style)
                cv.line(px_start, wf_y + 11, px_start, wf_y + wf_h - 2, palette::kParamTrig);
                cv.text(std::clamp(px_start - 3, wf_x + 2, wf_x + wf_w - 12), wf_y + 2, "S", palette::kParamTrig);

                // LOOP POINT (бірюзовий)
                if (tp.sample.loop_mode != SampleLoopMode::kOff) {
                    cv.line(px_loop, wf_y + 11, px_loop, wf_y + wf_h - 2, ColorRGB{0, 240, 200});
                    cv.text(std::clamp(px_loop - 3, wf_x + 2, wf_x + wf_w - 12), wf_y + 2, "L", ColorRGB{0, 240, 200});
                }

                // END (червоний)
                cv.line(px_end, wf_y + 11, px_end, wf_y + wf_h - 2, palette::kLevelWarnClip);
                cv.text(std::clamp(px_end - 3, wf_x + 2, wf_x + wf_w - 12), wf_y + 2, "E", palette::kLevelWarnClip);

                // Живий плейхед
                float ph = c.audio.trackPlayhead(trk_idx);
                if (ph >= 0.0f && ph <= 1.0f) {
                    int px_head = wf_x + 1 + static_cast<int>(ph * (wf_w - 2));
                    cv.fill(px_head, wf_y + 11, 2, wf_h - 12, palette::kCursorWhite);
                }

                char info[64];
                float sec = static_cast<float>(total_frames) / std::max(1u, sb.sample_rate);
                std::snprintf(info, sizeof(info), "SMP: %s | %s | %.2fs",
                              sb.name.c_str(), getSampleLoopModeName(tp.sample.loop_mode), sec);
                cv.text(wf_x + 24, wf_y + 2, info, palette::kTextDim);
            } else {
                cv.text(wf_x + 20, wf_y + wf_h / 2 - 3, "NO SAMPLE LOADED (SELECT WAV IN [R1] BROWSER)", palette::kTextDim);
            }
        }
    }

    // Закріплений знизу над HUD лейаут індикації (Scope + VU для кожного треку + Master)
    if (has_pinned_bottom) {
        const int lower_y = area.y + area.h - 26;
        cv.line(area.x, lower_y, area.x + area.w - 1, lower_y, palette::kGridDim);
        cv.fill(area.x, lower_y + 1, area.w, 25, palette::kPanelBg);

        const int mst_x = area.x + area.w - 24;

        if (spec_.id == ViewId::kMix && c.app.mix_submode == MixSubmode::kMaster) {
            // ЕКРАН МАСТЕРИНГУ: графік еквалайзеру (АЧХ) ліворуч/по центру,
            // а праворуч від АЧХ — смуги мастера
            const int eq_x0 = area.x + 6;
            const int eq_w = mst_x - eq_x0 - 6;
            const int eq_y0 = lower_y + 2;
            const int eq_h = 22;
            const int zero_y = eq_y0 + eq_h / 2;

            // Рамка та частотна сітка еквалайзера
            cv.rect(eq_x0 - 1, eq_y0 - 1, eq_w + 2, eq_h + 2, palette::kGridDim);

            const int x_100 = eq_x0 + static_cast<int>(eq_w * 0.233f);
            const int x_1k  = eq_x0 + static_cast<int>(eq_w * 0.566f);
            const int x_10k = eq_x0 + static_cast<int>(eq_w * 0.900f);

            cv.line(x_100, eq_y0, x_100, eq_y0 + eq_h - 1, palette::kGridDim);
            cv.line(x_1k,  eq_y0, x_1k,  eq_y0 + eq_h - 1, palette::kGridDim);
            cv.line(x_10k, eq_y0, x_10k, eq_y0 + eq_h - 1, palette::kGridDim);

            // Нульова лінія 0 dB (пунктир)
            for (int dx = 0; dx < eq_w; dx += 4) {
                cv.fill(eq_x0 + dx, zero_y, 1, 1, palette::kGridDim);
            }

            // Спектральний аналізатор реального часу (RTA 24-band spectrum under EQ curve)
            const int num_bands = 24;
            float band_w = static_cast<float>(eq_w) / static_cast<float>(num_bands);
            for (int b = 0; b < num_bands; ++b) {
                float band_mag = 0.0f;
                int k_step = std::max(1, 128 / num_bands);
                int k_start = b * k_step;
                int k_end = std::min(128, k_start + k_step);
                for (int k = k_start; k < k_end; ++k) {
                    float s = c.tel.scope_samples[k];
                    band_mag += std::fabs(s);
                }
                band_mag /= std::max(1, k_end - k_start);
                float h_norm = std::clamp(band_mag * 2.2f, 0.0f, 1.0f);
                int bar_h = static_cast<int>(h_norm * (eq_h - 2));
                if (bar_h > 0) {
                    int bx = eq_x0 + static_cast<int>(b * band_w);
                    int bw = std::max(1, static_cast<int>(band_w) - 1);
                    cv.fill(bx, eq_y0 + eq_h - 1 - bar_h, bw, bar_h, ColorRGB{28, 52, 70});
                }
            }

            // Отримуємо параметри еквалайзера
            const auto mfx = c.audio.getMasterFx();
            float f_hpf = (mfx.hpf_cutoff > 0.005f) ? (20.0f * std::pow(800.0f, mfx.hpf_cutoff)) : 0.0f;
            float hpf_order = mfx.hpf_slope_24 ? 4.0f : 2.0f;
            float f_bell = 40.0f * std::pow(400.0f, mfx.bell_freq);
            float bell_q = std::clamp(mfx.bell_q, 0.2f, 10.0f);
            float f_lpf = (mfx.lpf_cutoff < 0.995f) ? (20.0f * std::pow(1000.0f, mfx.lpf_cutoff)) : 30000.0f;
            float lpf_order = mfx.lpf_slope_24 ? 4.0f : 2.0f;

            int prev_cy = zero_y;
            for (int dx = 0; dx < eq_w; ++dx) {
                float f = 20.0f * std::pow(1000.0f, static_cast<float>(dx) / static_cast<float>(eq_w));

                float hpf_db = 0.0f;
                if (f_hpf > 20.0f) {
                    float r = f / f_hpf;
                    hpf_db = -10.0f * std::log10(1.0f + std::pow(1.0f / std::max(0.001f, r), hpf_order));
                }

                float bell_db = 0.0f;
                if (std::fabs(mfx.bell_gain) > 0.05f) {
                    float f_ratio = f / std::max(1.0f, f_bell) - f_bell / std::max(1.0f, f);
                    bell_db = mfx.bell_gain / (1.0f + bell_q * bell_q * f_ratio * f_ratio);
                }

                float lpf_db = 0.0f;
                if (f_lpf < 20000.0f) {
                    float r = f / f_lpf;
                    lpf_db = -10.0f * std::log10(1.0f + std::pow(r, lpf_order));
                }

                float total_db = hpf_db + bell_db + lpf_db;
                int cur_cy = zero_y - static_cast<int>(std::clamp(total_db * (10.0f / 18.0f), -10.0f, 10.0f));

                if (dx == 0) {
                    cv.fill(eq_x0 + dx, cur_cy, 1, 1, palette::kAccentOrange);
                } else {
                    cv.line(eq_x0 + dx - 1, prev_cy, eq_x0 + dx, cur_cy, palette::kAccentOrange);
                }
                prev_cy = cur_cy;
            }

            cv.text(x_100 - 6, eq_y0 + 2, "100", palette::kTextDim);
            cv.text(x_1k  - 4, eq_y0 + 2, "1K",  palette::kTextDim);
            cv.text(x_10k - 6, eq_y0 + 2, "10K", palette::kTextDim);

            // Праворуч від АЧХ залишаємо повнорозмірні смуги мастера на всю висоту 22px
            widgets::verticalMeter(cv, Rect{mst_x, lower_y + 2, 8, 22}, c.tel.vu_left);
            widgets::verticalMeter(cv, Rect{mst_x + 10, lower_y + 2, 8, 22}, c.tel.vu_right);
        } else {
            // 1. Квадратний міні-осцилограф висотою 22px
            widgets::scope(cv, Rect{area.x + 4, lower_y + 2, 22, 22}, c.tel.scope_samples, palette::kAccentCyan);

            // 2. Компактні індикатори рівнів для ВСІХ активних треків (висота відповідає осцилографу 22px)
            const int act_tracks = c.audio.getActiveRowCount();
            for (int t = 0; t < act_tracks && t < kMaxTracks; ++t) {
                const int mx = area.x + 32 + t * 14;
                if (mx + 10 >= area.x + area.w - 58) break; // Захист від наїзду на FX + Master
                widgets::verticalMeter(cv, Rect{mx, lower_y + 2, 8, 14}, c.tel.track_levels[t]);
                char tb[4];
                std::snprintf(tb, sizeof(tb), "%d", t + 1);
                const bool is_muted = c.audio.getTrackParams(t).muted;
                cv.text(mx + 1, lower_y + 17, tb, is_muted ? palette::kGridDim : palette::kTextDim);
            }

            // 3. Send FX meters (DLY, REV, CHR) directly to the left of Master VU
            const int fx_x = mst_x - 32;
            widgets::verticalMeter(cv, Rect{fx_x, lower_y + 2, 7, 14}, c.tel.send_delay);
            cv.text(fx_x + 1, lower_y + 17, "D", palette::kAccentCyan);

            widgets::verticalMeter(cv, Rect{fx_x + 10, lower_y + 2, 7, 14}, c.tel.send_reverb);
            cv.text(fx_x + 11, lower_y + 17, "R", palette::kAccentPurple);

            widgets::verticalMeter(cv, Rect{fx_x + 20, lower_y + 2, 7, 14}, c.tel.send_chorus);
            cv.text(fx_x + 21, lower_y + 17, "C", palette::kLevelOk);

            // 4. Справа: Master L та R вертикальні міні-VU
            widgets::verticalMeter(cv, Rect{mst_x, lower_y + 2, 8, 14}, c.tel.vu_left);
            widgets::verticalMeter(cv, Rect{mst_x + 10, lower_y + 2, 8, 14}, c.tel.vu_right);
            cv.text(mst_x + 1, lower_y + 17, "MST", palette::kAccentOrange);
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

[[maybe_unused]] const char* voiceLabel(EngineType e, int voice) {
    (void)voice;
    switch (e) {
        case EngineType::kSampleOneShot: return "SMP";
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
    if (c.app.seq_submode == SeqSubmode::kSong) {
        if (dy != 0) {
            c.app.song_cursor_row = std::clamp(c.app.song_cursor_row + dy, 0, 15);
            auto& cur_s = c.app.song_chain[c.app.song_cursor_row];
            c.app.pattern_mgr.switchPatternNow(cur_s.bank, cur_s.pattern, c.audio, c.app.bpm);
        }
        if (dx != 0) {
            if (c.app.song_col == 4) {
                c.app.song_mute_cursor_track = (c.app.song_mute_cursor_track + dx + 16) % 16;
            } else {
                c.app.song_col = (c.app.song_col + dx + 5) % 5;
            }
        }
        return;
    }

    if (c.app.rec_mode) {
        const int t = c.app.selected_track;
        const int max_lanes = 12;
        if (dy != 0) {
            int new_lane = c.app.rec_cursor_lane + dy;
            if (new_lane < 0) {
                // Вгору за межі октави (назва B знаходиться вгорі, lane 0)
                if (c.app.rec_octave < 8) {
                    c.app.rec_octave++;
                    c.app.rec_cursor_lane = 11;
                } else {
                    c.app.rec_cursor_lane = 0;
                }
            } else if (new_lane >= max_lanes) {
                // Вниз за межі октави (назва C знаходиться внизу, lane 11)
                if (c.app.rec_octave > 1) {
                    c.app.rec_octave--;
                    c.app.rec_cursor_lane = 0;
                } else {
                    c.app.rec_cursor_lane = 11;
                }
            } else {
                c.app.rec_cursor_lane = new_lane;
            }
        }
        const int len = static_cast<int>(c.audio.getRowLength(t));
        if (dx != 0 && len > 0) {
            const int jump = coarse ? 4 : 1;
            c.app.cursor_step = ((c.app.cursor_step + dx * jump) % len + len) % len;
        }
        const int vis_steps = static_cast<int>(kStepsPerPage);
        if (c.app.cursor_step < c.app.seq_scroll_step) {
            c.app.seq_scroll_step = c.app.cursor_step;
        } else if (c.app.cursor_step >= c.app.seq_scroll_step + vis_steps) {
            c.app.seq_scroll_step = c.app.cursor_step - vis_steps + 1;
        }
        c.app.seq_scroll_step = std::clamp(c.app.seq_scroll_step, 0, std::max(0, len - vis_steps));
        return;
    }

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
    if (c.app.seq_submode == SeqSubmode::kSong) {
        char b[24];
        std::snprintf(b, sizeof(b), "SONG.S%02d", c.app.song_cursor_row + 1);
        return b;
    }
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
    if (c.app.seq_submode == SeqSubmode::kSong) {
        char hdr[96];
        const auto& cur_step = c.app.song_chain[c.app.song_cursor_row];
        std::snprintf(hdr, sizeof(hdr), "SONG MODE | R%02d: %c%02d x%d LEN:%s BPM:%s | L1: SEQ",
                      c.app.song_cursor_row + 1, 'A' + cur_step.bank, cur_step.pattern + 1, cur_step.repeats,
                      (cur_step.length == 0 ? "PAT" : std::to_string(cur_step.length).c_str()),
                      (cur_step.bpm == 0 ? "PAT" : std::to_string(cur_step.bpm).c_str()));
        cv.text(area.x + 4, area.y + 2, hdr, palette::kLevelWarnClip);

        // Subheader columns
        cv.fill(area.x + 2, area.y + 12, area.w - 4, 9, palette::kHeaderSel);
        cv.text(area.x + 4,   area.y + 13, "ROW", palette::kTextDim);
        cv.text(area.x + 24,  area.y + 13, "PAT", (c.app.song_col == 0) ? palette::kAccentCyan : palette::kTextDim);
        cv.text(area.x + 50,  area.y + 13, "REP", (c.app.song_col == 1) ? palette::kAccentCyan : palette::kTextDim);
        cv.text(area.x + 72,  area.y + 13, "LEN", (c.app.song_col == 2) ? palette::kAccentCyan : palette::kTextDim);
        cv.text(area.x + 98,  area.y + 13, "BPM", (c.app.song_col == 3) ? palette::kAccentCyan : palette::kTextDim);
        cv.text(area.x + 124, area.y + 13, "TRACK MUTES (1..16)", (c.app.song_col == 4) ? palette::kAccentCyan : palette::kTextDim);
        cv.text(area.x + 280, area.y + 13, "STATUS", palette::kTextDim);

        const int start_y = area.y + 23;
        const int row_h = 10;

        for (int i = 0; i < 16; ++i) {
            const int y = start_y + i * row_h;
            const bool is_cursor = (i == c.app.song_cursor_row);
            const bool is_playing = (c.app.transport_playing && i == c.app.song_play_step);
            const auto& step = c.app.song_chain[i];

            if (is_cursor) {
                cv.fill(area.x + 2, y, area.w - 4, row_h - 1, palette::kHeaderSel);
                cv.rect(area.x + 2, y, area.w - 4, row_h - 1, palette::kLevelWarnClip);
            } else if (i % 2 == 1) {
                cv.fill(area.x + 2, y, area.w - 4, row_h - 1, ColorRGB{14, 14, 18});
            }

            // Playback pointer
            if (is_playing) {
                cv.text(area.x + 3, y + 1, ">", palette::kLevelOk);
            }

            // ROW
            char row_str[8];
            std::snprintf(row_str, sizeof(row_str), "%02d", i + 1);
            cv.text(area.x + 8, y + 1, row_str, is_cursor ? palette::kLevelWarnClip : palette::kTextDim);

            // PAT
            char pat_str[8];
            std::snprintf(pat_str, sizeof(pat_str), "%c%02d", 'A' + step.bank, step.pattern + 1);
            cv.text(area.x + 24, y + 1, pat_str, (is_cursor && c.app.song_col == 0) ? palette::kAccentCyan : palette::kTextMain);

            // REP
            char rep_str[8];
            std::snprintf(rep_str, sizeof(rep_str), "x%-2d", step.repeats);
            cv.text(area.x + 50, y + 1, rep_str, (is_cursor && c.app.song_col == 1) ? palette::kAccentCyan : palette::kTextMain);

            // LEN
            char len_str[8];
            if (step.length == 0) std::snprintf(len_str, sizeof(len_str), "PAT ");
            else std::snprintf(len_str, sizeof(len_str), "L%-3d", step.length);
            cv.text(area.x + 72, y + 1, len_str, (is_cursor && c.app.song_col == 2) ? palette::kAccentCyan : palette::kTextMain);

            // BPM
            char bpm_str[8];
            if (step.bpm == 0) std::snprintf(bpm_str, sizeof(bpm_str), "PAT ");
            else std::snprintf(bpm_str, sizeof(bpm_str), "%3d ", step.bpm);
            cv.text(area.x + 98, y + 1, bpm_str, (is_cursor && c.app.song_col == 3) ? palette::kAccentCyan : palette::kTextMain);

            // 16 MUTES
            for (int t = 0; t < 16; ++t) {
                const int mx = area.x + 126 + t * 9;
                const bool is_muted = (step.mute_mask & (1 << t)) != 0;
                const bool is_mute_cur = (is_cursor && c.app.song_col == 4 && t == c.app.song_mute_cursor_track);

                if (is_mute_cur) {
                    cv.rect(mx - 1, y, 8, row_h - 1, palette::kAccentCyan);
                }

                char mc = (t < 9) ? ('1' + t) : ('a' + t - 9);
                char mstr[2] = {mc, '\0'};
                if (is_muted) {
                    cv.text(mx, y + 1, ".", palette::kLevelWarnClip);
                } else {
                    cv.text(mx, y + 1, mstr, palette::kLevelOk);
                }
            }

            // STATUS
            const char* st_txt = step.enabled ? "[PLAY]" : "[SKIP]";
            ColorRGB st_col = step.enabled ? (is_playing ? palette::kLevelOk : palette::kTextMain) : palette::kTextDim;
            cv.text(area.x + 280, y + 1, st_txt, st_col);
        }

        // Help bar at bottom of Song Mode
        const int help_y = area.y + area.h - 11;
        cv.line(area.x + 2, help_y - 2, area.x + area.w - 2, help_y - 2, palette::kGridDim);
        cv.text(area.x + 4, help_y, "D-PAD: NAV/TRK | A: TOGGLE/MUTE | X/B: VAL+/- | SEL+L/R: COL", palette::kTextDim);
        return;
    }

    if (c.app.rec_mode) {
        const int t = c.app.selected_track;
        const auto tp = c.audio.getTrackParams(t);
        const uint32_t len = c.audio.getRowLength(t);
        const uint32_t scroll_off = static_cast<uint32_t>(c.app.seq_scroll_step);
        const int grid_x = area.x + 28;
        const int step_w = 8;
        const int grid_y = area.y + kHeaderH + 2;
        const int len_x = grid_x + static_cast<int>(kStepsPerPage) * step_w + 4;
        const uint32_t play = c.app.transport_playing ? (c.app.current_step % len) : UINT32_MAX;

        // Універсальний 12-півтоновий Piano Roll для всіх доріжок (включаючи Sampler)
        char hdr[96];
        static const char* kSemiNames[12] = {"B ", "A#", "A ", "G#", "G ", "F#", "F ", "E ", "D#", "D ", "C#", "C "};
        static const int kSemiIntervals[12] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
        int cur_l = std::clamp(c.app.rec_cursor_lane, 0, 11);
        int cur_pitch = c.app.rec_octave * 12 + kSemiIntervals[cur_l];
        std::snprintf(hdr, sizeof(hdr), "TRK %d: PIANO ROLL [%s] | OCT %d | NOTE %s%d | L%u",
                      t + 1, getEngineTypeName(tp.engine), c.app.rec_octave, kNoteNames[cur_pitch % 12], cur_pitch / 12 - 1, len);
        cv.text(area.x + 4, area.y + 3, hdr, palette::kAccentCyan);

        const int row_h = std::clamp((area.h - kHeaderH - 4) / 12, 8, 12);
        for (int l = 0; l < 12; ++l) {
            const int y = grid_y + l * row_h;
            const bool is_lane = (l == cur_l);
            if (is_lane) cv.fill(area.x + 2, y, 24, row_h - 1, palette::kHeaderSel);
            int row_pitch = c.app.rec_octave * 12 + kSemiIntervals[l];
            bool is_sharp = (kSemiNames[l][1] == '#');
            char lbl[8];
            std::snprintf(lbl, sizeof(lbl), "%-3s", kSemiNames[l]);
            cv.text(area.x + 4, y + (row_h - 5) / 2, lbl, is_lane ? palette::kAccentCyan : (is_sharp ? palette::kGridDim : palette::kTextDim));

            const int th = row_h - 2;
            for (uint32_t s = 0; s < kStepsPerPage; ++s) {
                const uint32_t idx = scroll_off + s;
                const int x = grid_x + static_cast<int>(s) * step_w;
                if (idx < len) {
                    const bool beat_alt = (((s / 4) + l) % 2 == 1);
                    ColorRGB bg_col = is_sharp ? (beat_alt ? ColorRGB{14, 14, 18} : ColorRGB{10, 10, 14})
                                               : (beat_alt ? ColorRGB{26, 26, 32} : ColorRGB{18, 18, 24});
                    cv.fill(x, y + 1, step_w - 1, th, bg_col);

                    const auto sd = c.audio.getStep(t, static_cast<int>(idx));
                    bool active_note = sd.enabled && (sd.note == row_pitch ||
                                                      sd.chord_notes[0] == row_pitch ||
                                                      sd.chord_notes[1] == row_pitch ||
                                                      sd.chord_notes[2] == row_pitch ||
                                                      sd.chord_notes[3] == row_pitch);
                    if (active_note) {
                        cv.fill(x + 1, y + 1, step_w - 3, th - 1, sd.is_param_trig ? palette::kParamTrig : palette::kAccentCyan);
                    } else {
                        if (s % 4 == 0) cv.fill(x + 2, y + row_h / 2 - 1, 2, 2, palette::kGridDim);
                        else cv.fill(x + 3, y + row_h / 2, 1, 1, ColorRGB{36, 36, 44});
                    }
                    if (idx == play) cv.line(x + 3, y, x + 3, y + row_h - 1, palette::kCursorWhite);
                } else if (idx == len) {
                    cv.line(x, y, x, y + row_h - 1, palette::kAccentOrange);
                } else {
                    const bool beat_alt = (((s / 4) + l) % 2 == 1);
                    cv.fill(x, y + 1, step_w - 1, th, beat_alt ? ColorRGB{10, 10, 14} : ColorRGB{6, 6, 8});
                }
                if (is_lane && idx == static_cast<uint32_t>(c.app.cursor_step)) {
                    cv.rect(x - 1, y, step_w, row_h, palette::kCursorWhite);
                }
            }
            char ltxt[8];
            std::snprintf(ltxt, sizeof(ltxt), "%u", len);
            cv.text(len_x, y + (row_h - 5) / 2, ltxt, is_lane ? palette::kAccentCyan : palette::kGridDim);
        }

        // Червоне повідомлення про вичерпання голосів (макс 4 ноти на крок)
        if (c.now_ms < c.app.voice_limit_alert_until_ms) {
            const int banner_w = 200;
            const int banner_h = 16;
            const int bx = area.x + (area.w - banner_w) / 2;
            const int by = area.y + 24;
            cv.fill(bx, by, banner_w, banner_h, ColorRGB{120, 15, 20});
            cv.rect(bx, by, banner_w, banner_h, palette::kLevelWarnClip);
            cv.text(bx + 10, by + 4, "VOICE LIMIT: MAX 4 VOICES / STEP", palette::kTextMain);
        }
        return;
    }

    const int active = c.audio.getActiveRowCount();
    const bool on_add = c.app.cursor_row >= active;
    const int cur_r = on_add ? active - 1 : c.app.cursor_row;
    const auto tp = c.audio.getTrackParams(c.audio.getRowTrack(cur_r));
    const auto st = c.audio.getStep(cur_r, c.app.cursor_step);
    const uint32_t cur_len = c.audio.getRowLength(cur_r);

    // Заголовок: лише контекст курсора (транспорт/BPM — у HUD, підказки — у HUD)
    char hdr[96];
    if (on_add) {
        std::snprintf(hdr, sizeof(hdr), "[+] ADD TRACK (%d/%d TRACKS ACTIVE) | [A] SELECT ENGINE", active, kTotalRows);
        cv.text(area.x + 4, area.y + 3, hdr, palette::kAccentCyan);
    } else {
        const char* trig = !st.enabled ? "---" : (st.is_param_trig ? "P-LOCK" : "NOTE");
        std::string trk_nm = c.audio.getTrackName(cur_r);
        std::snprintf(hdr, sizeof(hdr), "R%02d [%s: %s] | ST%03d %s%d %s%s | L%u",
                      cur_r + 1, trk_nm.c_str(), getEngineTypeName(tp.engine), c.app.cursor_step + 1,
                      kNoteNames[st.note % 12], st.note / 12 - 1, trig,
                      (st.slide ? " SLD" : ""),
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

        char lbl[8];
        std::string trk_nm = c.audio.getTrackName(t);
        if (trk_nm.length() > 3) trk_nm = trk_nm.substr(0, 3);
        std::snprintf(lbl, sizeof(lbl), "%02d%s", r + 1, trk_nm.c_str());
        const ColorRGB lc = is_cur ? palette::kAccentOrange : (trk.muted ? palette::kGridDim : palette::kTextDim);
        cv.text(area.x + 4, y + (row_h - 5) / 2, lbl, lc);

        const uint32_t play = c.app.transport_playing ? (c.app.current_step % len) : UINT32_MAX;
        const int th = row_h - 3;

        // Визначаємо залишкову тривалість gate length ноти, яка могла початися до поточної сторінки
        int gate_tail_remaining = 0;
        const int scan_start = std::max(0, static_cast<int>(scroll_off) - 128);
        for (int k = scan_start; k < static_cast<int>(scroll_off); ++k) {
            const auto sk = c.audio.getStep(r, k);
            if (sk.enabled) {
                int span = static_cast<int>(sk.gate_len + 0.5f);
                int rem = span - 1 - (static_cast<int>(scroll_off) - 1 - k);
                gate_tail_remaining = std::max(0, rem);
            } else if (gate_tail_remaining > 0) {
                gate_tail_remaining--;
            }
        }

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
                    gate_tail_remaining = std::max(0, static_cast<int>(sd.gate_len + 0.5f) - 1);
                } else if (gate_tail_remaining > 0) {
                    // Ніжний блакитний хвіст тривалості ноти (Gate Length)
                    cv.fill(x + 1, y + 2, step_w - 3, th - 2, ColorRGB{18, 52, 72});
                    cv.fill(x, y + (th / 2), step_w, 2, ColorRGB{45, 115, 150});
                    gate_tail_remaining--;
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
    {"SMP.TRIM_ST", "SMP.TRIM_END", "SMP.DUB_FBK", "SMP.ASSIGN"}
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
    const char* st_name = (st == RecState::kRecording) ? "RECORDING..."
                        : (st == RecState::kOverdub)   ? "OVERDUB (SOUND-ON-SOUND)..."
                        : (st == RecState::kArmed)     ? "ARMED (WAITING)"
                        : (st == RecState::kFinished)  ? "LOOP READY (8 SLICES)"
                        : "STANDBY";
    ColorRGB hdr_col = (st == RecState::kRecording) ? palette::kLevelWarnClip
                     : (st == RecState::kOverdub)   ? palette::kParamTrig
                     : palette::kAccentCyan;
    std::snprintf(hdr, sizeof(hdr), "SAMPLING WORKSTATION [%s]", st_name);
    cv.text(area.x + 4, area.y + 3, hdr, hdr_col);

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

    // 8 Слайсів: вертикальні пунктирні розділювачі та підсвітка активного слайсу
    const int span_px = end_px - st_px;
    const int active_slc = c.app.sampler.activePlaybackSlice();
    if (span_px > 24) {
        for (int i = 0; i < 8; ++i) {
            int s_x = st_px + (i * span_px) / 8;
            int s_next_x = st_px + ((i + 1) * span_px) / 8;
            if (i > 0) {
                for (int py = wf_y + 2; py < wf_y + wf_h - 10; py += 4) {
                    cv.fill(s_x, py, 1, 2, ColorRGB{45, 55, 70});
                }
            }
            if (i == active_slc) {
                cv.fill(s_x + 1, wf_y + 1, std::max(2, s_next_x - s_x - 1), wf_h - 2, ColorRGB{20, 50, 70});
            }
            char slbl[8];
            std::snprintf(slbl, sizeof(slbl), "S%d", i + 1);
            cv.text(s_x + 2, wf_y + wf_h - 9, slbl, (i == active_slc) ? palette::kAccentOrange : ColorRGB{65, 80, 100});
        }
    }

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
    cv.text(std::max(wf_x + 2, end_px - 18), wf_y + 3, "END", palette::kAccentCyan);

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

    auto chord = getChordIntervals(ChordType::kSeventh, scale);
    int base_root = c.app.perf_octave * 12 + root_semi;
    auto getNoteStr = [&](int offset, const char* deg) {
        int n = quantizeToScale(base_root + offset, base_root, scale);
        char b[24];
        constexpr const char* kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        std::snprintf(b, sizeof(b), "%s%d (%s)", kNotes[n % 12], n / 12 - 1, deg);
        return std::string(b);
    };
    std::string n0 = getNoteStr(chord.size() > 0 ? chord[0] : 0, "ROOT");
    std::string n1 = getNoteStr(chord.size() > 1 ? chord[1] : 4, "3RD");
    std::string n2 = getNoteStr(chord.size() > 2 ? chord[2] : 7, "5TH");
    std::string n3 = getNoteStr(chord.size() > 3 ? chord[3] : 11, "7TH");

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
