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

struct TrackParameters {
    EngineType engine = EngineType::kVaMono;
    std::array<float, kParamsPerTrack> p{0.55f, 0.35f, 0.50f, 0.35f, 0.30f, 0.40f};
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

// Крок 16-крокового секвенсора з підтримкою Parameter Locks (як у Elektron Digitakt)
struct SequencerStep {
    bool enabled = false;
    uint8_t note = 48;          // MIDI нота (0..127)
    float velocity = 0.90f;     // 0.0 .. 1.0
    bool has_plock = false;     // Чи зафіксовано P-Lock фільтра на цьому кроці
    float plock_cutoff = 0.5f;  // Значення P-Lock для Cutoff
};

} // namespace groovebox
