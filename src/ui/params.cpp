#include "ui/params.hpp"
#include "ui/actions.hpp"

#include <algorithm>
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
    p.id = std::move(id);
    p.label = std::move(label);
    p.value = [=](const UiContext& c) { auto tp = c.audio.getTrackParams(trk(c, t)); return std::string(name(acc(tp))); };
    p.step = [=](UiContext& c, int dir, bool) {
        const int ti = trk(c, t);
        auto tp = c.audio.getTrackParams(ti);
        const int n = static_cast<int>(E::kCount);
        acc(tp) = static_cast<E>((static_cast<int>(acc(tp)) + dir + n) % n);
        c.audio.setTrackParams(ti, tp);
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
            if (idx == 4) {
                int a = std::clamp(static_cast<int>(tp.p[4] * 31.99f) + 1, 1, 32);
                return "ALGO " + std::to_string(a);
            }
        } else if (tp.engine == EngineType::kDrumRack && idx == 0) {
            static const char* kKits[] = {"TR-808", "TR-909", "TR-707", "LINN", "DMX", "CR-78", "SP1200", "TR-606"};
            int k = std::clamp(static_cast<int>(tp.p[0] * 7.99f), 0, 7);
            return std::string(kKits[k]);
        }
        return fmt127(tp.p[idx]);
    };
    p.norm = [=](const UiContext& c) { return c.audio.getTrackParams(trk(c, kSelectedTrack)).p[idx]; };
    p.step = [=](UiContext& c, int dir, bool coarse) {
        const int t = trk(c, kSelectedTrack);
        auto tp = c.audio.getTrackParams(t);
        if (tp.engine == EngineType::kDx7 && idx == 0) {
            int total = c.audio.dx7().patchCount();
            if (total > 0) {
                int cur = std::clamp(static_cast<int>(tp.p[0] * (total - 1) + 0.5f), 0, total - 1);
                int next = std::clamp(cur + dir, 0, total - 1);
                tp.p[0] = static_cast<float>(next) / static_cast<float>(total - 1);
                c.audio.setTrackParams(t, tp);
                return;
            }
        } else if (tp.engine == EngineType::kDrumRack && idx == 0) {
            int cur = std::clamp(static_cast<int>(tp.p[0] * 7.99f), 0, 7);
            int next = std::clamp(cur + dir, 0, 7);
            tp.p[0] = static_cast<float>(next) / 7.0f;
            static const char* kKitDirs[] = {
                "res/samples/drums/01_tr808",
                "res/samples/drums/02_tr909",
                "res/samples/drums/03_tr707",
                "res/samples/drums/04_linndrum",
                "res/samples/drums/05_oberheim_dmx",
                "res/samples/drums/06_cr78",
                "res/samples/drums/07_sp1200",
                "res/samples/drums/08_tr606"
            };
            c.audio.sampler().loadDrumKit(kKitDirs[next]);
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
using StepMutator = void (*)(SequencerStep&, const TrackParameters&, int dir, bool coarse);
using StepFormatter = std::string (*)(const SequencerStep&, const TrackParameters&);

Param stepParam(std::string id, std::string label, StepFormatter fmt, StepMutator mut) {
    Param p;
    p.id = std::move(id);
    p.label = std::move(label);
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

float lockedOr(float v, float dflt) { return v >= 0.0f ? v : dflt; }
float stepNorm(int dir, bool coarse) { return dir * (coarse ? 8.0f : 1.0f) / 127.0f; }

const char* lfoWaveName(LfoWave w) { return getLfoWaveName(w); }
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
        p.dyn_label = [](const UiContext& c) {
            const auto tp = c.audio.getTrackParams(trk(c, -1));
            if (tp.engine == EngineType::kDrumRack) return std::string("KIT");
            if (tp.engine == EngineType::kDx7) return std::string("PATCH");
            if (tp.engine == EngineType::kSampleOneShot || tp.engine == EngineType::kSliceSampler) {
                return std::string("SAMPLE");
            }
            return std::string("WAVE");
        };
        p.label = "WAVE";
        p.value = [](const UiContext& c) -> std::string {
            const auto tp = c.audio.getTrackParams(trk(c, -1));
            if (tp.engine == EngineType::kDrumRack) {
                static const char* kKits[] = {"TR-808", "TR-909", "TR-707", "LINN", "DMX", "CR-78", "SP1200", "TR-606"};
                int k = std::clamp(static_cast<int>(tp.p[0] * 7.99f), 0, 7);
                return std::string(kKits[k]);
            }
            if (tp.engine == EngineType::kDx7) {
                int p_idx = std::clamp(static_cast<int>(tp.p[0] * (c.audio.dx7().patchCount() - 1) + 0.5f), 0, c.audio.dx7().patchCount() - 1);
                return c.audio.dx7().getPatch(p_idx).name;
            }
            if (tp.engine == EngineType::kSampleOneShot || tp.engine == EngineType::kSliceSampler) {
                const auto& buf = c.app.sampler.recordedSample();
                if (buf.loaded) return buf.name;
                char b[16];
                std::snprintf(b, sizeof(b), "SAMPLE #%d", static_cast<int>(tp.p[0] * 7.0f) + 1);
                return std::string(b);
            }
            static const char* kWaveNames[5] = {"SAW", "SQUARE", "TRI", "SINE", "NOISE"};
            int wave_idx = std::clamp(static_cast<int>(tp.p[5] * 4.99f), 0, 4);
            return std::string(kWaveNames[wave_idx]);
        };
        p.norm = [](const UiContext& c) {
            const auto tp = c.audio.getTrackParams(trk(c, -1));
            return (tp.engine == EngineType::kDrumRack || tp.engine == EngineType::kDx7 ||
                    tp.engine == EngineType::kSampleOneShot || tp.engine == EngineType::kSliceSampler) ? tp.p[0] : tp.p[5];
        };
        p.step = [](UiContext& c, int dir, bool) {
            const int t = trk(c, -1);
            auto tp = c.audio.getTrackParams(t);
            if (tp.engine == EngineType::kDrumRack) {
                int cur = std::clamp(static_cast<int>(tp.p[0] * 7.99f), 0, 7);
                int next = std::clamp(cur + dir, 0, 7);
                tp.p[0] = static_cast<float>(next) / 7.0f;
                static const char* kKitDirs[] = {
                    "res/samples/drums/01_tr808",
                    "res/samples/drums/02_tr909",
                    "res/samples/drums/03_tr707",
                    "res/samples/drums/04_linndrum",
                    "res/samples/drums/05_oberheim_dmx",
                    "res/samples/drums/06_cr78",
                    "res/samples/drums/07_sp1200",
                    "res/samples/drums/08_tr606"
                };
                c.audio.sampler().loadDrumKit(kKitDirs[next]);
            } else if (tp.engine == EngineType::kDx7) {
                int total = c.audio.dx7().patchCount();
                if (total > 0) {
                    int cur = std::clamp(static_cast<int>(tp.p[0] * (total - 1) + 0.5f), 0, total - 1);
                    int next = std::clamp(cur + dir, 0, total - 1);
                    tp.p[0] = static_cast<float>(next) / static_cast<float>(total - 1);
                }
            } else if (tp.engine == EngineType::kSampleOneShot || tp.engine == EngineType::kSliceSampler) {
                tp.p[0] = std::clamp(tp.p[0] + dir * (1.0f / 7.0f), 0.0f, 1.0f);
            } else {
                int cur = std::clamp(static_cast<int>(tp.p[5] * 4.99f), 0, 4);
                int next = (cur + dir + 5) % 5;
                tp.p[5] = static_cast<float>(next) / 4.0f;
            }
            c.audio.setTrackParams(t, tp);
        };
        add(std::move(p));
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

    add(trackEnum<LfoWave>("LFO1.WAVE", "L1 WAVE", S, [](TrackParameters& t) -> LfoWave& { return t.lfo1.wave; }, lfoWaveName));
    add(trackFloat("LFO1.RATE", "L1 RATE", S, [](TrackParameters& t) -> float& { return t.lfo1.rate; }, 0.05f, 2.0f, 0.01f, fmtHz));
    add(trackFloat("LFO1.DEPTH", "L1 DEPTH", S, [](TrackParameters& t) -> float& { return t.lfo1.depth; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(trackEnum<StickDest>("LFO1.DEST", "L1 DEST", S, [](TrackParameters& t) -> StickDest& { return t.lfo1.dest; }, destName));
    add(trackEnum<LfoWave>("LFO2.WAVE", "L2 WAVE", S, [](TrackParameters& t) -> LfoWave& { return t.lfo2.wave; }, lfoWaveName));
    add(trackFloat("LFO2.RATE", "L2 RATE", S, [](TrackParameters& t) -> float& { return t.lfo2.rate; }, 0.05f, 2.0f, 0.01f, fmtHz));
    add(trackFloat("LFO2.DEPTH", "L2 DEPTH", S, [](TrackParameters& t) -> float& { return t.lfo2.depth; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(trackEnum<StickDest>("LFO2.DEST", "L2 DEST", S, [](TrackParameters& t) -> StickDest& { return t.lfo2.dest; }, destName));

    add(trackEnum<StickDest>("STICK.LX", "L-STK X", S, [](TrackParameters& t) -> StickDest& { return t.stick_left_x; }, destName));
    add(trackEnum<StickDest>("STICK.LY", "L-STK Y", S, [](TrackParameters& t) -> StickDest& { return t.stick_left_y; }, destName));
    add(trackEnum<StickDest>("STICK.RX", "R-STK X", S, [](TrackParameters& t) -> StickDest& { return t.stick_right_x; }, destName));
    add(trackEnum<StickDest>("STICK.RY", "R-STK Y", S, [](TrackParameters& t) -> StickDest& { return t.stick_right_y; }, destName));

    // ===== TRK.* (обраний трек) та T1..T4.* (фіксований трек для MIX) — рівні та посили =====
    for (int t = -1; t < kTracksCount; ++t) {
        const std::string pre = (t < 0) ? "TRK." : ("T" + std::to_string(t + 1) + ".");
        add(trackFloat(pre + "LVL", "LEVEL", t, [](TrackParameters& x) -> float& { return x.level; }, 0.0f, 1.0f, 0.02f, fmtPct));
        add(trackFloat(pre + "PAN", "PAN", t, [](TrackParameters& x) -> float& { return x.pan; }, -1.0f, 1.0f, 0.05f, fmtPan));
        add(trackFloat(pre + "DLY", "DLY SND", t, [](TrackParameters& x) -> float& { return x.delay_send; }, 0.0f, 1.0f, 0.02f, fmtPct));
        add(trackFloat(pre + "REV", "REV SND", t, [](TrackParameters& x) -> float& { return x.reverb_send; }, 0.0f, 1.0f, 0.02f, fmtPct));
        add(trackMute(pre + "MUTE", t));
    }

    // ===== Глобальні ефекти та майстер =====
    add(globalFloat("DLY.FBK", "DLY FBK", [](SynthParams& s) -> std::atomic<float>& { return s.delay_feedback; }, 0.0f, 0.95f, 0.02f, fmtPct));
    add(globalFloat("DLY.RET", "DLY RET", [](SynthParams& s) -> std::atomic<float>& { return s.delay_mix; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(globalFloat("REV.DECAY", "REV DEC", [](SynthParams& s) -> std::atomic<float>& { return s.reverb_decay; }, 0.0f, 0.95f, 0.02f, fmtPct));
    add(globalFloat("REV.RET", "REV RET", [](SynthParams& s) -> std::atomic<float>& { return s.reverb_mix; }, 0.0f, 1.0f, 0.02f, fmtPct));
    add(globalFloat("FX.CHR_MIX", "CHR MIX", [](SynthParams& s) -> std::atomic<float>& { return s.chorus_mix; }, 0.0f, 1.0f, 0.05f, fmtPct));
    add(globalFloat("FX.CHR_RATE", "CHR RATE", [](SynthParams& s) -> std::atomic<float>& { return s.chorus_rate; }, 0.1f, 5.0f, 0.1f, fmtHz));
    add(globalFloat("FX.CHR_DEP", "CHR DEP", [](SynthParams& s) -> std::atomic<float>& { return s.chorus_depth; }, 0.0f, 1.0f, 0.05f, fmtPct));
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
            c.app.pattern_mgr.selectPattern(b, c.app.pattern_mgr.currentPattern(), false);
            c.app.pattern_mgr.applyCurrent(c.audio, c.app.bpm);
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
            c.app.pattern_mgr.selectPattern(c.app.pattern_mgr.currentBank(), p_idx, false);
            c.app.pattern_mgr.applyCurrent(c.audio, c.app.bpm);
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.SAVE";
        p.label = "SAVE PROJ";
        p.value = [](const UiContext&) { return "PRESS [A]"; };
        p.step = [](UiContext& c, int, bool) {
            c.app.pattern_mgr.saveProject(PatternManager::defaultProjectPath(), c.audio, c.app.bpm);
            c.app.last_action = "PROJECT SAVED";
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SYS.LOAD";
        p.label = "LOAD PROJ";
        p.value = [](const UiContext&) { return "PRESS [A]"; };
        p.step = [](UiContext& c, int, bool) {
            c.app.pattern_mgr.loadProject(PatternManager::defaultProjectPath(), c.audio, c.app.bpm);
            c.app.last_action = "PROJECT LOADED";
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
                case RecState::kFinished: return "DONE";
                default: return "OFF";
            }
        };
        p.step = [](UiContext& c, int, bool) {
            if (c.app.sampler.recordingState() == RecState::kIdle) {
                c.app.sampler.armRecording();
                c.app.last_action = "REC ARMED";
            } else {
                c.app.sampler.stopRecording();
                c.app.last_action = "REC STOPPED";
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
                case RecMode::kSyncBar:   return "SYNC 1BAR";
                default:                  return "MANUAL";
            }
        };
        p.step = [](UiContext& c, int dir, bool) {
            int m = (static_cast<int>(c.app.sampler.recordingMode()) + dir + 3) % 3;
            c.app.sampler.setRecordingMode(static_cast<RecMode>(m));
        };
        add(std::move(p));
    }
    {
        Param p;
        p.id = "SMP.THRS";
        p.label = "THRESH";
        p.value = [](const UiContext&) { return "-18 DB"; };
        p.step = [](UiContext& c, int, bool) { c.app.last_action = "THRESH SET"; };
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
        p.step = [](UiContext& c, int dir, bool) {
            c.app.sample_target_track = (c.app.sample_target_track + dir + 4) % 4;
            c.app.last_action = "ASSIGN TO T" + std::to_string(c.app.sample_target_track + 1);
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
        [](const SequencerStep& s, const TrackParameters&) { return fmtPct(s.gate_len); },
        [](SequencerStep& s, const TrackParameters&, int dir, bool) {
            s.gate_len = std::clamp(s.gate_len + dir * 0.10f, 0.10f, 2.0f);
        }));
    add(stepParam("STEP.SLIDE", "SLIDE",
        [](const SequencerStep& s, const TrackParameters& tp) {
            if (tp.engine == EngineType::kDrumSyn) return std::string("N/A (DRUM)");
            return std::string(s.slide ? "ON" : "OFF");
        },
        [](SequencerStep& s, const TrackParameters& tp, int, bool) {
            if (tp.engine != EngineType::kDrumSyn) s.slide = !s.slide;
        }));
    add(stepParam("STEP.ATK", "ATTACK",
        [](const SequencerStep& s, const TrackParameters& tp) { return fmt127(lockedOr(s.attack, tp.attack)) + (s.attack >= 0 ? " L" : ""); },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            s.attack = std::clamp(lockedOr(s.attack, tp.attack) + stepNorm(dir, coarse), 0.0f, 1.0f);
        }));
    add(stepParam("STEP.DEC", "DECAY",
        [](const SequencerStep& s, const TrackParameters& tp) { return fmt127(lockedOr(s.decay, tp.decay)) + (s.decay >= 0 ? " L" : ""); },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            s.decay = std::clamp(lockedOr(s.decay, tp.decay) + stepNorm(dir, coarse), 0.01f, 1.0f);
        }));
    add(stepParam("STEP.SUS", "SUSTAIN",
        [](const SequencerStep& s, const TrackParameters& tp) { return fmt127(lockedOr(s.sustain, tp.sustain)) + (s.sustain >= 0 ? " L" : ""); },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            s.sustain = std::clamp(lockedOr(s.sustain, tp.sustain) + stepNorm(dir, coarse), 0.0f, 1.0f);
        }));
    add(stepParam("STEP.REL", "RELEASE",
        [](const SequencerStep& s, const TrackParameters& tp) { return fmt127(lockedOr(s.release, tp.release)) + (s.release >= 0 ? " L" : ""); },
        [](SequencerStep& s, const TrackParameters& tp, int dir, bool coarse) {
            s.release = std::clamp(lockedOr(s.release, tp.release) + stepNorm(dir, coarse), 0.01f, 1.0f);
        }));
    add(stepParam("STEP.CUT", "CUT LOCK",
        [](const SequencerStep& s, const TrackParameters&) {
            return s.has_plock ? fmt127(lockedOr(s.plock_cutoff, 0.5f)) + " L" : std::string("---");
        },
        [](SequencerStep& s, const TrackParameters&, int dir, bool coarse) {
            s.has_plock = true;
            s.plock_cutoff = std::clamp(lockedOr(s.plock_cutoff, 0.5f) + stepNorm(dir, coarse), 0.05f, 0.95f);
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
