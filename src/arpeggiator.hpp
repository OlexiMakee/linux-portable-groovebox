#pragma once

#include "synth_engines.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace groovebox {

class AudioEngine;

// Музичні лади для квантування нот
enum class ScaleType : uint8_t {
    kChromatic = 0,
    kMajor,          // Ionian
    kMinor,          // Natural Minor / Aeolian
    kHarmonicMinor,
    kDorian,
    kPhrygian,
    kLydian,
    kMixolydian,
    kPentatonicMinor,
    kPentatonicMajor,
    kBlues,
    kCount
};

const char* getScaleName(ScaleType scale);
int quantizeToScale(int midi_note, int root_note, ScaleType scale);

// Швидкість / дільник Arp 1 (довгі гармонічні ноти / акорди)
enum class Arp1Division : uint8_t {
    kDiv1_1 = 0, // 16 кроків (ціла нота)
    kDiv1_2,     // 8 кроків (половинна)
    kDiv1_4,     // 4 кроки (четвертна)
    kDiv1_8,     // 2 кроки (восьма)
    kCount
};

const char* getArp1DivisionName(Arp1Division div);
int getArp1StepLength(Arp1Division div);

// Типи акордів для Arp 1
enum class ChordType : uint8_t {
    kRootOnly = 0, // Тільки тоніка (Unison)
    kOctaves,      // Октави (Root + Oct)
    kFifth,        // Power chord (Root + 5th)
    kTriad,        // Трезвуччя в ладі (Root + 3rd + 5th)
    kSeventh,      // Септакорд у ладі (Root + 3rd + 5th + 7th)
    kSus4,         // Sus4 (Root + 4th + 5th)
    kAdd9,         // Add9 (Root + 3rd + 5th + 9th)
    kCount
};

const char* getChordTypeName(ChordType chord);
std::vector<int> getChordIntervals(ChordType chord, ScaleType scale);

// Швидкість / дільник Arp 2 (швидкі мелодійні ноти)
enum class Arp2Division : uint8_t {
    kDiv1_4 = 0, // 4 кроки
    kDiv1_8,     // 2 кроки
    kDiv1_16,    // 1 крок (шістнадцята)
    kDiv1_32,    // 1/2 кроку (тридцять друга)
    kDiv1_16T,   // Тріолі
    kCount
};

const char* getArp2DivisionName(Arp2Division div);

// Напрямки та алгоритми Arp 2
enum class ArpPattern : uint8_t {
    kUp = 0,
    kDown,
    kUpDown,
    kPingPong,
    kConverge,    // З країв до центру
    kDiverge,     // З центру до країв
    kRandom,      // Випадковий вибір
    kBrownianWalk,// Броунівський рух (+1, 0, -1 ступінь)
    kEuclidean,   // Евклідовий ритм
    kCount
};

const char* getArpPatternName(ArpPattern pattern);

// Повна конфігурація двох послідовних арпеджираторів
struct ArpConfig {
    bool enabled = false;
    ScaleType scale = ScaleType::kMinor;
    int root_note = 48; // C3 (48), 36..72

    // --- Arp 1: Гармонічний / Акордовий макро-генератор ---
    Arp1Division arp1_div = Arp1Division::kDiv1_2; // 8 кроків
    ChordType chord_type = ChordType::kTriad;
    int progression_id = 0; // 0=Static, 1=I-VI-III-VII, 2=I-IV-V-I, 3=i-iv-v-i
    float arp1_gate = 0.90f; // Довгі ноти / педи

    // --- Arp 2: Мелодійний процедурний мікро-генератор ---
    Arp2Division arp2_div = Arp2Division::kDiv1_16; // 1 крок (1/16)
    ArpPattern pattern = ArpPattern::kUpDown;
    int octaves = 2;       // 1..3 октави
    float arp2_gate = 0.60f; // Чіткі короткі ноти
    bool slide_auto = false; // Автоматичний TB-303 слайд на перепадах нот

    // --- Алгоритмічний блок (Euclid & Turing Machine) ---
    int euclid_hits = 11;  // Евклідові удари (1..16)
    int euclid_length = 16;// Довжина евклідового циклу
    int mutation_pct = 0;  // 0% = точний луп, 100% = хаотична мутація
    uint32_t seed = 42;    // Фіксований сідувач для детермінізму
};

class Arpeggiator {
public:
    Arpeggiator();

    const ArpConfig& config() const { return config_; }
    ArpConfig& config() { return config_; }
    void setConfig(const ArpConfig& cfg) { config_ = cfg; }

    // Генерація списку кроків для секвенсора
    std::vector<SequencerStep> generateSteps(int count = 16);

    // Запікання згенерованої мелодії безпосередньо в доріжку аудіорушія ("Commit to Sequencer")
    bool commitToTrack(AudioEngine& engine, int row, int start_step = 0, int count = 16);

    // Алгоритмічні утиліти
    static std::vector<bool> generateEuclideanRhythm(int hits, int total_steps);

private:
    ArpConfig config_;
    uint32_t turing_register_ = 0xACE1u; // 16-бітний LFSR
};

} // namespace groovebox
