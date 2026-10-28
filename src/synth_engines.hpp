#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace groovebox {

// Типи синтезаторних та семплерних движків (лише чисті семплери та синтезатори, нуль драм-реків)
enum class EngineType : uint8_t {
    kVaMono        = 0, // PolyBLEP Saw/Pulse + Sub Osc
    kFm2Op         = 1, // 2-Operator FM (Ratio, Index, Feedback)
    kAcid303       = 2, // Acid 303 Bassline (Waveform, Cutoff, Reso, EnvMod, Accent)
    kChordPad      = 3, // 4-Voice Chord Synth (Chord Type, Detune, Cutoff, Attack/Decay)
    kSampleOneShot = 4, // Універсальний семплер моно/стерео семплів з пулу
    kSliceSampler  = 5, // Loop Slicer Sampler with P-Lock Slices
    kDx7           = 6, // Yamaha DX7 6-Operator FM Synthesizer (ROM1A + SysEx)
    kMidiOut       = 7, // External MIDI Gear Sequencer Track
    kCount         = 8,
    kDrumRack      = 254, // Псевдонім сумісності для поетапного рефакторингу
    kDrumSyn       = 255  // Псевдонім сумісності для поетапного рефакторингу
};

inline constexpr EngineType kSampleDrums = EngineType::kSampleOneShot;
inline constexpr EngineType kDrumSyn = EngineType::kSampleOneShot;

inline constexpr const char* getEngineTypeName(EngineType type) {
    switch (type) {
        case EngineType::kVaMono:        return "VA SAW+SUB";
        case EngineType::kFm2Op:         return "2-OP FM";
        case EngineType::kAcid303:       return "ACID 303";
        case EngineType::kChordPad:      return "CHORD PAD";
        case EngineType::kSampleOneShot: return "SAMPLER";
        case EngineType::kSliceSampler:  return "LOOP SLICER";
        case EngineType::kDx7:           return "DX7 6-OP";
        case EngineType::kMidiOut:       return "MIDI OUT";
        default:                         return "UNKNOWN";
    }
}

// Кожен движок має 6 уніфікованих нормалізованих параметрів (0.0f .. 1.0f),
// назви яких автоматично підтягуються в інтерфейс (нуль хардкоду в UI!)
inline constexpr int kParamsPerTrack = 6;
inline constexpr int kTotalRows = 16;
inline constexpr int kMaxTracks = 16;
inline constexpr int kTracksCount = 16;
inline constexpr int kVoicesPerTrack = 4;
inline constexpr int kTotalVoices = kMaxTracks * kVoicesPerTrack;
inline constexpr uint32_t kMaxSteps = 256;
inline constexpr uint32_t kStepsPerPage = 32; // 32 видимих кроки на сторінку (велика читабельна сітка 7px)
inline constexpr uint32_t kMaxPages = 8;      // До 8 сторінок: P1..P8 (до 256 кроків)

enum class AdsrStage : uint8_t {
    kIdle = 0,
    kAttack,
    kDecay,
    kSustain,
    kRelease
};

struct AdsrEnvelope {
    AdsrStage stage = AdsrStage::kIdle;
    float value = 0.0f;
    float attack_rate = 0.01f;
    float decay_rate = 0.999f;
    float sustain_level = 0.70f;
    float release_rate = 0.995f;

    void trigger(float attack_norm, float decay_norm, float sustain_norm, float sample_rate) {
        stage = AdsrStage::kAttack;
        float att_sec = 0.002f + attack_norm * 0.80f;
        attack_rate = 1.0f / (att_sec * sample_rate);
        float dec_sec = 0.010f + decay_norm * 1.50f;
        decay_rate = std::exp(-1.0f / (dec_sec * sample_rate));
        sustain_level = std::clamp(sustain_norm, 0.0f, 1.0f);
        value = 0.0f;
    }

    void release(float release_norm, float sample_rate) {
        if (stage == AdsrStage::kIdle) return;
        stage = AdsrStage::kRelease;
        float rel_sec = 0.010f + release_norm * 1.80f;
        release_rate = std::exp(-1.0f / (rel_sec * sample_rate));
    }

