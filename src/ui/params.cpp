#include "ui/params.hpp"

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
    p.value = [=](const UiContext& c) { return fmt127(c.audio.getTrackParams(trk(c, kSelectedTrack)).p[idx]); };
    p.norm = [=](const UiContext& c) { return c.audio.getTrackParams(trk(c, kSelectedTrack)).p[idx]; };
    p.step = [=](UiContext& c, int dir, bool coarse) {
        c.audio.adjustTrackParam(trk(c, kSelectedTrack), idx, dir * (coarse ? 0.10f : 0.02f));
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
    add(stepParam("STEP.NOTE", "NOTE",
        [](const SequencerStep& s, const TrackParameters&) { return noteName(s.note) + " (" + std::to_string(s.note) + ")"; },
        [](SequencerStep& s, const TrackParameters&, int dir, bool coarse) {
            s.note = static_cast<uint8_t>(std::clamp(s.note + dir * (coarse ? 12 : 1), 0, 127));
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
}

} // namespace groovebox
