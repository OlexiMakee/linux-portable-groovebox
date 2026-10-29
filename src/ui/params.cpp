#include "ui/params.hpp"
#include "ui/actions.hpp"
#include "preset_manager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace groovebox {

namespace {

// ---------- Форматери значень (без стану, перевикористовуються будь-яким параметром) ----------
using Fmt = std::string (*)(float);

std::string fmtPct(float v)     { char b[16]; std::snprintf(b, sizeof(b), "%3.0f%%", v * 100.0f); return b; }
std::string fmt127(float v)     { char b[16]; std::snprintf(b, sizeof(b), "%3.0f", v * 127.0f); return b; }
std::string fmtMs(float v)      { char b[16]; std::snprintf(b, sizeof(b), "%4.0f MS", v * 1000.0f); return b; }
std::string fmtPan(float v)     {
    if (std::abs(v) < 0.01f) return "C";
    char b[16]; std::snprintf(b, sizeof(b), "%s%2.0f", v < 0 ? "L" : "R", std::abs(v) * 100.0f); return b;
}
std::string fmtHz(float v)      { char b[16]; std::snprintf(b, sizeof(b), "%4.1f HZ", v * 10.0f); return b; }
std::string fmtBipolarPct(float v) {
    char b[16];
    int p = static_cast<int>(std::round(v * 100.0f));
    std::snprintf(b, sizeof(b), "%+d%%", p);
    return b;
}
std::string fmtLpf(float v) {
    float hz = 20.0f * std::pow(1000.0f, v);
    char b[16];
    if (hz < 1000.0f) std::snprintf(b, sizeof(b), "%.0fHz", hz);
    else std::snprintf(b, sizeof(b), "%.1fkHz", hz / 1000.0f);
    return b;
}
std::string fmtHpf(float v) {
    if (v <= 0.005f) return "OFF";
    float hz = 20.0f * std::pow(800.0f, v);
    char b[16];
    if (hz < 1000.0f) std::snprintf(b, sizeof(b), "%.0fHz", hz);
    else std::snprintf(b, sizeof(b), "%.1fkHz", hz / 1000.0f);
    return b;
}

constexpr const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

std::string noteName(int n) {
    char b[16];
    std::snprintf(b, sizeof(b), "%s%d", kNoteNames[n % 12], n / 12 - 1);
    return b;
}

constexpr int kSelectedTrack = -1;

int trk(const UiContext& c, int t) {
    return std::clamp(t < 0 ? c.app.selected_track : t, 0, kTracksCount - 1);
}

int activeTrackCount(const UiContext& c) {
    return std::max(1, (c.audio.getActiveRowCount() + kVoicesPerTrack - 1) / kVoicesPerTrack);
}

// ---------- Фабрики параметрів ----------
using TrackFloatAcc = float& (*)(TrackParameters&);

Param trackFloat(std::string id, std::string label, int t, TrackFloatAcc acc,
                 float lo, float hi, float step, Fmt fmt) {
    Param p;
    p.id = std::move(id);
    p.label = std::move(label);
    p.value = [=](const UiContext& c) { auto tp = c.audio.getTrackParams(trk(c, t)); return fmt(acc(tp)); };
    p.norm = [=](const UiContext& c) { auto tp = c.audio.getTrackParams(trk(c, t)); return (acc(tp) - lo) / (hi - lo); };
    p.step = [=](UiContext& c, int dir, bool coarse) {
        const int ti = trk(c, t);
        auto tp = c.audio.getTrackParams(ti);
        acc(tp) = std::clamp(acc(tp) + dir * step * (coarse ? 5.0f : 1.0f), lo, hi);
        c.audio.setTrackParams(ti, tp);
    };
    return p;
}

template <typename E>
Param trackEnum(std::string id, std::string label, int t, E& (*acc)(TrackParameters&),
                const char* (*name)(E)) {
    Param p;
    std::string pid = id;
    p.id = std::move(id);
    p.label = std::move(label);
    p.value = [=](const UiContext& c) { auto tp = c.audio.getTrackParams(trk(c, t)); return std::string(name(acc(tp))); };
    p.step = [=](UiContext& c, int dir, bool) {
        if constexpr (std::is_same_v<E, ModTarget>) {
            c.app.mod_picker_param_id = pid;
            const int ti = trk(c, t);
            auto tp = c.audio.getTrackParams(ti);
            c.app.mod_picker_sel = static_cast<int>(acc(tp));
            c.app.mod_picker_open = true;
            c.app.last_action = "TARGET MENU OPEN";
        } else {
            const int ti = trk(c, t);
            auto tp = c.audio.getTrackParams(ti);
            const int n = static_cast<int>(E::kCount);
            acc(tp) = static_cast<E>((static_cast<int>(acc(tp)) + dir + n) % n);
            c.audio.setTrackParams(ti, tp);
        }
    };
    return p;
}

Param trackMute(std::string id, int t) {
    Param p;
    p.id = std::move(id);
    p.label = "MUTE";
    p.value = [=](const UiContext& c) { return std::string(c.audio.getTrackParams(trk(c, t)).muted ? "MUTED" : "ON"); };
    p.step = [=](UiContext& c, int, bool) {
        const int ti = trk(c, t);
        auto tp = c.audio.getTrackParams(ti);
        tp.muted = !tp.muted;
        c.audio.setTrackParams(ti, tp);
    };
    return p;
}

// Параметр движка p[idx]: назва береться з метаданих движка (CUTOFF / TUNE / FM IDX ...)
Param engineParam(int idx) {
    Param p;
    p.id = "TRK.P" + std::to_string(idx + 1);
    p.label = "P" + std::to_string(idx + 1);
    p.dyn_label = [=](const UiContext& c) {
        return std::string(getParamName(c.audio.getTrackParams(trk(c, kSelectedTrack)).engine, idx));
    };
    p.value = [=](const UiContext& c) {
        const auto tp = c.audio.getTrackParams(trk(c, kSelectedTrack));
        if (tp.engine == EngineType::kDx7) {
            if (idx == 0) {
                int p_idx = std::clamp(static_cast<int>(tp.p[0] * (c.audio.dx7().patchCount() - 1) + 0.5f), 0, c.audio.dx7().patchCount() - 1);
                return c.audio.dx7().getPatch(p_idx).name;
            }
            if (idx == 1) {
                char b[16]; std::snprintf(b, sizeof(b), "%d%%", static_cast<int>(tp.p[1] * 100.0f + 0.5f)); return std::string(b);
            }
            if (idx == 4) {
                int a = std::clamp(static_cast<int>(tp.p[4] * 31.99f) + 1, 1, 32);
                return "ALGO " + std::to_string(a);
            }
            if (idx == 5) {
                int fb = std::clamp(static_cast<int>(tp.p[5] * 7.99f), 0, 7);
                return "FB " + std::to_string(fb);
            }
        }
        if ((tp.engine == EngineType::kSampleOneShot && idx == 1) ||
            (tp.engine == EngineType::kSliceSampler && idx == 2)) {
            int st = static_cast<int>(std::round((tp.p[idx] - 0.5f) * 48.0f));
            char b[16]; std::snprintf(b, sizeof(b), "%+d ST", st); return std::string(b);
        }
        return fmt127(tp.p[idx]);
    };
    p.norm = [=](const UiContext& c) { return c.audio.getTrackParams(trk(c, kSelectedTrack)).p[idx]; };
    p.step = [=](UiContext& c, int dir, bool coarse) {
        const int t = trk(c, kSelectedTrack);
        auto tp = c.audio.getTrackParams(t);
        if (tp.engine == EngineType::kDx7) {
            if (idx == 0) {
                int total = c.audio.dx7().patchCount();
                if (total > 0) {
                    int cur = std::clamp(static_cast<int>(tp.p[0] * (total - 1) + 0.5f), 0, total - 1);
                    int next = std::clamp(cur + dir, 0, total - 1);
                    tp.p[0] = static_cast<float>(next) / static_cast<float>(total - 1);
                    c.audio.setTrackParams(t, tp);
                    return;
                }
            } else if (idx == 4) {
                int cur = std::clamp(static_cast<int>(tp.p[4] * 31.0f + 0.5f), 0, 31);
                tp.p[4] = static_cast<float>(std::clamp(cur + dir, 0, 31)) / 31.0f;
                c.audio.setTrackParams(t, tp);
                return;
            } else if (idx == 5) {
                int cur = std::clamp(static_cast<int>(tp.p[5] * 7.0f + 0.5f), 0, 7);
                tp.p[5] = static_cast<float>(std::clamp(cur + dir, 0, 7)) / 7.0f;
                c.audio.setTrackParams(t, tp);
                return;
            }
        } else if ((tp.engine == EngineType::kSampleOneShot && idx == 1) ||
                   (tp.engine == EngineType::kSliceSampler && idx == 2)) {
            int st = std::clamp(static_cast<int>(std::round((tp.p[idx] - 0.5f) * 48.0f)) + dir * (coarse ? 12 : 1), -24, 24);
            tp.p[idx] = (static_cast<float>(st) + 24.0f) / 48.0f;
            c.audio.setTrackParams(t, tp);
            return;
        }
        c.audio.adjustTrackParam(t, idx, dir * (coarse ? 0.10f : 0.02f));
    };
    return p;
}

using AtomAcc = std::atomic<float>& (*)(SynthParams&);

Param globalFloat(std::string id, std::string label, AtomAcc acc, float lo, float hi, float step, Fmt fmt) {
    Param p;
    p.id = std::move(id);
    p.label = std::move(label);
    p.value = [=](const UiContext& c) { return fmt(acc(c.audio.params()).load(std::memory_order_relaxed)); };
    p.norm = [=](const UiContext& c) { return (acc(c.audio.params()).load(std::memory_order_relaxed) - lo) / (hi - lo); };
    p.step = [=](UiContext& c, int dir, bool coarse) {
        auto& a = acc(c.audio.params());
        a.store(std::clamp(a.load() + dir * step * (coarse ? 5.0f : 1.0f), lo, hi), std::memory_order_relaxed);
    };
    return p;
}

Param info(std::string id, std::string label, std::function<std::string(const UiContext&)> value) {
    Param p;
    p.id = std::move(id);
    p.label = std::move(label);
    p.value = std::move(value);
    return p;
}

// Параметри кроку під курсором секвенсора (для INSPECT)
using StepMutator = std::function<void(SequencerStep&, const TrackParameters&, int dir, bool coarse)>;
using StepFormatter = std::function<std::string(const SequencerStep&, const TrackParameters&)>;

Param stepParam(std::string id, std::string label, StepFormatter fmt, StepMutator mut,
                std::function<std::string(const UiContext&)> dyn_lbl = nullptr) {
    Param p;
    p.id = std::move(id);
    p.label = std::move(label);
    if (dyn_lbl) p.dyn_label = std::move(dyn_lbl);
    p.value = [=](const UiContext& c) {
        const auto st = c.audio.getStep(c.app.cursor_row, c.app.cursor_step);
        return fmt(st, c.audio.getTrackParams(c.audio.getRowTrack(c.app.cursor_row)));
    };
    p.step = [=](UiContext& c, int dir, bool coarse) {
        auto st = c.audio.getStep(c.app.cursor_row, c.app.cursor_step);
        mut(st, c.audio.getTrackParams(c.audio.getRowTrack(c.app.cursor_row)), dir, coarse);
        c.audio.setStep(c.app.cursor_row, c.app.cursor_step, st);
    };
    return p;
}

[[maybe_unused]] float lockedOr(float v, float dflt) { return v >= 0.0f ? v : dflt; }
float stepNorm(int dir, bool coarse) { return dir * (coarse ? 8.0f : 1.0f) / 127.0f; }

inline void updateStepHasPlock(SequencerStep& s) {
    bool any = (s.plock_cutoff >= 0.0f) || (s.plock_val >= 0.0f) ||
               (s.attack >= 0.0f) || (s.decay >= 0.0f) || (s.sustain >= 0.0f) || (s.release >= 0.0f) ||
               (s.plock_sample_start >= 0.0f) || (s.plock_sample_end >= 0.0f) || (s.plock_sample_loop >= 0.0f) ||
               (s.plock_delay_send >= 0.0f) || (s.plock_reverb_send >= 0.0f);
    for (float p : s.plock_p) {
        if (p >= 0.0f) any = true;
    }
    s.has_plock = any;
}

inline void stepPlockFloat(float& val, float dflt, int dir, bool coarse, SequencerStep& s) {
    if (dir > 0) {
        if (val < 0.0f) {
            val = std::clamp(dflt + stepNorm(dir, coarse), 0.0f, 1.0f);
        } else {
            val = std::clamp(val + stepNorm(dir, coarse), 0.0f, 1.0f);
        }
    } else if (dir < 0) {
        if (val < 0.0f) {
            val = std::clamp(dflt + stepNorm(dir, coarse), 0.0f, 1.0f);
        } else if (val <= 0.01f) {
            val = -1.0f;
        } else {
            val = std::clamp(val + stepNorm(dir, coarse), 0.0f, 1.0f);
        }
    }
    updateStepHasPlock(s);
}

const char* lfoWaveName(LfoWave w) { return getLfoWaveName(w); }
const char* lfoModeName(LfoMode m) { return getLfoModeName(m); }
const char* destName(StickDest d) { return getStickDestShortName(d); }

} // namespace