    void reset() {
        stage = AdsrStage::kIdle;
        value = 0.0f;
    }

    float process() {
        switch (stage) {
            case AdsrStage::kAttack:
                value += attack_rate;
                if (value >= 1.0f) {
                    value = 1.0f;
                    stage = AdsrStage::kDecay;
                }
                break;
            case AdsrStage::kDecay:
                value = sustain_level + (value - sustain_level) * decay_rate;
                if (std::abs(value - sustain_level) < 0.002f) {
                    value = sustain_level;
                    if (sustain_level <= 0.001f) {
                        stage = AdsrStage::kIdle;
                        value = 0.0f;
                    } else {
                        stage = AdsrStage::kSustain;
                    }
                }
                break;
            case AdsrStage::kSustain:
                if (sustain_level <= 0.001f) {
                    stage = AdsrStage::kIdle;
                    value = 0.0f;
                } else {
                    value = sustain_level;
                }
                break;
            case AdsrStage::kRelease:
                value *= release_rate;
                if (value <= 0.001f) {
                    value = 0.0f;
                    stage = AdsrStage::kIdle;
                }
                break;
            case AdsrStage::kIdle:
            default:
                value = 0.0f;
                break;
        }
        return value;
    }

    bool isActive() const { return stage != AdsrStage::kIdle; }
};

enum class ModTarget : uint8_t {
    kNone = 0,
    kCutoff,        // LPF Cutoff
    kResonance,     // LPF Resonance
    kEnvMod,        // LPF Env Mod
    kLpfDecay,      // LPF Decay
    kHpfCutoff,     // HPF Cutoff
    kHpfResonance,  // HPF Resonance
    kHpfEnv,        // HPF Env Mod
    kHpfDecay,      // HPF Decay
    kGainOver,      // Gain / Overdrive
    kPitch,         // Pitch / Tune
    kGlide,         // Glide Time
    kParam1,        // Param 1 (Macro / Cutoff / Presets)
    kParam2,        // Param 2
    kParam3,        // Param 3
    kParam4,        // Param 4
    kParam5,        // Param 5
    kParam6,        // Param 6
    kAmpAttack,     // Amp Attack
    kAmpDecay,      // Amp Decay
    kAmpSustain,    // Amp Sustain
    kAmpRelease,    // Amp Release
    kPan,           // Pan
    kLevel,         // Volume Level
    kDelaySend,     // Delay Send
    kReverbSend,    // Reverb Send
    kLfo1Rate,      // LFO1 Rate
    kLfo1Depth,     // LFO1 Depth
    kLfo2Rate,      // LFO2 Rate
    kLfo2Depth,     // LFO2 Depth
    kLfo3Rate,      // LFO3 Rate
    kLfo3Depth,     // LFO3 Depth
    kLfo4Rate,      // LFO4 Rate
    kLfo4Depth,     // LFO4 Depth
    kEnv1Attack,    // Env1 Attack
    kEnv1Decay,     // Env1 Decay
    kEnv1Sustain,   // Env1 Sustain
    kEnv1Release,   // Env1 Release
    kEnv2Attack,    // Env2 Attack
    kEnv2Decay,     // Env2 Decay
    kEnv2Sustain,   // Env2 Sustain
    kEnv2Release,   // Env2 Release
    kCount
};

using StickDest = ModTarget;

