#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace groovebox {

// Типи синтезаторних движків (легко додавати нові — достатньо дописати enum та масив метаданих)
enum class EngineType : uint8_t {
    kVaMono   = 0, // PolyBLEP Saw/Pulse + Sub Osc
    kFm2Op    = 1, // 2-Operator FM (Ratio, Index, Feedback)
    kAcid303  = 2, // Acid 303 Bassline (Waveform, Cutoff, Reso, EnvMod, Accent)
    kChordPad = 3, // 4-Voice Chord Synth (Chord Type, Detune, Cutoff, Attack/Decay)
    kDrumSyn  = 4, // Analog Drum Synth (Kick/Tom/Zap/Noise Perc)
    kCount    = 5
};

inline constexpr const char* getEngineTypeName(EngineType type) {
    switch (type) {
        case EngineType::kVaMono:   return "VA SAW+SUB";
        case EngineType::kFm2Op:    return "2-OP FM";
        case EngineType::kAcid303:  return "ACID 303";
        case EngineType::kChordPad: return "CHORD PAD";
        case EngineType::kDrumSyn:  return "ANALOG DRUM";
        default:                    return "UNKNOWN";
    }
}

// Кожен движок має 6 уніфікованих нормалізованих параметрів (0.0f .. 1.0f),
// назви яких автоматично підтягуються в інтерфейс (нуль хардкоду в UI!)
inline constexpr int kParamsPerTrack = 6;
inline constexpr int kTotalRows = 16;         // 4 доріжки x 4 голоси
inline constexpr int kTracksCount = 4;
inline constexpr int kVoicesPerTrack = 4;
inline constexpr uint32_t kMaxSteps = 128;
inline constexpr uint32_t kStepsPerPage = 64;

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
                    stage = AdsrStage::kSustain;
                }
                break;
            case AdsrStage::kSustain:
                value = sustain_level;
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

struct TrackParameters {
    EngineType engine = EngineType::kVaMono;
    std::array<float, kParamsPerTrack> p{0.55f, 0.35f, 0.50f, 0.35f, 0.30f, 0.40f};
    float attack = 0.02f;
    float decay = 0.40f;
    float sustain = 0.65f;
    float release = 0.30f;
    float level = 0.85f;
    float pan = 0.0f;       // -1.0 (L) .. +1.0 (R)
    float delay_send = 0.2f;
    bool muted = false;
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
    // kDrumSyn
    {"TUNE", "SWEEP", "DECAY", "NOISE", "DRIVE", "CLICK"}
};

inline constexpr const char* getParamName(EngineType type, int param_idx) {
    int e = std::clamp(static_cast<int>(type), 0, static_cast<int>(EngineType::kCount) - 1);
    int idx = std::clamp(param_idx, 0, kParamsPerTrack - 1);
    return kParamNames[e][idx];
}

// Крок 64/128-крокового секвенсора з підтримкою Elektron Trig моделі (Note Trig vs Yellow Param Trig)
struct SequencerStep {
    bool enabled = false;        // Чи активний крок
    bool is_param_trig = false;  // Жовтий крок Elektron: НЕ тригерить ноту/MIDI, тільки змінює параметри звуку!
    uint8_t note = 48;           // MIDI нота (0..127)
    float velocity = 0.90f;      // 0.0 .. 1.0
    bool has_plock = false;      // Чи зафіксовано P-Lock
    float plock_cutoff = 0.5f;   // P-Lock фільтра
    float plock_val = 0.5f;      // P-Lock додаткового параметра
};

} // namespace groovebox
