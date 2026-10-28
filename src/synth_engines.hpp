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

enum class StickDest : uint8_t {
    kCutoff = 0,
    kResonance,
    kEnvMod,
    kFmIndex,
    kHpfCutoff,
    kLfo1Rate,
    kLfo1Depth,
    kLfo2Rate,
    kLfo2Depth,
    kDelaySend,
    kDelayTime,
    kDelayFeedback,
    kReverbSend,
    kPan,
    kLevel,
    kCount
};

inline constexpr const char* getStickDestShortName(StickDest dest) {
    switch (dest) {
        case StickDest::kCutoff:        return "CUT";
        case StickDest::kResonance:     return "RES";
        case StickDest::kEnvMod:        return "ENV";
        case StickDest::kFmIndex:       return "FM";
        case StickDest::kHpfCutoff:     return "HPF";
        case StickDest::kLfo1Rate:      return "MOD1";
        case StickDest::kLfo1Depth:     return "DEP1";
        case StickDest::kLfo2Rate:      return "MOD2";
        case StickDest::kLfo2Depth:     return "DEP2";
        case StickDest::kDelaySend:     return "D.SND";
        case StickDest::kDelayTime:     return "D.TIM";
        case StickDest::kDelayFeedback: return "D.FBK";
        case StickDest::kReverbSend:    return "REV";
        case StickDest::kPan:           return "PAN";
        case StickDest::kLevel:         return "VOL";
        default:                        return "OFF";
    }
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
    StickDest dest = StickDest::kCutoff;
    LfoMode mode = LfoMode::kTrig;
    uint8_t multiplier = 1;   // 1, 2, 4, 8, 16, 32, 64 (Elektron Digitakt style)
    float phase = 0.0f;
    float value = 0.0f;
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
    StickDest stick_left_x = StickDest::kCutoff;
    StickDest stick_left_y = StickDest::kResonance;
    StickDest stick_right_x = StickDest::kLfo1Rate;
    StickDest stick_right_y = StickDest::kLfo2Rate;
    LfoParameters lfo1{.wave = LfoWave::kTriangle, .rate = 0.25f, .depth = 0.0f, .dest = StickDest::kCutoff, .mode = LfoMode::kTrig, .multiplier = 1};
    LfoParameters lfo2{.wave = LfoWave::kSine, .rate = 0.40f, .depth = 0.0f, .dest = StickDest::kResonance, .mode = LfoMode::kFree, .multiplier = 1};
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