inline constexpr const char* getModTargetShortName(ModTarget target) {
    switch (target) {
        case ModTarget::kCutoff:       return "CUT";
        case ModTarget::kResonance:    return "RES";
        case ModTarget::kEnvMod:       return "ENV";
        case ModTarget::kLpfDecay:     return "L.DEC";
        case ModTarget::kHpfCutoff:    return "HPF";
        case ModTarget::kHpfResonance: return "H.RES";
        case ModTarget::kHpfEnv:       return "H.ENV";
        case ModTarget::kHpfDecay:     return "H.DEC";
        case ModTarget::kGainOver:     return "GAIN";
        case ModTarget::kPitch:        return "PTCH";
        case ModTarget::kGlide:        return "GLID";
        case ModTarget::kParam1:       return "P1";
        case ModTarget::kParam2:       return "P2";
        case ModTarget::kParam3:       return "P3";
        case ModTarget::kParam4:       return "P4";
        case ModTarget::kParam5:       return "P5";
        case ModTarget::kParam6:       return "P6";
        case ModTarget::kAmpAttack:    return "ATK";
        case ModTarget::kAmpDecay:     return "DEC";
        case ModTarget::kAmpSustain:   return "SUS";
        case ModTarget::kAmpRelease:   return "REL";
        case ModTarget::kPan:          return "PAN";
        case ModTarget::kLevel:        return "VOL";
        case ModTarget::kDelaySend:    return "D.SND";
        case ModTarget::kReverbSend:   return "REV";
        case ModTarget::kLfo1Rate:     return "L1.RTE";
        case ModTarget::kLfo1Depth:    return "L1.DEP";
        case ModTarget::kLfo2Rate:     return "L2.RTE";
        case ModTarget::kLfo2Depth:    return "L2.DEP";
        case ModTarget::kLfo3Rate:     return "L3.RTE";
        case ModTarget::kLfo3Depth:    return "L3.DEP";
        case ModTarget::kLfo4Rate:     return "L4.RTE";
        case ModTarget::kLfo4Depth:    return "L4.DEP";
        case ModTarget::kEnv1Attack:   return "E1.ATK";
        case ModTarget::kEnv1Decay:    return "E1.DEC";
        case ModTarget::kEnv1Sustain:  return "E1.SUS";
        case ModTarget::kEnv1Release:  return "E1.REL";
        case ModTarget::kEnv2Attack:   return "E2.ATK";
        case ModTarget::kEnv2Decay:    return "E2.DEC";
        case ModTarget::kEnv2Sustain:  return "E2.SUS";
        case ModTarget::kEnv2Release:  return "E2.REL";
        default:                       return "OFF";
    }
}

inline constexpr const char* getModTargetLongName(ModTarget target) {
    switch (target) {
        case ModTarget::kCutoff:       return "LPF Cutoff";
        case ModTarget::kResonance:    return "LPF Resonance";
        case ModTarget::kEnvMod:       return "LPF Env Mod";
        case ModTarget::kLpfDecay:     return "LPF Decay";
        case ModTarget::kHpfCutoff:    return "HPF Cutoff";
        case ModTarget::kHpfResonance: return "HPF Resonance";
        case ModTarget::kHpfEnv:       return "HPF Env Mod";
        case ModTarget::kHpfDecay:     return "HPF Decay";
        case ModTarget::kGainOver:     return "Gain / Overdrive";
        case ModTarget::kPitch:        return "Pitch / Tune";
        case ModTarget::kGlide:        return "Glide Time";
        case ModTarget::kParam1:       return "Param 1 (Macro)";
        case ModTarget::kParam2:       return "Param 2";
        case ModTarget::kParam3:       return "Param 3";
        case ModTarget::kParam4:       return "Param 4";
        case ModTarget::kParam5:       return "Param 5";
        case ModTarget::kParam6:       return "Param 6";
        case ModTarget::kAmpAttack:    return "Amp Attack";
        case ModTarget::kAmpDecay:     return "Amp Decay";
        case ModTarget::kAmpSustain:   return "Amp Sustain";
        case ModTarget::kAmpRelease:   return "Amp Release";
        case ModTarget::kPan:          return "Track Pan";
        case ModTarget::kLevel:        return "Track Level";
        case ModTarget::kDelaySend:    return "Delay Send";
        case ModTarget::kReverbSend:   return "Reverb Send";
        case ModTarget::kLfo1Rate:     return "LFO 1 Rate";
        case ModTarget::kLfo1Depth:    return "LFO 1 Depth";
        case ModTarget::kLfo2Rate:     return "LFO 2 Rate";
        case ModTarget::kLfo2Depth:    return "LFO 2 Depth";
        case ModTarget::kLfo3Rate:     return "LFO 3 Rate";
        case ModTarget::kLfo3Depth:    return "LFO 3 Depth";
        case ModTarget::kLfo4Rate:     return "LFO 4 Rate";
        case ModTarget::kLfo4Depth:    return "LFO 4 Depth";
        case ModTarget::kEnv1Attack:   return "Env 1 Attack";
        case ModTarget::kEnv1Decay:    return "Env 1 Decay";
        case ModTarget::kEnv1Sustain:  return "Env 1 Sustain";
        case ModTarget::kEnv1Release:  return "Env 1 Release";
        case ModTarget::kEnv2Attack:   return "Env 2 Attack";
        case ModTarget::kEnv2Decay:    return "Env 2 Decay";
        case ModTarget::kEnv2Sustain:  return "Env 2 Sustain";
        case ModTarget::kEnv2Release:  return "Env 2 Release";
        default:                       return "Off (None)";
    }
}