ParamRegistry& ParamRegistry::instance() {
    static ParamRegistry reg;
    return reg;
}

const Param* ParamRegistry::find(std::string_view id) const {
    auto it = map_.find(std::string(id));
    return it == map_.end() ? nullptr : &it->second;
}

void ParamRegistry::add(Param p) {
    std::string key = p.id;
    map_[key] = std::move(p);
}

std::vector<std::string> ParamRegistry::ids() const {
    std::vector<std::string> out;
    out.reserve(map_.size());
    for (const auto& kv : map_) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

ParamRegistry::ParamRegistry() {
    const int S = kSelectedTrack;

    // ===== TRK.* — інструмент обраного треку =====
    {
        Param p;
        p.id = "TRK.ENGINE";
        p.label = "ENGINE";
        p.value = [](const UiContext& c) { return std::string(getEngineTypeName(c.audio.getTrackParams(trk(c, -1)).engine)); };
        p.step = [](UiContext& c, int dir, bool) { c.audio.cycleTrackEngine(trk(c, -1), dir); };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "TRK.PRESET";
        p.label = "PRESET";
        p.value = [](const UiContext& c) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            auto presets = PresetManager::instance().getFactoryPresets(tp.engine);
            if (presets.empty()) return std::string("--- [INIT]");
            int idx = std::clamp(tp.preset_idx, 0, static_cast<int>(presets.size()) - 1);
            return std::string(presets[idx].name);
        };
        p.norm = [](const UiContext& c) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            auto presets = PresetManager::instance().getFactoryPresets(tp.engine);
            if (presets.size() <= 1) return 0.0f;
            return static_cast<float>(tp.preset_idx) / static_cast<float>(presets.size() - 1);
        };
        p.step = [](UiContext& c, int dir, bool) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            auto presets = PresetManager::instance().getFactoryPresets(tp.engine);
            if (presets.empty()) return;
            const int n = static_cast<int>(presets.size());
            tp.preset_idx = (tp.preset_idx + dir + n) % n;
            PresetManager::instance().applyPreset(tp, presets[tp.preset_idx]);
            c.audio.setTrackParams(t, tp);
            c.app.last_action = std::string("PRESET: ") + presets[tp.preset_idx].name;
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "TRK.GAIN_OVER";
        p.label = "GAIN";
        p.value = [](const UiContext& c) {
            float g = c.audio.getTrackParams(trk(c, -1)).gain_over;
            char b[24];
            if (std::fabs(g) < 0.005f) {
                std::snprintf(b, sizeof(b), "0.0 dB [CLN]");
            } else if (g < 0.0f) {
                float db = g * 24.0f;
                std::snprintf(b, sizeof(b), "%+4.1f dB", db);
            } else {
                float db = g * 24.0f;
                std::snprintf(b, sizeof(b), "%+4.1f dB [CRUNCH]", db);
            }
            return std::string(b);
        };
        p.norm = [](const UiContext& c) {
            float g = c.audio.getTrackParams(trk(c, -1)).gain_over;
            return (g + 1.0f) * 0.5f;
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            float step = coarse ? 0.10f : 0.02f;
            tp.gain_over = std::clamp(tp.gain_over + dir * step, -1.0f, 1.0f);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "TRK.RND";
        p.label = "RANDOM";
        p.value = [](const UiContext&) { return "DICE [A]"; };
        p.step = [](UiContext& c, int, bool) {
            ActionRegistry::instance().run("SYNTH.RANDOMIZE", c);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "TRK.WAVE_SAMPLE";
        p.label = "SAMPLE";
        p.value = [](const UiContext& c) -> std::string {
            const auto tp = c.audio.getTrackParams(trk(c, -1));
            if (tp.engine == EngineType::kSampleOneShot || tp.engine == EngineType::kSliceSampler) {
                int t = trk(c, -1);
                const auto& buf = c.audio.sampler().trackSample(t);
                if (buf.loaded && !buf.name.empty() && buf.name != "EMPTY") return buf.name;
                return c.audio.getTrackName(t);
            }
            return "---";
        };
        p.norm = [](const UiContext& c) {
            const auto tp = c.audio.getTrackParams(trk(c, -1));
            return (tp.engine == EngineType::kSampleOneShot || tp.engine == EngineType::kSliceSampler) ? tp.p[0] : 0.0f;
        };
        p.step = [](UiContext& c, int, bool) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            if (tp.engine == EngineType::kSampleOneShot || tp.engine == EngineType::kSliceSampler) {
                c.app.sample_browser_target_track = t;
                ActionRegistry::instance().run("SAMPLE_BROWSER.OPEN", c);
            }
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.PLAY_MODE";
        p.label = "MODE";
        p.value = [](const UiContext& c) {
            auto tp = c.audio.getTrackParams(trk(c, -1));
            return std::string(getSampleEngineModeName(tp.sample.play_mode));
        };
        p.norm = [](const UiContext& c) {
            auto tp = c.audio.getTrackParams(trk(c, -1));
            return (tp.sample.play_mode == SampleEngineMode::kSlicer) ? 1.0f : 0.0f;
        };
        p.step = [](UiContext& c, int, bool) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            tp.sample.play_mode = (tp.sample.play_mode == SampleEngineMode::kOneShot)
                                      ? SampleEngineMode::kSlicer
                                      : SampleEngineMode::kOneShot;
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.SLICE";
        p.label = "SLICE";
        p.value = [](const UiContext& c) {
            auto tp = c.audio.getTrackParams(trk(c, -1));
            char b[16];
            std::snprintf(b, sizeof(b), "SL %d/16", tp.sample.slice_index + 1);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) {
            auto tp = c.audio.getTrackParams(trk(c, -1));
            return std::clamp(static_cast<float>(tp.sample.slice_index) / 15.0f, 0.0f, 1.0f);
        };
        p.step = [](UiContext& c, int dir, bool) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            tp.sample.slice_index = std::clamp(tp.sample.slice_index + dir, 0, 15);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    {
        // Digitakt-style sampler parameters (SMP.LOOP, SMP.START, SMP.LOOP_PT, SMP.END, SMP.TUNE)
        Param p;
        p.id = "SMP.LOOP";
        p.label = "LOOP";
        p.value = [](const UiContext& c) {
            return std::string(getSampleLoopModeName(c.audio.getTrackParams(trk(c, -1)).sample.loop_mode));
        };
        p.norm = [](const UiContext& c) {
            return static_cast<int>(c.audio.getTrackParams(trk(c, -1)).sample.loop_mode) / 3.0f;
        };
        p.step = [](UiContext& c, int dir, bool) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            int m = (static_cast<int>(tp.sample.loop_mode) + dir + 4) % 4;
            tp.sample.loop_mode = static_cast<SampleLoopMode>(m);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    {
        auto fmtTuneSemi = [](float v) -> std::string {
            char b[16];
            if (std::abs(v) < 0.05f) return "0 semi";
            std::snprintf(b, sizeof(b), "%+d semi", static_cast<int>(std::round(v)));
            return std::string(b);
        };
        add(trackFloat("SMP.START", "START", -1, [](TrackParameters& t) -> float& { return t.sample.start; }, 0.0f, 0.99f, 0.01f, fmtPct));
        add(trackFloat("SMP.LOOP_PT", "LOOP PT", -1, [](TrackParameters& t) -> float& { return t.sample.loop_point; }, 0.0f, 0.99f, 0.01f, fmtPct));
        add(trackFloat("SMP.END", "END", -1, [](TrackParameters& t) -> float& { return t.sample.end; }, 0.01f, 1.0f, 0.01f, fmtPct));
        add(trackFloat("SMP.TUNE", "TUNE", -1, [](TrackParameters& t) -> float& { return t.sample.tune; }, -24.0f, 24.0f, 1.0f, fmtTuneSemi));
    }
    {
        Param p;
        p.id = "TRK.SEL";
        p.label = "TRACK";
        p.value = [](const UiContext& c) {
            char b[16];
            std::snprintf(b, sizeof(b), "T%d/%d", c.app.selected_track + 1, activeTrackCount(c));
            return std::string(b);
        };
        p.step = [](UiContext& c, int dir, bool) {
            const int n = activeTrackCount(c);
            c.app.selected_track = (c.app.selected_track + dir + n) % n;
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "TRK.TRANSP";
        p.label = "TRANSP";
        p.value = [](const UiContext& c) {
            char b[16];
            std::snprintf(b, sizeof(b), "%+d ST", c.audio.getTrackParams(trk(c, -1)).transpose);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) { return (c.audio.getTrackParams(trk(c, -1)).transpose + 24) / 48.0f; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            tp.transpose = std::clamp(tp.transpose + dir * (coarse ? 12 : 1), -24, 24);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    add(trackFloat("TRK.GLIDE", "GLIDE", S, [](TrackParameters& t) -> float& { return t.glide; }, 0.005f, 0.5f, 0.005f, fmtMs));
    for (int i = 0; i < kParamsPerTrack; ++i) add(engineParam(i));
    add(trackFloat("TRK.ATK", "ATTACK", S, [](TrackParameters& t) -> float& { return t.attack; }, 0.001f, 0.8f, 0.01f, fmtMs));
    add(trackFloat("TRK.DEC", "DECAY", S, [](TrackParameters& t) -> float& { return t.decay; }, 0.01f, 1.5f, 0.02f, fmtMs));
    add(trackFloat("TRK.SUS", "SUSTAIN", S, [](TrackParameters& t) -> float& { return t.sustain; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(trackFloat("TRK.REL", "RELEASE", S, [](TrackParameters& t) -> float& { return t.release; }, 0.01f, 1.8f, 0.02f, fmtMs));

#define DEFINE_LFO_PARAMS(N, lfo_member) \
    add(trackEnum<LfoWave>("LFO" #N ".WAVE", "L" #N " WAVE", S, [](TrackParameters& t) -> LfoWave& { return t.lfo_member.wave; }, lfoWaveName)); \
    { \
        Param p; \
        p.id = "LFO" #N ".RATE"; \
        p.label = "L" #N " RATE"; \
        p.value = [=](const UiContext& c) { \
            auto tp = c.audio.getTrackParams(trk(c, S)); \
            const auto& l = tp.lfo_member; \
            int s = 1 + static_cast<int>(l.rate * 127.0f + 0.5f); \
            uint8_t m = l.multiplier > 0 ? l.multiplier : 1; \
            float hz = (0.05f + l.rate * 19.95f) * static_cast<float>(m); \
            char b[24]; \
            if (hz >= 10.0f) std::snprintf(b, sizeof(b), "%d (%.0fHz)", s, hz); \
            else std::snprintf(b, sizeof(b), "%d (%.1fHz)", s, hz); \
            return std::string(b); \
        }; \
        p.norm = [=](const UiContext& c) { return c.audio.getTrackParams(trk(c, S)).lfo_member.rate; }; \
        p.step = [=](UiContext& c, int dir, bool coarse) { \
            int t = trk(c, S); \
            auto tp = c.audio.getTrackParams(t); \
            tp.lfo_member.rate = std::clamp(tp.lfo_member.rate + dir * (coarse ? 0.08f : 0.01f), 0.0f, 1.0f); \
            c.audio.setTrackParams(t, tp); \
        }; \
        add(std::move(p)); \
    } \
    { \
        Param p; \
        p.id = "LFO" #N ".MULT"; \
        p.label = "L" #N " MULT"; \
        p.value = [=](const UiContext& c) { \
            uint8_t m = c.audio.getTrackParams(trk(c, S)).lfo_member.multiplier; \
            switch (m) { \
                case 2:  return "x2"; \
                case 4:  return "x4"; \
                case 8:  return "x8"; \
                case 16: return "x16"; \
                case 32: return "x32"; \
                case 64: return "x64"; \
                default: return "x1"; \
            } \
        }; \
        p.norm = [=](const UiContext& c) { return static_cast<float>(c.audio.getTrackParams(trk(c, S)).lfo_member.multiplier) / 64.0f; }; \
        p.step = [=](UiContext& c, int dir, bool) { \
            int t = trk(c, S); \
            auto tp = c.audio.getTrackParams(t); \
            static constexpr uint8_t kM[] = {1, 2, 4, 8, 16, 32, 64}; \
            int idx = 0; \
            for (int i = 0; i < 7; ++i) if (kM[i] == tp.lfo_member.multiplier) { idx = i; break; } \
            idx = std::clamp(idx + dir, 0, 6); \
            tp.lfo_member.multiplier = kM[idx]; \
            c.audio.setTrackParams(t, tp); \
        }; \
        add(std::move(p)); \
    } \
    add(trackFloat("LFO" #N ".DEPTH", "L" #N " DEPTH", S, [](TrackParameters& t) -> float& { return t.lfo_member.depth; }, 0.0f, 1.0f, 0.02f, fmtPct)); \
    add(trackEnum<ModTarget>("LFO" #N ".DEST", "L" #N " DEST", S, [](TrackParameters& t) -> ModTarget& { return t.lfo_member.dest; }, destName)); \
    add(trackEnum<LfoMode>("LFO" #N ".MODE", "L" #N " MODE", S, [](TrackParameters& t) -> LfoMode& { return t.lfo_member.mode; }, lfoModeName));

    DEFINE_LFO_PARAMS(1, lfo1);
    DEFINE_LFO_PARAMS(2, lfo2);
    DEFINE_LFO_PARAMS(3, lfo3);
    DEFINE_LFO_PARAMS(4, lfo4);
#undef DEFINE_LFO_PARAMS

#define DEFINE_ENV_PARAMS(N, env_member) \
    add(trackFloat("ENV" #N ".ATK", "E" #N " ATK", S, [](TrackParameters& t) -> float& { return t.env_member.attack; }, 0.001f, 0.8f, 0.01f, fmtMs)); \
    add(trackFloat("ENV" #N ".DEC", "E" #N " DEC", S, [](TrackParameters& t) -> float& { return t.env_member.decay; }, 0.01f, 2.5f, 0.02f, fmtMs)); \
    add(trackFloat("ENV" #N ".SUS", "E" #N " SUS", S, [](TrackParameters& t) -> float& { return t.env_member.sustain; }, 0.0f, 1.0f, 0.02f, fmtPct)); \
    add(trackFloat("ENV" #N ".REL", "E" #N " REL", S, [](TrackParameters& t) -> float& { return t.env_member.release; }, 0.01f, 2.5f, 0.02f, fmtMs)); \
    add(trackFloat("ENV" #N ".AMT", "E" #N " AMT", S, [](TrackParameters& t) -> float& { return t.env_member.amount; }, -1.0f, 1.0f, 0.05f, fmtBipolarPct)); \
    add(trackEnum<ModTarget>("ENV" #N ".DEST", "E" #N " DEST", S, [](TrackParameters& t) -> ModTarget& { return t.env_member.dest; }, destName));

    DEFINE_ENV_PARAMS(1, env1);
    DEFINE_ENV_PARAMS(2, env2);
#undef DEFINE_ENV_PARAMS

    // Аналогові стіки (Mode 1 & Mode 2 для лівого та правого стіків)
    add(trackEnum<ModTarget>("STK.L1_X", "L1 X-TGT", S, [](TrackParameters& t) -> ModTarget& { return t.stick_l_m1.dest_x; }, destName));
    add(trackFloat("STK.L1_XAMT", "L1 X-AMT", S, [](TrackParameters& t) -> float& { return t.stick_l_m1.amount_x; }, 0.0f, 1.0f, 0.05f, fmtPct));
    add(trackEnum<ModTarget>("STK.L1_Y", "L1 Y-TGT", S, [](TrackParameters& t) -> ModTarget& { return t.stick_l_m1.dest_y; }, destName));
    add(trackFloat("STK.L1_YAMT", "L1 Y-AMT", S, [](TrackParameters& t) -> float& { return t.stick_l_m1.amount_y; }, 0.0f, 1.0f, 0.05f, fmtPct));

    add(trackEnum<ModTarget>("STK.L2_X", "L2 X-TGT", S, [](TrackParameters& t) -> ModTarget& { return t.stick_l_m2.dest_x; }, destName));
    add(trackFloat("STK.L2_XAMT", "L2 X-AMT", S, [](TrackParameters& t) -> float& { return t.stick_l_m2.amount_x; }, 0.0f, 1.0f, 0.05f, fmtPct));
    add(trackEnum<ModTarget>("STK.L2_Y", "L2 Y-TGT", S, [](TrackParameters& t) -> ModTarget& { return t.stick_l_m2.dest_y; }, destName));
    add(trackFloat("STK.L2_YAMT", "L2 Y-AMT", S, [](TrackParameters& t) -> float& { return t.stick_l_m2.amount_y; }, 0.0f, 1.0f, 0.05f, fmtPct));

    add(trackEnum<ModTarget>("STK.R1_X", "R1 X-TGT", S, [](TrackParameters& t) -> ModTarget& { return t.stick_r_m1.dest_x; }, destName));
    add(trackFloat("STK.R1_XAMT", "R1 X-AMT", S, [](TrackParameters& t) -> float& { return t.stick_r_m1.amount_x; }, 0.0f, 1.0f, 0.05f, fmtPct));
    add(trackEnum<ModTarget>("STK.R1_Y", "R1 Y-TGT", S, [](TrackParameters& t) -> ModTarget& { return t.stick_r_m1.dest_y; }, destName));
    add(trackFloat("STK.R1_YAMT", "R1 Y-AMT", S, [](TrackParameters& t) -> float& { return t.stick_r_m1.amount_y; }, 0.0f, 1.0f, 0.05f, fmtPct));

    add(trackEnum<ModTarget>("STK.R2_X", "R2 X-TGT", S, [](TrackParameters& t) -> ModTarget& { return t.stick_r_m2.dest_x; }, destName));
    add(trackFloat("STK.R2_XAMT", "R2 X-AMT", S, [](TrackParameters& t) -> float& { return t.stick_r_m2.amount_x; }, 0.0f, 1.0f, 0.05f, fmtPct));
    add(trackEnum<ModTarget>("STK.R2_Y", "R2 Y-TGT", S, [](TrackParameters& t) -> ModTarget& { return t.stick_r_m2.dest_y; }, destName));
    add(trackFloat("STK.R2_YAMT", "R2 Y-AMT", S, [](TrackParameters& t) -> float& { return t.stick_r_m2.amount_y; }, 0.0f, 1.0f, 0.05f, fmtPct));

    // Сумісність для існуючих шорткатів
    add(trackEnum<ModTarget>("STICK.LX", "L-STK X", S, [](TrackParameters& t) -> ModTarget& { return t.stick_left_x; }, destName));
    add(trackEnum<ModTarget>("STICK.LY", "L-STK Y", S, [](TrackParameters& t) -> ModTarget& { return t.stick_left_y; }, destName));
    add(trackEnum<ModTarget>("STICK.RX", "R-STK X", S, [](TrackParameters& t) -> ModTarget& { return t.stick_right_x; }, destName));
    add(trackEnum<ModTarget>("STICK.RY", "R-STK Y", S, [](TrackParameters& t) -> ModTarget& { return t.stick_right_y; }, destName));

    // ===== TRK.* (обраний трек) та T1..T4.* (фіксований трек для MIX) — фільтри, рівні та посили =====
    for (int t = -1; t < kTracksCount; ++t) {
        const std::string pre = (t < 0) ? "TRK." : ("T" + std::to_string(t + 1) + ".");
        add(trackFloat(pre + "LPF_CUT", "LP CUT", t, [](TrackParameters& x) -> float& { return x.lpf_cutoff; }, 0.02f, 0.98f, 0.02f, fmtLpf));
        add(trackFloat(pre + "LPF_RES", "LP RES", t, [](TrackParameters& x) -> float& { return x.lpf_resonance; }, 0.0f, 0.95f, 0.02f, fmtPct));
        {
            Param p;
            p.id = pre + "LPF_POLE";
            p.label = "LP SLP";
            p.value = [t](const UiContext& c) {
                int tr = trk(c, t);
                return c.audio.getTrackParams(tr).lpf_24db ? std::string("24 DB") : std::string("12 DB");
            };
            p.norm = [t](const UiContext& c) {
                int tr = trk(c, t);
                return c.audio.getTrackParams(tr).lpf_24db ? 1.0f : 0.0f;
            };
            p.step = [t](UiContext& c, int, bool) {
                int tr = trk(c, t);
                auto tp = c.audio.getTrackParams(tr);
                tp.lpf_24db = !tp.lpf_24db;
                c.audio.setTrackParams(tr, tp);
            };
            add(std::move(p));
        }
        add(trackFloat(pre + "LPF_ENV", "LP ENV", t, [](TrackParameters& x) -> float& { return x.lpf_env; }, -1.0f, 1.0f, 0.05f, fmtBipolarPct));
        add(trackFloat(pre + "LPF_DEC", "LP DEC", t, [](TrackParameters& x) -> float& { return x.lpf_decay; }, 0.01f, 2.5f, 0.05f, fmtMs));

        add(trackFloat(pre + "HPF_CUT", "HP CUT", t, [](TrackParameters& x) -> float& { return x.hpf_cutoff; }, 0.0f, 0.98f, 0.02f, fmtHpf));
        add(trackFloat(pre + "HPF_RES", "HP RES", t, [](TrackParameters& x) -> float& { return x.hpf_resonance; }, 0.0f, 0.95f, 0.02f, fmtPct));
        {
            Param p;
            p.id = pre + "HPF_POLE";
            p.label = "HP SLP";
            p.value = [t](const UiContext& c) {
                int tr = trk(c, t);
                return c.audio.getTrackParams(tr).hpf_24db ? std::string("24 DB") : std::string("12 DB");
            };
            p.norm = [t](const UiContext& c) {
                int tr = trk(c, t);
                return c.audio.getTrackParams(tr).hpf_24db ? 1.0f : 0.0f;
            };
            p.step = [t](UiContext& c, int, bool) {
                int tr = trk(c, t);
                auto tp = c.audio.getTrackParams(tr);
                tp.hpf_24db = !tp.hpf_24db;
                c.audio.setTrackParams(tr, tp);
            };
            add(std::move(p));
        }
        add(trackFloat(pre + "HPF_ENV", "HP ENV", t, [](TrackParameters& x) -> float& { return x.hpf_env; }, -1.0f, 1.0f, 0.05f, fmtBipolarPct));
        add(trackFloat(pre + "HPF_DEC", "HP DEC", t, [](TrackParameters& x) -> float& { return x.hpf_decay; }, 0.01f, 2.5f, 0.05f, fmtMs));

        add(trackFloat(pre + "HPF", "HP CUT", t, [](TrackParameters& x) -> float& { return x.hpf_cutoff; }, 0.0f, 0.98f, 0.02f, fmtHpf));
        add(trackFloat(pre + "LVL", "LEVEL", t, [](TrackParameters& x) -> float& { return x.level; }, 0.0f, 1.0f, 0.02f, fmtPct));
        add(trackFloat(pre + "PAN", "PAN", t, [](TrackParameters& x) -> float& { return x.pan; }, -1.0f, 1.0f, 0.05f, fmtPan));
        add(trackFloat(pre + "DLY", "DLY SND", t, [](TrackParameters& x) -> float& { return x.delay_send; }, 0.0f, 1.0f, 0.02f, fmtPct));
        add(trackFloat(pre + "REV", "REV SND", t, [](TrackParameters& x) -> float& { return x.reverb_send; }, 0.0f, 1.0f, 0.02f, fmtPct));
        add(trackFloat(pre + "CHR", "CHR SND", t, [](TrackParameters& x) -> float& { return x.chorus_send; }, 0.0f, 1.0f, 0.02f, fmtPct));
        add(trackMute(pre + "MUTE", t));
    }

    // ===== TRACK INSERT FX (Bitcrusher, SRR, Distortion, Chorus, Delay, Reverb, Sidechain) =====
    {
        Param p;
        p.id = "FX.BC_BITS";
        p.label = "BITS";
        p.value = [](const UiContext& c) {
            auto tp = c.audio.getTrackParams(trk(c, S));
            if (tp.fx.bit_depth >= 16) return std::string("16 (OFF)");
            char b[16]; std::snprintf(b, sizeof(b), "%d BITS", tp.fx.bit_depth);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) {
            return (c.audio.getTrackParams(trk(c, S)).fx.bit_depth - 1) / 15.0f;
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            const int t = trk(c, S);
            auto tp = c.audio.getTrackParams(t);
            tp.fx.bit_depth = std::clamp(tp.fx.bit_depth + dir * (coarse ? 4 : 1), 1, 16);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "FX.SRR";
        p.label = "SRR";
        p.value = [](const UiContext& c) {
            auto tp = c.audio.getTrackParams(trk(c, S));
            if (tp.fx.srr_factor <= 1) return std::string("1X (OFF)");
            char b[16]; std::snprintf(b, sizeof(b), "%dX", tp.fx.srr_factor);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) {
            return (c.audio.getTrackParams(trk(c, S)).fx.srr_factor - 1) / 63.0f;
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            const int t = trk(c, S);
            auto tp = c.audio.getTrackParams(t);
            tp.fx.srr_factor = std::clamp(tp.fx.srr_factor + dir * (coarse ? 8 : 1), 1, 64);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    add(trackFloat("FX.DST_DRV", "DRIVE", S, [](TrackParameters& t) -> float& { return t.fx.dist_drive; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(trackFloat("FX.DST_TONE", "TONE", S, [](TrackParameters& t) -> float& { return t.fx.dist_tone; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(trackFloat("FX.DST_MIX", "D.MIX", S, [](TrackParameters& t) -> float& { return t.fx.dist_mix; }, 0.0f, 1.0f, 0.02f, fmtPct));

    add(trackFloat("FX.CHR_MIX", "C.MIX", S, [](TrackParameters& t) -> float& { return t.fx.chorus_mix; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(trackFloat("FX.CHR_RATE", "C.RATE", S, [](TrackParameters& t) -> float& { return t.fx.chorus_rate; }, 0.1f, 8.0f, 0.1f, fmtHz));
    add(trackFloat("FX.CHR_DEP", "C.DEP", S, [](TrackParameters& t) -> float& { return t.fx.chorus_depth; }, 0.0f, 1.0f, 0.02f, fmtPct));

    {
        Param p;
        p.id = "FX.DLY_DIV";
        p.label = "TIME";
        p.value = [](const UiContext& c) {
            int d = std::clamp(c.audio.getTrackParams(trk(c, S)).fx.delay_div, 0, 7);
            return std::string(kDelayDivNames[d]);
        };
        p.norm = [](const UiContext& c) {
            return std::clamp(c.audio.getTrackParams(trk(c, S)).fx.delay_div, 0, 7) / 7.0f;
        };
        p.step = [](UiContext& c, int dir, bool) {
            const int t = trk(c, S);
            auto tp = c.audio.getTrackParams(t);
            tp.fx.delay_div = std::clamp(tp.fx.delay_div + dir, 0, 7);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    add(trackFloat("FX.DLY_FBK", "D.FBK", S, [](TrackParameters& t) -> float& { return t.fx.delay_feedback; }, 0.0f, 0.95f, 0.02f, fmtPct));
    auto fmtDelayWidth = [](float v) -> std::string {
        int pct = static_cast<int>(std::round(v * 100.0f));
        if (std::abs(pct) <= 2) return "MONO";
        if (pct > 0) {
            char b[16];
            std::snprintf(b, sizeof(b), "PING %d%%", pct);
            return std::string(b);
        } else {
            char b[16];
            std::snprintf(b, sizeof(b), "PONG %d%%", -pct);
            return std::string(b);
        }
    };
    add(trackFloat("FX.DLY_WID", "STEREO", S, [](TrackParameters& t) -> float& { return t.fx.delay_width; }, -1.0f, 1.0f, 0.05f, fmtDelayWidth));
    add(trackFloat("FX.DLY_MIX", "D.MIX", S, [](TrackParameters& t) -> float& { return t.fx.delay_mix; }, 0.0f, 1.0f, 0.02f, fmtPct));

    {
        Param p;
        p.id = "FX.REV_ENG";
        p.label = "ENGINE";
        p.value = [](const UiContext& c) {
            return std::string(getReverbEngineName(c.audio.getTrackParams(trk(c, S)).fx.rev_engine));
        };
        p.norm = [](const UiContext& c) {
            return static_cast<int>(c.audio.getTrackParams(trk(c, S)).fx.rev_engine) / 3.0f;
        };
        p.step = [](UiContext& c, int dir, bool) {
            const int t = trk(c, S);
            auto tp = c.audio.getTrackParams(t);
            int e = (static_cast<int>(tp.fx.rev_engine) + dir + 4) % 4;
            tp.fx.rev_engine = static_cast<ReverbEngine>(e);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    add(trackFloat("FX.REV_DEC", "R.DEC", S, [](TrackParameters& t) -> float& { return t.fx.rev_decay; }, 0.05f, 0.98f, 0.02f, fmtPct));
    add(trackFloat("FX.REV_HICUT", "HI-CUT", S, [](TrackParameters& t) -> float& { return t.fx.rev_hicut; }, 0.05f, 1.0f, 0.02f, fmtPct));
    add(trackFloat("FX.REV_LOCUT", "LO-CUT", S, [](TrackParameters& t) -> float& { return t.fx.rev_locut; }, 0.0f, 0.8f, 0.02f, fmtPct));
    add(trackFloat("FX.REV_PRE", "PRE-DLY", S, [](TrackParameters& t) -> float& { return t.fx.rev_predelay; }, 0.0f, 0.15f, 0.005f, fmtMs));
    add(trackFloat("FX.REV_MIX", "R.MIX", S, [](TrackParameters& t) -> float& { return t.fx.rev_mix; }, 0.0f, 1.0f, 0.02f, fmtPct));

    {
        Param p;
        p.id = "FX.SC_SRC";
        p.label = "SC SRC";
        p.value = [](const UiContext& c) {
            int src = c.audio.getTrackParams(trk(c, S)).fx.sc_source;
            if (src <= 0) return std::string("OFF");
            char b[16]; std::snprintf(b, sizeof(b), "TRK %02d", src);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) {
            return c.audio.getTrackParams(trk(c, S)).fx.sc_source / 16.0f;
        };
        p.step = [](UiContext& c, int dir, bool) {
            const int t = trk(c, S);
            auto tp = c.audio.getTrackParams(t);
            int max_src = c.audio.getActiveRowCount();
            tp.fx.sc_source = std::clamp(tp.fx.sc_source + dir, 0, max_src);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "FX.SC_THR";
        p.label = "SC THR";
        p.value = [](const UiContext& c) {
            float th = c.audio.getTrackParams(trk(c, S)).fx.sc_thresh;
            char b[16]; std::snprintf(b, sizeof(b), "%+.1f dB", th);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) {
            return (c.audio.getTrackParams(trk(c, S)).fx.sc_thresh + 48.0f) / 48.0f;
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            const int t = trk(c, S);
            auto tp = c.audio.getTrackParams(t);
            tp.fx.sc_thresh = std::clamp(tp.fx.sc_thresh + dir * (coarse ? 6.0f : 1.0f), -48.0f, 0.0f);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "FX.SC_RAT";
        p.label = "SC RAT";
        p.value = [](const UiContext& c) {
            float r = c.audio.getTrackParams(trk(c, S)).fx.sc_ratio;
            char b[16]; std::snprintf(b, sizeof(b), "%.1f:1", r);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) {
            return (c.audio.getTrackParams(trk(c, S)).fx.sc_ratio - 1.0f) / 19.0f;
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            const int t = trk(c, S);
            auto tp = c.audio.getTrackParams(t);
            tp.fx.sc_ratio = std::clamp(tp.fx.sc_ratio + dir * (coarse ? 2.0f : 0.5f), 1.0f, 20.0f);
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }
    add(trackFloat("FX.SC_ATK", "SC ATK", S, [](TrackParameters& t) -> float& { return t.fx.sc_attack; }, 0.001f, 0.100f, 0.002f, fmtMs));
    add(trackFloat("FX.SC_REL", "SC REL", S, [](TrackParameters& t) -> float& { return t.fx.sc_release; }, 0.020f, 1.000f, 0.020f, fmtMs));
    {
        Param p;
        p.id = "FX.SC_POS";
        p.label = "SC POS";
        p.value = [](const UiContext& c) {
            return std::string(getSidechainPosName(c.audio.getTrackParams(trk(c, S)).fx.sc_pos));
        };
        p.norm = [](const UiContext& c) {
            return static_cast<int>(c.audio.getTrackParams(trk(c, S)).fx.sc_pos);
        };
        p.step = [](UiContext& c, int, bool) {
            const int t = trk(c, S);
            auto tp = c.audio.getTrackParams(t);
            tp.fx.sc_pos = (tp.fx.sc_pos == SidechainPos::kPreDelayRev) ? SidechainPos::kPostDelayRev : SidechainPos::kPreDelayRev;
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
    }

    // ===== Глобальні ефекти та майстер =====
    add(globalFloat("DLY.FBK", "DLY FBK", [](SynthParams& s) -> std::atomic<float>& { return s.delay_feedback; }, 0.0f, 0.95f, 0.02f, fmtPct));
    add(globalFloat("DLY.RET", "DLY RET", [](SynthParams& s) -> std::atomic<float>& { return s.delay_mix; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(globalFloat("REV.DECAY", "REV DEC", [](SynthParams& s) -> std::atomic<float>& { return s.reverb_decay; }, 0.0f, 0.95f, 0.02f, fmtPct));
    add(globalFloat("REV.RET", "REV RET", [](SynthParams& s) -> std::atomic<float>& { return s.reverb_mix; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(globalFloat("MST.CHR_MIX", "CHR MIX", [](SynthParams& s) -> std::atomic<float>& { return s.chorus_mix; }, 0.0f, 1.0f, 0.05f, fmtPct));
    add(globalFloat("MST.CHR_RATE", "CHR RATE", [](SynthParams& s) -> std::atomic<float>& { return s.chorus_rate; }, 0.1f, 5.0f, 0.1f, fmtHz));
    add(globalFloat("MST.CHR_DEP", "CHR DEP", [](SynthParams& s) -> std::atomic<float>& { return s.chorus_depth; }, 0.0f, 1.0f, 0.05f, fmtPct));
    add(globalFloat("FX.DRIVE", "OVERDRIVE", [](SynthParams& s) -> std::atomic<float>& { return s.drive_amount; }, 0.0f, 1.0f, 0.05f, fmtPct));
    add(globalFloat("FX.DRV_TONE", "DRV TONE", [](SynthParams& s) -> std::atomic<float>& { return s.drive_tone; }, 0.0f, 1.0f, 0.05f, fmtPct));
    {
        Param p;
        p.id = "MST.VOL";
        p.label = "MASTER";
        p.value = [](const UiContext& c) { return fmtPct(c.audio.params().master_volume.load()); };
        p.norm = [](const UiContext& c) { return c.audio.params().master_volume.load(); };
        p.step = [](UiContext& c, int dir, bool coarse) { c.audio.adjustMasterVolume(dir * (coarse ? 0.10f : 0.02f)); };
        add(std::move(p));
    }

    // ===== MASTERING BUS PARAMS =====
    {
        Param p;
        p.id = "MST.GAIN";
        p.label = "GAIN";
        p.value = [](const UiContext& c) {
            float g = c.audio.getMasterFx().gain_over;
            char b[16];
            if (g > 0.001f) std::snprintf(b, sizeof(b), "+%.1f CRN", g * 24.0f);
            else if (g < -0.001f) std::snprintf(b, sizeof(b), "%.1f dB", g * 24.0f);
            else std::snprintf(b, sizeof(b), "0 dB (MAX)");
            return std::string(b);
        };
        p.norm = [](const UiContext& c) { return (c.audio.getMasterFx().gain_over + 1.0f) * 0.5f; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.gain_over = std::clamp(mfx.gain_over + dir * (coarse ? 0.10f : 0.02f), -1.0f, 1.0f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.PAN";
        p.label = "PAN";
        p.value = [](const UiContext&) { return fmtPan(0.0f); };
        p.norm = [](const UiContext&) { return 0.5f; };
        p.step = [](UiContext&, int, bool) {};
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.SC_SRC";
        p.label = "SC SRC";
        p.value = [](const UiContext& c) {
            int src = c.audio.getMasterFx().sc_source;
            if (src == 0) return std::string("BUS COMP");
            char b[16]; std::snprintf(b, sizeof(b), "TRK %02d", src);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) { return c.audio.getMasterFx().sc_source / 16.0f; };
        p.step = [](UiContext& c, int dir, bool) {
            auto mfx = c.audio.getMasterFx();
            int max_src = c.audio.getActiveRowCount();
            mfx.sc_source = std::clamp(mfx.sc_source + dir, 0, max_src);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.SC_THR";
        p.label = "SC THR";
        p.value = [](const UiContext& c) {
            float th = c.audio.getMasterFx().sc_thresh;
            if (th >= -0.1f) return std::string("OFF");
            char b[16]; std::snprintf(b, sizeof(b), "%+.1f dB", th);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) { return (c.audio.getMasterFx().sc_thresh + 48.0f) / 48.0f; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.sc_thresh = std::clamp(mfx.sc_thresh + dir * (coarse ? 6.0f : 1.0f), -48.0f, 0.0f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.SC_RAT";
        p.label = "SC RAT";
        p.value = [](const UiContext& c) {
            float r = c.audio.getMasterFx().sc_ratio;
            char b[16]; std::snprintf(b, sizeof(b), "%.1f:1", r);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) { return (c.audio.getMasterFx().sc_ratio - 1.0f) / 19.0f; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.sc_ratio = std::clamp(mfx.sc_ratio + dir * (coarse ? 2.0f : 0.5f), 1.0f, 20.0f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.SC_ATK";
        p.label = "SC ATK";
        p.value = [](const UiContext& c) { return fmtMs(c.audio.getMasterFx().sc_attack); };
        p.norm = [](const UiContext& c) { return (c.audio.getMasterFx().sc_attack - 0.001f) / 0.099f; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.sc_attack = std::clamp(mfx.sc_attack + dir * (coarse ? 0.010f : 0.002f), 0.001f, 0.100f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.SC_REL";
        p.label = "SC REL";
        p.value = [](const UiContext& c) { return fmtMs(c.audio.getMasterFx().sc_release); };
        p.norm = [](const UiContext& c) { return (c.audio.getMasterFx().sc_release - 0.020f) / 0.980f; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.sc_release = std::clamp(mfx.sc_release + dir * (coarse ? 0.050f : 0.010f), 0.020f, 1.000f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.HPF_CUT";
        p.label = "HP CUT";
        p.value = [](const UiContext& c) { return fmtHpf(c.audio.getMasterFx().hpf_cutoff); };
        p.norm = [](const UiContext& c) { return c.audio.getMasterFx().hpf_cutoff; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.hpf_cutoff = std::clamp(mfx.hpf_cutoff + dir * (coarse ? 0.05f : 0.01f), 0.0f, 0.95f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.HPF_RES";
        p.label = "HP RES";
        p.value = [](const UiContext& c) { return fmtPct(c.audio.getMasterFx().hpf_resonance); };
        p.norm = [](const UiContext& c) { return c.audio.getMasterFx().hpf_resonance; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.hpf_resonance = std::clamp(mfx.hpf_resonance + dir * (coarse ? 0.05f : 0.01f), 0.0f, 0.95f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.HPF_SLP";
        p.label = "HP SLP";
        p.value = [](const UiContext& c) { return c.audio.getMasterFx().hpf_slope_24 ? "24 dB" : "12 dB"; };
        p.norm = [](const UiContext& c) { return c.audio.getMasterFx().hpf_slope_24 ? 1.0f : 0.0f; };
        p.step = [](UiContext& c, int, bool) {
            auto mfx = c.audio.getMasterFx();
            mfx.hpf_slope_24 = !mfx.hpf_slope_24;
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.LPF_CUT";
        p.label = "LP CUT";
        p.value = [](const UiContext& c) { return fmtLpf(c.audio.getMasterFx().lpf_cutoff); };
        p.norm = [](const UiContext& c) { return c.audio.getMasterFx().lpf_cutoff; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.lpf_cutoff = std::clamp(mfx.lpf_cutoff + dir * (coarse ? 0.05f : 0.01f), 0.05f, 1.0f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.LPF_RES";
        p.label = "LP RES";
        p.value = [](const UiContext& c) { return fmtPct(c.audio.getMasterFx().lpf_resonance); };
        p.norm = [](const UiContext& c) { return c.audio.getMasterFx().lpf_resonance; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.lpf_resonance = std::clamp(mfx.lpf_resonance + dir * (coarse ? 0.05f : 0.01f), 0.0f, 0.95f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.LPF_SLP";
        p.label = "LP SLP";
        p.value = [](const UiContext& c) { return c.audio.getMasterFx().lpf_slope_24 ? "24 dB" : "12 dB"; };
        p.norm = [](const UiContext& c) { return c.audio.getMasterFx().lpf_slope_24 ? 1.0f : 0.0f; };
        p.step = [](UiContext& c, int, bool) {
            auto mfx = c.audio.getMasterFx();
            mfx.lpf_slope_24 = !mfx.lpf_slope_24;
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    auto addBellParams = [&](int idx, auto get_freq, auto set_freq, auto get_gain, auto set_gain, auto get_q, auto set_q) {
        std::string s_idx = std::to_string(idx);
        std::string pre = "MST.EQ" + s_idx + "_";
        {
            Param p;
            p.id = pre + "FRQ";
            p.label = "EQ" + s_idx + " F";
            p.value = [get_freq](const UiContext& c) {
                float f = 20.0f * std::pow(900.0f, get_freq(c.audio.getMasterFx()));
                return fmtHz(f);
            };
            p.norm = [get_freq](const UiContext& c) { return get_freq(c.audio.getMasterFx()); };
            p.step = [get_freq, set_freq](UiContext& c, int dir, bool coarse) {
                auto mfx = c.audio.getMasterFx();
                set_freq(mfx, std::clamp(get_freq(mfx) + dir * (coarse ? 0.05f : 0.01f), 0.0f, 1.0f));
                c.audio.setMasterFx(mfx);
            };
            add(std::move(p));
        }
        {
            Param p;
            p.id = pre + "GAIN";
            p.label = "EQ" + s_idx + " G";
            p.value = [get_gain](const UiContext& c) {
                float g = get_gain(c.audio.getMasterFx());
                char b[16]; std::snprintf(b, sizeof(b), "%+.1f dB", g);
                return std::string(b);
            };
            p.norm = [get_gain](const UiContext& c) { return (get_gain(c.audio.getMasterFx()) + 18.0f) / 36.0f; };
            p.step = [get_gain, set_gain](UiContext& c, int dir, bool coarse) {
                auto mfx = c.audio.getMasterFx();
                set_gain(mfx, std::clamp(get_gain(mfx) + dir * (coarse ? 3.0f : 0.5f), -18.0f, 18.0f));
                c.audio.setMasterFx(mfx);
            };
            add(std::move(p));
        }
        {
            Param p;
            p.id = pre + "Q";
            p.label = "EQ" + s_idx + " Q";
            p.value = [get_q](const UiContext& c) {
                float q = get_q(c.audio.getMasterFx());
                char b[16]; std::snprintf(b, sizeof(b), "Q %.2f", q);
                return std::string(b);
            };
            p.norm = [get_q](const UiContext& c) { return (get_q(c.audio.getMasterFx()) - 0.2f) / 7.8f; };
            p.step = [get_q, set_q](UiContext& c, int dir, bool coarse) {
                auto mfx = c.audio.getMasterFx();
                set_q(mfx, std::clamp(get_q(mfx) + dir * (coarse ? 0.5f : 0.1f), 0.2f, 8.0f));
                c.audio.setMasterFx(mfx);
            };
            add(std::move(p));
        }
    };

    addBellParams(1,
        [](const MasterFxParameters& m) { return m.bell_freq; }, [](MasterFxParameters& m, float v) { m.bell_freq = v; },
        [](const MasterFxParameters& m) { return m.bell_gain; }, [](MasterFxParameters& m, float v) { m.bell_gain = v; },
        [](const MasterFxParameters& m) { return m.bell_q; },    [](MasterFxParameters& m, float v) { m.bell_q = v; });

    addBellParams(2,
        [](const MasterFxParameters& m) { return m.bell2_freq; }, [](MasterFxParameters& m, float v) { m.bell2_freq = v; },
        [](const MasterFxParameters& m) { return m.bell2_gain; }, [](MasterFxParameters& m, float v) { m.bell2_gain = v; },
        [](const MasterFxParameters& m) { return m.bell2_q; },    [](MasterFxParameters& m, float v) { m.bell2_q = v; });

    addBellParams(3,
        [](const MasterFxParameters& m) { return m.bell3_freq; }, [](MasterFxParameters& m, float v) { m.bell3_freq = v; },
        [](const MasterFxParameters& m) { return m.bell3_gain; }, [](MasterFxParameters& m, float v) { m.bell3_gain = v; },
        [](const MasterFxParameters& m) { return m.bell3_q; },    [](MasterFxParameters& m, float v) { m.bell3_q = v; });

    // Аліаси для зворотної сумісності (MST.EQ_FRQ, MST.EQ_GAIN, MST.EQ_Q)
    {
        Param p;
        p.id = "MST.EQ_FRQ";
        p.label = "EQ FRQ";
        p.value = [](const UiContext& c) { return fmtHz(20.0f * std::pow(900.0f, c.audio.getMasterFx().bell_freq)); };
        p.norm = [](const UiContext& c) { return c.audio.getMasterFx().bell_freq; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.bell_freq = std::clamp(mfx.bell_freq + dir * (coarse ? 0.05f : 0.01f), 0.0f, 1.0f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.EQ_GAIN";
        p.label = "EQ GAIN";
        p.value = [](const UiContext& c) {
            float g = c.audio.getMasterFx().bell_gain;
            char b[16]; std::snprintf(b, sizeof(b), "%+.1f dB", g);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) { return (c.audio.getMasterFx().bell_gain + 18.0f) / 36.0f; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.bell_gain = std::clamp(mfx.bell_gain + dir * (coarse ? 3.0f : 0.5f), -18.0f, 18.0f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "MST.EQ_Q";
        p.label = "EQ Q";
        p.value = [](const UiContext& c) {
            float q = c.audio.getMasterFx().bell_q;
            char b[16]; std::snprintf(b, sizeof(b), "Q %.2f", q);
            return std::string(b);
        };
        p.norm = [](const UiContext& c) { return (c.audio.getMasterFx().bell_q - 0.2f) / 7.8f; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto mfx = c.audio.getMasterFx();
            mfx.bell_q = std::clamp(mfx.bell_q + dir * (coarse ? 0.5f : 0.1f), 0.2f, 8.0f);
            c.audio.setMasterFx(mfx);
        };
        add(std::move(p));
    }
    {
        Param p = info("MST.PEAK", "PEAK", [](const UiContext& c) { return fmtPct(c.tel.peak_hold); });
        p.norm = [](const UiContext& c) { return c.tel.peak_hold; };
        add(std::move(p));
    }
    add(info("DLY.TIME", "DLY TIME", [](const UiContext& c) {
        char b[16];
        std::snprintf(b, sizeof(b), "%s", c.tel.sample_rate == 48000 ? "250 MS" : "250 MS");
        return std::string(b);
    }));
    {
        Param p;
        p.id = "MST.BPM";
        p.label = "BPM";
        p.value = [](const UiContext& c) { char b[16]; std::snprintf(b, sizeof(b), "%3.0f", c.app.bpm); return std::string(b); };
        p.norm = [](const UiContext& c) { return (c.app.bpm - 60.0f) / 140.0f; };
        p.step = [](UiContext& c, int dir, bool coarse) { c.app.bpm = std::clamp(c.app.bpm + dir * (coarse ? 10.0f : 1.0f), 60.0f, 200.0f); };
        add(std::move(p));
    }

    // ===== SYS.* — системні =====
    {
        Param p;
        p.id = "SYS.BUF";
        p.label = "AUDIO BUFFER";
        p.value = [](const UiContext& c) {
            char b[40];
            const float ms = c.tel.sample_rate ? c.tel.period_frames * 1000.0f / c.tel.sample_rate : 0.0f;
            std::snprintf(b, sizeof(b), "%u SMP (%.1f MS)", c.tel.period_frames, ms);
            return std::string(b);
        };
        p.step = [](UiContext& c, int, bool) { c.audio.cycleBufferSize(); };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.USB";
        p.label = "USB ROLE";
        p.value = [](const UiContext& c) {
            if (c.hw.usb_role == "host") return std::string("HOST (OTG 5V MIDI/AUDIO)");
            if (c.hw.usb_role == "device_audio") return std::string("AUDIO GADGET :8000");
            return std::string("DEVICE (SSH 10.1.1.2)");
        };
        p.step = [](UiContext& c, int, bool) { if (c.hw_mon) c.hw_mon->toggleUsbRole(); };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.PANIC";
        p.label = "PANIC";
        p.value = [](const UiContext&) { return std::string("KILL ALL VOICES"); };
        p.step = [](UiContext& c, int, bool) { c.audio.panic(); };
        add(std::move(p));
    }
    add(info("SYS.SR", "SAMPLE RATE", [](const UiContext& c) {
        char b[40];
        std::snprintf(b, sizeof(b), "%u HZ (%s)", c.tel.sample_rate, c.hw.usb_role == "host" ? "USB UAC2" : "RK817 DAC");
        return std::string(b);
    }));
    add(info("SYS.OUT", "OUTPUT", [](const UiContext& c) {
        return std::string(c.in.headphones_plugged ? "HEADPHONES" : "SPEAKER");
    }));
    add(info("SYS.CPU", "CPU", [](const UiContext& c) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.1f%% %dC", c.hw.cpu_load_percent, c.hw.cpu_temp_c);
        return std::string(b);
    }));
    add(info("SYS.RAM", "RAM", [](const UiContext& c) {
        char b[32];
        std::snprintf(b, sizeof(b), "%d / %d MB", c.hw.ram_used_mb, c.hw.ram_total_mb);
        return std::string(b);
    }));
    add(info("SYS.XRUN", "XRUNS", [](const UiContext& c) { return std::to_string(c.tel.xrun_count); }));

    // ===== ELEKTRON PATTERNS & PROJECT PERSISTENCE =====
    {
        Param p;
        p.id = "SYS.BANK";
        p.label = "BANK (A-H)";
        p.value = [](const UiContext& c) {
            char b[16];
            std::snprintf(b, sizeof(b), "BANK %c", 'A' + c.app.pattern_mgr.currentBank());
            return std::string(b);
        };
        p.step = [](UiContext& c, int dir, bool) {
            int b = (c.app.pattern_mgr.currentBank() + dir + kNumBanks) % kNumBanks;
            c.app.pattern_mgr.switchPatternNow(b, c.app.pattern_mgr.currentPattern(), c.audio, c.app.bpm);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.PAT";
        p.label = "PATTERN";
        p.value = [](const UiContext& c) { return c.app.pattern_mgr.activePatternName(); };
        p.step = [](UiContext& c, int dir, bool) {
            int p_idx = (c.app.pattern_mgr.currentPattern() + dir + kPatternsPerBank) % kPatternsPerBank;
            c.app.pattern_mgr.switchPatternNow(c.app.pattern_mgr.currentBank(), p_idx, c.audio, c.app.bpm);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.SAVE";
        p.label = "SAVE PROJ";
        p.value = [](const UiContext&) { return "QUICK [A]"; };
        p.step = [](UiContext& c, int, bool) {
            ActionRegistry::instance().run("PROJECT.SAVE", c);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.SAVE_AS";
        p.label = "SAVE AS";
        p.value = [](const UiContext&) { return "BROWSE [A]"; };
        p.step = [](UiContext& c, int, bool) {
            ActionRegistry::instance().run("PROJECT.SAVE_AS_OPEN", c);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.LOAD";
        p.label = "LOAD PROJ";
        p.value = [](const UiContext&) { return "BROWSE [A]"; };
        p.step = [](UiContext& c, int, bool) {
            ActionRegistry::instance().run("PROJECT.LOAD_OPEN", c);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.SAMPLER";
        p.label = "SAMPLER REC";
        p.value = [](const UiContext& c) {
            switch (c.app.sampler.recordingState()) {
                case RecState::kArmed: return std::string("ARMED (THRESHOLD)");
                case RecState::kRecording: return std::string("RECORDING...");
                case RecState::kFinished: return std::string("SAMPLE READY");
                default: return std::string("IDLE (A: ARM)");
            }
        };
        p.step = [](UiContext& c, int, bool) {
            if (c.app.sampler.recordingState() == RecState::kIdle) {
                c.app.sampler.armRecording();
                c.app.last_action = "REC ARMED";
            } else if (c.app.sampler.recordingState() == RecState::kRecording) {
                c.app.sampler.stopRecording();
                c.app.last_action = "REC STOPPED";
            } else {
                c.app.sampler.armRecording();
            }
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.SLICES";
        p.label = "SLICER GRID";
        p.value = [](const UiContext& c) { return std::to_string(c.app.sampler.sliceCount()) + " SLICES"; };
        p.step = [](UiContext& c, int dir, bool) {
            int cnt = c.app.sampler.sliceCount();
            cnt = dir > 0 ? (cnt < 16 ? 16 : 32) : (cnt > 16 ? 16 : 8);
            c.app.sampler.sliceGrid(cnt);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.REC_SRC";
        p.label = "INPUT";
        p.value = [](const UiContext& c) {
            return c.in.headphones_plugged ? "LINE-IN" : "INT MIC";
        };
        p.step = [](UiContext& c, int, bool) {
            c.app.last_action = "INPUT TOGGLED";
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.REC_ARM";
        p.label = "REC ARM";
        p.value = [](const UiContext& c) {
            auto st = c.app.sampler.recordingState();
            switch (st) {
                case RecState::kArmed: return "ARMED";
                case RecState::kRecording: return "RECORD";
                case RecState::kFinished: return "DONE";
                default: return "STANDBY";
            }
        };
        p.step = [](UiContext& c, int, bool) {
            if (c.app.sampler.recordingState() == RecState::kIdle) {
                c.app.sampler.armRecording();
                c.app.last_action = "SAMPLER ARMED";
            } else {
                c.app.sampler.stopRecording();
                c.app.last_action = "SAMPLER STOPPED";
            }
        };
        add(std::move(p));
    }

    // =========================================================================
    // SAMPLING WORKSTATION (SMP.*)
    // =========================================================================
    {
        Param p;
        p.id = "SMP.SRC";
        p.label = "SRC";
        p.value = [](const UiContext& c) {
            return c.in.headphones_plugged ? "LINE-IN" : "MASTER";
        };
        p.step = [](UiContext& c, int, bool) {
            c.app.last_action = "REC SRC TOGGLED";
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.ARM";
        p.label = "REC ARM";
        p.value = [](const UiContext& c) {
            auto st = c.app.sampler.recordingState();
            switch (st) {
                case RecState::kArmed: return "ARMED";
                case RecState::kRecording: return "RECORD";
                case RecState::kOverdub: return "OVERDUB";
                case RecState::kFinished: return "LOOP";
                default: return "OFF";
            }
        };
        p.step = [](UiContext& c, int dir, bool) {
            auto st = c.app.sampler.recordingState();
            if (dir > 0) {
                if (st == RecState::kIdle) {
                    c.app.sampler.armRecording();
                    c.app.last_action = "REC ARMED";
                } else if (st == RecState::kArmed) {
                    c.app.sampler.startRecording();
                    c.app.last_action = "RECORDING";
                } else if (st == RecState::kRecording) {
                    c.app.sampler.startOverdub();
                    c.app.last_action = "OVERDUBBING";
                } else if (st == RecState::kOverdub) {
                    c.app.sampler.stopRecording();
                    c.app.last_action = "LOOP STOPPED";
                } else {
                    c.app.sampler.clearRecordedSample();
                    c.app.last_action = "LOOP CLEARED";
                }
            } else {
                if (st != RecState::kIdle) {
                    c.app.sampler.stopRecording();
                    c.app.last_action = "REC STOPPED";
                }
            }
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.MODE";
        p.label = "MODE";
        p.value = [](const UiContext& c) {
            auto m = c.app.sampler.recordingMode();
            switch (m) {
                case RecMode::kThreshold: return "THRESH";
                case RecMode::kSyncBar:   return "SYNC 2BAR";
                default:                  return "MANUAL";
            }
        };
        p.step = [](UiContext& c, int dir, bool) {
            int m = (static_cast<int>(c.app.sampler.recordingMode()) + dir + 3) % 3;
            auto mode = static_cast<RecMode>(m);
            c.app.sampler.setRecordingMode(mode);
            if (mode == RecMode::kSyncBar) {
                c.app.sampler.armSyncedRecording(2, c.app.bpm, c.tel.sample_rate);
                c.app.last_action = "SYNC LOOPER 2BARS";
            }
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.THRS";
        p.label = "TRANSIENT";
        p.value = [](const UiContext& c) {
            char b[32];
            std::snprintf(b, sizeof(b), "AUTO %d%%", static_cast<int>(c.app.sample_transient_sens * 100.0f));
            return std::string(b);
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            float delta = coarse ? 0.20f : 0.05f;
            c.app.sample_transient_sens = std::clamp(c.app.sample_transient_sens + dir * delta, 0.05f, 0.95f);
            c.app.sampler.sliceTransients(c.app.sample_transient_sens);
            c.app.last_action = "TRANSIENTS: " + std::to_string(c.app.sampler.sliceCount()) + " SLICES";
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.TRIM_ST";
        p.label = "TRIM ST";
        p.value = [](const UiContext& c) {
            char b[16];
            std::snprintf(b, sizeof(b), "%d%%", static_cast<int>(c.app.sample_trim_start * 100.0f));
            return std::string(b);
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            float d = coarse ? 0.10f : 0.01f;
            c.app.sample_trim_start = std::clamp(c.app.sample_trim_start + dir * d, 0.0f, c.app.sample_trim_end - 0.02f);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.TRIM_END";
        p.label = "TRIM END";
        p.value = [](const UiContext& c) {
            char b[16];
            std::snprintf(b, sizeof(b), "%d%%", static_cast<int>(c.app.sample_trim_end * 100.0f));
            return std::string(b);
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            float d = coarse ? 0.10f : 0.01f;
            c.app.sample_trim_end = std::clamp(c.app.sample_trim_end + dir * d, c.app.sample_trim_start + 0.02f, 1.0f);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.DUB_FBK";
        p.label = "DUB FBK";
        p.value = [](const UiContext& c) {
            char b[16];
            std::snprintf(b, sizeof(b), "%d%%", static_cast<int>(c.app.sampler.overdubFeedback() * 100.0f));
            return std::string(b);
        };
        p.norm = [](const UiContext& c) { return c.app.sampler.overdubFeedback(); };
        p.step = [](UiContext& c, int dir, bool coarse) {
            float d = coarse ? 0.10f : 0.05f;
            c.app.sampler.setOverdubFeedback(c.app.sampler.overdubFeedback() + dir * d);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.ZOOM";
        p.label = "ZOOM";
        p.value = [](const UiContext& c) {
            return std::to_string(c.app.sample_zoom) + "X";
        };
        p.step = [](UiContext& c, int dir, bool) {
            c.app.sample_zoom = std::clamp(c.app.sample_zoom + dir, 1, 8);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.ASSIGN";
        p.label = "ASSIGN";
        p.value = [](const UiContext& c) {
            return "TRK " + std::to_string(c.app.sample_target_track + 1);
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            if (coarse) {
                c.app.sampler.sliceToDrumSlots(c.app.sample_trim_start, c.app.sample_trim_end);
                c.app.last_action = "SLICED -> DRUM SLOTS 1-8";
            } else {
                c.app.sample_target_track = (c.app.sample_target_track + dir + 4) % 4;
                c.app.sampler.assignTrimmedToSlot(c.app.sample_target_track, c.app.sample_trim_start, c.app.sample_trim_end);
                c.app.last_action = "ASSIGNED -> TRK " + std::to_string(c.app.sample_target_track + 1);
            }
        };
        add(std::move(p));
    }

    // ===== STEP.* — крок під курсором секвенсора (INSPECT) =====
    add(stepParam("STEP.TYPE", "TYPE",
        [](const SequencerStep& s, const TrackParameters&) {
            return std::string(!s.enabled ? "OFF" : (s.is_param_trig ? "PARAM TRIG" : "NOTE TRIG"));
        },
        [](SequencerStep& s, const TrackParameters&, int dir, bool) {
            // Цикл OFF -> NOTE -> PARAM -> OFF
            int state = !s.enabled ? 0 : (s.is_param_trig ? 2 : 1);
            state = (state + dir + 3) % 3;
            s.enabled = state != 0;
            s.is_param_trig = state == 2;
        }));
    add(stepParam("STEP.NOTE", "NOTE 1",
        [](const SequencerStep& s, const TrackParameters&) { return noteName(s.note) + " (" + std::to_string(s.note) + ")"; },
        [](SequencerStep& s, const TrackParameters&, int dir, bool coarse) {
            s.note = static_cast<uint8_t>(std::clamp(s.note + dir * (coarse ? 12 : 1), 0, 127));
            s.chord_notes[0] = s.note;
        }));
    add(stepParam("STEP.CHORD1", "NOTE 2",
        [](const SequencerStep& s, const TrackParameters&) {
            if (s.chord_notes[1] == 255) return std::string("--- (OFF)");
            return noteName(s.chord_notes[1]) + " (" + std::to_string(s.chord_notes[1]) + ")";
        },
        [](SequencerStep& s, const TrackParameters&, int dir, bool coarse) {
            if (s.chord_notes[1] == 255 && dir > 0) s.chord_notes[1] = static_cast<uint8_t>(std::clamp(s.note + 4, 0, 127));
            else if (s.chord_notes[1] != 255) {
                int n = static_cast<int>(s.chord_notes[1]) + dir * (coarse ? 12 : 1);
                if (n < 24) s.chord_notes[1] = 255;
                else s.chord_notes[1] = static_cast<uint8_t>(std::clamp(n, 24, 127));
            }
        }));
    add(stepParam("STEP.CHORD2", "NOTE 3",
        [](const SequencerStep& s, const TrackParameters&) {
            if (s.chord_notes[2] == 255) return std::string("--- (OFF)");
            return noteName(s.chord_notes[2]) + " (" + std::to_string(s.chord_notes[2]) + ")";
        },
        [](SequencerStep& s, const TrackParameters&, int dir, bool coarse) {
            if (s.chord_notes[2] == 255 && dir > 0) s.chord_notes[2] = static_cast<uint8_t>(std::clamp(s.note + 7, 0, 127));
            else if (s.chord_notes[2] != 255) {
                int n = static_cast<int>(s.chord_notes[2]) + dir * (coarse ? 12 : 1);
                if (n < 24) s.chord_notes[2] = 255;
                else s.chord_notes[2] = static_cast<uint8_t>(std::clamp(n, 24, 127));
            }
        }));
    add(stepParam("STEP.CHORD3", "NOTE 4",
        [](const SequencerStep& s, const TrackParameters&) {
            if (s.chord_notes[3] == 255) return std::string("--- (OFF)");
            return noteName(s.chord_notes[3]) + " (" + std::to_string(s.chord_notes[3]) + ")";
        },
        [](SequencerStep& s, const TrackParameters&, int dir, bool coarse) {
            if (s.chord_notes[3] == 255 && dir > 0) s.chord_notes[3] = static_cast<uint8_t>(std::clamp(s.note + 11, 0, 127));
            else if (s.chord_notes[3] != 255) {
                int n = static_cast<int>(s.chord_notes[3]) + dir * (coarse ? 12 : 1);
                if (n < 24) s.chord_notes[3] = 255;
                else s.chord_notes[3] = static_cast<uint8_t>(std::clamp(n, 24, 127));
            }
        }));
    add(stepParam("STEP.VEL", "VELOCITY",
        [](const SequencerStep& s, const TrackParameters&) { return fmt127(s.velocity); },
        [](SequencerStep& s, const TrackParameters&, int dir, bool coarse) {
            s.velocity = std::clamp(s.velocity + stepNorm(dir, coarse), 0.05f, 1.0f);
        }));
    add(stepParam("STEP.GATE", "GATE LEN",
        [](const SequencerStep& s, const TrackParameters&) {
            int g = std::clamp(static_cast<int>(s.gate_len + 0.5f), 1, 128);
            return std::to_string(g) + (g == 1 ? " STEP" : " STEPS");
        },
        [](SequencerStep& s, const TrackParameters&, int dir, bool coarse) {
            int g = std::clamp(static_cast<int>(s.gate_len + 0.5f) + dir * (coarse ? 8 : 1), 1, 128);
            s.gate_len = static_cast<float>(g);
        }));
    add(stepParam("STEP.SLIDE", "SLIDE",
        [](const SequencerStep& s, const TrackParameters&) {
            return std::string(s.slide ? "ON" : "OFF");
        },
        [](SequencerStep& s, const TrackParameters&, int, bool) {
            s.slide = !s.slide;
        }));
    add(stepParam("STEP.CUT", "CUT LOCK",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.plock_cutoff >= 0.0f ? fmt127(s.plock_cutoff) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.plock_cutoff, tp.lpf_cutoff, dir, coarse, s);
        }));
    add(stepParam("STEP.ATK", "ATTACK",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.attack >= 0.0f ? fmt127(s.attack) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.attack, tp.attack, dir, coarse, s);
        }));
    add(stepParam("STEP.DEC", "DECAY",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.decay >= 0.0f ? fmt127(s.decay) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.decay, tp.decay, dir, coarse, s);
        }));
    add(stepParam("STEP.SUS", "SUSTAIN",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.sustain >= 0.0f ? fmt127(s.sustain) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.sustain, tp.sustain, dir, coarse, s);
        }));
    add(stepParam("STEP.REL", "RELEASE",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.release >= 0.0f ? fmt127(s.release) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.release, tp.release, dir, coarse, s);
        }));
    for (int i = 0; i < 6; ++i) {
        add(stepParam("STEP.P" + std::to_string(i + 1), "P" + std::to_string(i + 1),
            [i](const SequencerStep& s, const TrackParameters&) {
                return s.plock_p[i] >= 0.0f ? fmt127(s.plock_p[i]) + " L" : std::string("--- (TRK)");
            },
            [i](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
                stepPlockFloat(s.plock_p[i], tp.p[i], dir, coarse, s);
            },
            [i](const UiContext& c) {
                const auto tp = c.audio.getTrackParams(c.audio.getRowTrack(c.app.cursor_row));
                return "P" + std::to_string(i + 1) + " " + std::string(getParamName(tp.engine, i));
            }));
    }
    add(stepParam("STEP.SMP_START", "SMP START",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.plock_sample_start >= 0.0f ? fmt127(s.plock_sample_start) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.plock_sample_start, tp.sample.start, dir, coarse, s);
        }));
    add(stepParam("STEP.SMP_LOOP", "SMP LOOP",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.plock_sample_loop >= 0.0f ? fmt127(s.plock_sample_loop) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.plock_sample_loop, tp.sample.loop_point, dir, coarse, s);
        }));
    add(stepParam("STEP.SMP_END", "SMP END",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.plock_sample_end >= 0.0f ? fmt127(s.plock_sample_end) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.plock_sample_end, tp.sample.end, dir, coarse, s);
        }));
    add(stepParam("STEP.DLY_SEND", "DLY SEND",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.plock_delay_send >= 0.0f ? fmt127(s.plock_delay_send) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.plock_delay_send, tp.delay_send, dir, coarse, s);
        }));
    add(stepParam("STEP.REV_SEND", "REV SEND",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.plock_reverb_send >= 0.0f ? fmt127(s.plock_reverb_send) + " L" : std::string("--- (TRK)");
        },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            stepPlockFloat(s.plock_reverb_send, tp.reverb_send, dir, coarse, s);
        }));
    add(stepParam("STEP.PROB", "PROB",
        [](const SequencerStep& s, const TrackParameters&) { return std::to_string(s.probability) + "%"; },
        [](SequencerStep& s, const TrackParameters&, int dir, bool coarse) {
            s.probability = static_cast<uint8_t>(std::clamp(s.probability + dir * (coarse ? 10 : 1), 0, 100));
        }));
    add(stepParam("STEP.COND", "CONDITION",
        [](const SequencerStep& s, const TrackParameters&) { return std::string(getConditionName(s.condition)); },
        [](SequencerStep& s, const TrackParameters&, int dir, bool) {
            const int n = static_cast<int>(TrigCondition::kCount);
            s.condition = static_cast<TrigCondition>((static_cast<int>(s.condition) + dir + n) % n);
        }));

    // =========================================================================
    // 7. ДВОКАСКАДНИЙ АРПЕДЖИРАТОР ТА ПРОЦЕДУРНИЙ ГЕНЕРАТОР (ARP.*)
    // =========================================================================
    {
        Param p;
        p.id = "ARP.SCALE";
        p.label = "SCALE";
        p.value = [](const UiContext& c) { return std::string(getScaleName(c.app.arpeggiator.config().scale)); };
        p.step = [](UiContext& c, int dir, bool) {
            auto& cfg = c.app.arpeggiator.config();
            const int n = static_cast<int>(ScaleType::kCount);
            cfg.scale = static_cast<ScaleType>((static_cast<int>(cfg.scale) + dir + n) % n);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.ROOT";
        p.label = "ROOT";
        p.value = [](const UiContext& c) { return noteName(c.app.arpeggiator.config().root_note); };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto& cfg = c.app.arpeggiator.config();
            cfg.root_note = std::clamp(cfg.root_note + dir * (coarse ? 12 : 1), 24, 84);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.DIV1";
        p.label = "ARP1 DIV";
        p.value = [](const UiContext& c) { return std::string(getArp1DivisionName(c.app.arpeggiator.config().arp1_div)); };
        p.step = [](UiContext& c, int dir, bool) {
            auto& cfg = c.app.arpeggiator.config();
            const int n = static_cast<int>(Arp1Division::kCount);
            cfg.arp1_div = static_cast<Arp1Division>((static_cast<int>(cfg.arp1_div) + dir + n) % n);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.CHORD";
        p.label = "CHORD";
        p.value = [](const UiContext& c) { return std::string(getChordTypeName(c.app.arpeggiator.config().chord_type)); };
        p.step = [](UiContext& c, int dir, bool) {
            auto& cfg = c.app.arpeggiator.config();
            const int n = static_cast<int>(ChordType::kCount);
            cfg.chord_type = static_cast<ChordType>((static_cast<int>(cfg.chord_type) + dir + n) % n);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.PROG";
        p.label = "PROG";
        p.value = [](const UiContext& c) {
            int pid = c.app.arpeggiator.config().progression_id;
            switch (pid) {
                case 1: return std::string("i-VI-III-VII");
                case 2: return std::string("I-IV-V-I");
                case 3: return std::string("i-iv-v-v");
                default: return std::string("STATIC");
            }
        };
        p.step = [](UiContext& c, int dir, bool) {
            auto& cfg = c.app.arpeggiator.config();
            cfg.progression_id = (cfg.progression_id + dir + 4) % 4;
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.DIV2";
        p.label = "ARP2 DIV";
        p.value = [](const UiContext& c) { return std::string(getArp2DivisionName(c.app.arpeggiator.config().arp2_div)); };
        p.step = [](UiContext& c, int dir, bool) {
            auto& cfg = c.app.arpeggiator.config();
            const int n = static_cast<int>(Arp2Division::kCount);
            cfg.arp2_div = static_cast<Arp2Division>((static_cast<int>(cfg.arp2_div) + dir + n) % n);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.PAT";
        p.label = "PATTERN";
        p.value = [](const UiContext& c) { return std::string(getArpPatternName(c.app.arpeggiator.config().pattern)); };
        p.step = [](UiContext& c, int dir, bool) {
            auto& cfg = c.app.arpeggiator.config();
            const int n = static_cast<int>(ArpPattern::kCount);
            cfg.pattern = static_cast<ArpPattern>((static_cast<int>(cfg.pattern) + dir + n) % n);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.OCT";
        p.label = "OCTAVES";
        p.value = [](const UiContext& c) { return std::to_string(c.app.arpeggiator.config().octaves) + " OCT"; };
        p.step = [](UiContext& c, int dir, bool) {
            auto& cfg = c.app.arpeggiator.config();
            cfg.octaves = std::clamp(cfg.octaves + dir, 1, 4);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.GATE";
        p.label = "GATE LEN";
        p.value = [](const UiContext& c) { return fmtPct(c.app.arpeggiator.config().arp2_gate); };
        p.norm = [](const UiContext& c) { return c.app.arpeggiator.config().arp2_gate; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto& cfg = c.app.arpeggiator.config();
            cfg.arp2_gate = std::clamp(cfg.arp2_gate + dir * (coarse ? 0.20f : 0.05f), 0.10f, 1.20f);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.EUCLID";
        p.label = "EUCLID";
        p.value = [](const UiContext& c) {
            char b[16];
            std::snprintf(b, sizeof(b), "%d/%d", c.app.arpeggiator.config().euclid_hits, c.app.arpeggiator.config().euclid_length);
            return std::string(b);
        };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto& cfg = c.app.arpeggiator.config();
            cfg.euclid_hits = std::clamp(cfg.euclid_hits + dir * (coarse ? 4 : 1), 1, cfg.euclid_length);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.MUTATE";
        p.label = "MUTATION";
        p.value = [](const UiContext& c) { return std::to_string(c.app.arpeggiator.config().mutation_pct) + "%"; };
        p.norm = [](const UiContext& c) { return static_cast<float>(c.app.arpeggiator.config().mutation_pct) / 100.0f; };
        p.step = [](UiContext& c, int dir, bool coarse) {
            auto& cfg = c.app.arpeggiator.config();
            cfg.mutation_pct = std::clamp(cfg.mutation_pct + dir * (coarse ? 10 : 2), 0, 100);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.RND";
        p.label = "RANDOM";
        p.value = [](const UiContext&) { return "DICE [A]"; };
        p.step = [](UiContext& c, int, bool) {
            ActionRegistry::instance().run("ARP.RANDOMIZE", c);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "ARP.COMMIT";
        p.label = "APPLY";
        p.value = [](const UiContext& c) {
            char b[16];
            std::snprintf(b, sizeof(b), "@ST%02d [A]", c.app.cursor_step + 1);
            return std::string(b);
        };
        p.step = [](UiContext& c, int, bool) {
            ActionRegistry::instance().run("ARP.COMMIT", c);
        };
        add(std::move(p));
    }
}

} // namespace groovebox