inline constexpr const char* getStickDestShortName(ModTarget dest) {
    return getModTargetShortName(dest);
}

enum class LfoWave : uint8_t {
    kTriangle = 0,
    kSine,
    kSaw,
    kSquare,
    kRandomSnh,
    kCount
};

inline constexpr const char* getLfoWaveName(LfoWave wave) {
    switch (wave) {
        case LfoWave::kTriangle:  return "TRI";
        case LfoWave::kSine:      return "SIN";
        case LfoWave::kSaw:       return "SAW";
        case LfoWave::kSquare:    return "SQR";
        case LfoWave::kRandomSnh: return "S&H";
        default:                  return "TRI";
    }
}

enum class LfoMode : uint8_t {
    kTrig = 0,
    kFree = 1,
    kCount
};

inline constexpr const char* getLfoModeName(LfoMode mode) {
    switch (mode) {
        case LfoMode::kFree: return "FREE";
        default:             return "TRIG";
    }
}

struct LfoParameters {
    LfoWave wave = LfoWave::kTriangle;
    float rate = 0.25f;        // 0.0 .. 1.0 (пропорційна шкала 1..128)
    float depth = 0.0f;       // 0.0 .. 1.0
    ModTarget dest = ModTarget::kCutoff;
    LfoMode mode = LfoMode::kTrig;
    uint8_t multiplier = 1;   // 1, 2, 4, 8, 16, 32, 64 (Elektron Digitakt style)
    float phase = 0.0f;
    float value = 0.0f;
};

struct ModEnvParameters {
    float attack = 0.01f;   // 10ms
    float decay = 0.30f;    // 300ms
    float sustain = 0.0f;   // 0.0 .. 1.0
    float release = 0.10f;  // 100ms
    float amount = 0.0f;    // -1.0 .. +1.0 (bipolar depth)
    ModTarget dest = ModTarget::kNone;
};

struct StickAxisConfig {
    ModTarget dest_x = ModTarget::kNone;
    float amount_x = 0.50f; // 0.0 .. 1.0 (0% .. 100%)
    ModTarget dest_y = ModTarget::kNone;
    float amount_y = 0.50f; // 0.0 .. 1.0 (0% .. 100%)
};

struct TrackParameters {
    char name[16] = "";
    EngineType engine = EngineType::kVaMono;
    std::array<float, kParamsPerTrack> p{0.55f, 0.35f, 0.50f, 0.35f, 0.30f, 0.40f};
    float attack = 0.005f;  // 5ms
    float decay = 0.28f;    // 280ms
    float sustain = 0.0f;   // 0% sustain by default (no infinite drone!)
    float release = 0.05f;  // 50ms minimal release
    float level = 0.85f;
    float gain_over = 0.0f; // -1.0 .. +1.0 (-24 dB .. +24 dB, 0.0 = 0 dB clean normalized max)
    float pan = 0.0f;       // -1.0 (L) .. +1.0 (R)
    float delay_send = 0.0f;
    float reverb_send = 0.0f;
    float lpf_cutoff = 1.0f;     // 0.0 .. 1.0 (20 Hz .. 20 kHz, дефолт 1.0 = відкритий)
    float lpf_resonance = 0.15f; // 0.0 .. 0.95 (Q)
    float lpf_env = 0.0f;        // -1.0 .. +1.0 (біполярна огинаюча, дефолт 0.0)
    float lpf_decay = 0.35f;     // 0.01 .. 4.0s (час спаду огинаючої фільтра)
    float hpf_cutoff = 0.0f;     // 0.0 .. 1.0 (20 Hz .. 16 kHz, дефолт 0.0 = low-cut OFF)
    float hpf_resonance = 0.0f;  // 0.0 .. 0.95
    float hpf_env = 0.0f;        // -1.0 .. +1.0 (біполярна огинаюча, дефолт 0.0)
    float hpf_decay = 0.35f;     // 0.01 .. 4.0s (час спаду огинаючої фільтра)
    bool muted = false;
    int transpose = 0;      // -24 .. +24 півтонів (транспозиція треку)
    float glide = 0.065f;   // 5..500 мс час легато-слайду (TB-303 класика = 65 мс)

    // Конфігурація аналогових стіків (Mode 1 & Mode 2 для обох стіків)
    StickAxisConfig stick_l_m1{ModTarget::kCutoff, 0.50f, ModTarget::kResonance, 0.50f};
    StickAxisConfig stick_l_m2{ModTarget::kEnvMod, 0.50f, ModTarget::kHpfCutoff, 0.50f};
    StickAxisConfig stick_r_m1{ModTarget::kLfo1Rate, 0.50f, ModTarget::kLfo1Depth, 0.50f};
    StickAxisConfig stick_r_m2{ModTarget::kLfo2Rate, 0.50f, ModTarget::kLfo2Depth, 0.50f};

    // Сумісність для існуючого коду
    ModTarget stick_left_x = ModTarget::kCutoff;
    ModTarget stick_left_y = ModTarget::kResonance;
    ModTarget stick_right_x = ModTarget::kLfo1Rate;
    ModTarget stick_right_y = ModTarget::kLfo2Rate;

    // 4 призначувані LFO на трек
    LfoParameters lfo1{.wave = LfoWave::kTriangle, .rate = 0.25f, .depth = 0.0f, .dest = ModTarget::kCutoff, .mode = LfoMode::kTrig, .multiplier = 1};
    LfoParameters lfo2{.wave = LfoWave::kSine, .rate = 0.40f, .depth = 0.0f, .dest = ModTarget::kResonance, .mode = LfoMode::kFree, .multiplier = 1};
    LfoParameters lfo3{.wave = LfoWave::kSaw, .rate = 0.50f, .depth = 0.0f, .dest = ModTarget::kNone, .mode = LfoMode::kTrig, .multiplier = 1};
    LfoParameters lfo4{.wave = LfoWave::kSquare, .rate = 0.20f, .depth = 0.0f, .dest = ModTarget::kNone, .mode = LfoMode::kFree, .multiplier = 1};

    // 2 призначувані ADSR огинаючі на трек
    ModEnvParameters env1{.attack = 0.01f, .decay = 0.30f, .sustain = 0.0f, .release = 0.10f, .amount = 0.0f, .dest = ModTarget::kNone};
    ModEnvParameters env2{.attack = 0.01f, .decay = 0.25f, .sustain = 0.2f, .release = 0.15f, .amount = 0.0f, .dest = ModTarget::kNone};
};

inline constexpr const char* kParamNames[static_cast<int>(EngineType::kCount)][kParamsPerTrack] = {
    // kVaMono
    {"CUTOFF", "RESO", "ENV MOD", "DECAY", "SUB LVL", "PULSE W"},
    // kFm2Op
    {"CUTOFF", "FM IDX", "RATIO", "DECAY", "FEEDBCK", "MOD DEC"},
    // kAcid303
    {"CUTOFF", "RESO", "ENV MOD", "DECAY", "ACCENT", "WAVEFRM"},
    // kChordPad
    {"CUTOFF", "RESO", "CHORD", "DECAY", "DETUNE", "ENV MOD"},
    // kSampleOneShot (SAMPLER)
    {"SAMPLE", "TUNE", "DECAY", "START", "DRIVE", "REVERSE"},
    // kSliceSampler
    {"SAMPLE", "SLICE", "TUNE", "DECAY", "FILTER", "REVERSE"},
    // kDx7
    {"PRESET", "DETUNE", "BRIGHT", "DECAY", "ALGO", "FEEDBCK"},
    // kMidiOut
    {"CHANNEL", "CC A", "CC B", "CC C", "CC D", "PROGRAM"}
};

inline constexpr const char* getParamName(EngineType type, int param_idx) {
    int e = std::clamp(static_cast<int>(type), 0, static_cast<int>(EngineType::kCount) - 1);
    int idx = std::clamp(param_idx, 0, kParamsPerTrack - 1);
    return kParamNames[e][idx];
}

// Elektron Trig Conditions (умовні тригери кроку секвенсора)
enum class TrigCondition : uint8_t {
    kNone = 0,   // Безумовне спрацьовування (за probability)
    kFill,       // Тільки в режимі FILL (утримання L3)
    kNotFill,    // Завжди, КРІМ режиму FILL (/FILL)
    kPre,        // PRE: якщо попередній крок цієї доріжки зіграв
    kNotPre,     // /PRE: якщо попередній крок не зіграв
    k1_2,        // 1:2 (1-й з 2 циклів)
    k2_2,        // 2:2 (2-й з 2 циклів)
    k1_4,        // 1:4 (1-й з 4 циклів)
    k2_4,        // 2:4 (2-й з 4 циклів)
    k3_4,        // 3:4 (3-й з 4 циклів)
    k4_4,        // 4:4 (4-й з 4 циклів)
    kFirst,      // FIRST: тільки найперший цикл після старту
    kNotFirst,   // /FIRST: усі цикли, крім найпершого
    kCount
};

inline constexpr const char* getConditionName(TrigCondition cond) {
    switch (cond) {
        case TrigCondition::kNone:     return "---";
        case TrigCondition::kFill:     return "FILL";
        case TrigCondition::kNotFill:  return "/FILL";
        case TrigCondition::kPre:      return "PRE";
        case TrigCondition::kNotPre:   return "/PRE";
        case TrigCondition::k1_2:      return "1:2";
        case TrigCondition::k2_2:      return "2:2";
        case TrigCondition::k1_4:      return "1:4";
        case TrigCondition::k2_4:      return "2:4";
        case TrigCondition::k3_4:      return "3:4";
        case TrigCondition::k4_4:      return "4:4";
        case TrigCondition::kFirst:    return "1ST";
        case TrigCondition::kNotFirst: return "/1ST";
        default: return "---";
    }
}

// Крок 64/128-крокового секвенсора з підтримкою Elektron Trig моделі та поліфонії акордів
struct SequencerStep {
    bool enabled = false;        // Чи активний крок
    bool is_param_trig = false;  // Жовтий крок Elektron: НЕ тригерить ноту/MIDI, тільки змінює параметри звуку!
    bool slide = false;          // TB-303 / Elektron Legato Slide (65ms constant-time RC glide)
    uint8_t note = 48;           // MIDI нота 1 (базова нота акорду 0..127)
    std::array<uint8_t, 4> chord_notes = {48, 255, 255, 255}; // До 4 нот у акорді (255 = вимкнено)
    float velocity = 0.90f;      // 0.0 .. 1.0 (єдине velocity для всіх нот акорду)
    float gate_len = 1.0f;       // 1 .. 128 кроків (дискретна довжина у кроках секвенсора)
    uint8_t probability = 100;   // Elektron Probability: 0% .. 100%
    TrigCondition condition = TrigCondition::kNone; // Elektron Trig Condition
    bool has_plock = false;      // Чи зафіксовано P-Lock
    float plock_cutoff = -1.0f;  // P-Lock фільтра (-1 = дефолт доріжки)
    float plock_val = -1.0f;     // P-Lock додаткового параметра (-1 = дефолт)
    float attack = -1.0f;        // P-Lock Attack (-1 = дефолт доріжки)
    float decay = -1.0f;         // P-Lock Decay (-1 = дефолт доріжки)
    float sustain = -1.0f;       // P-Lock Sustain (-1 = дефолт доріжки)
    float release = -1.0f;       // P-Lock Release (-1 = дефолт доріжки)
};

} // namespace groovebox
