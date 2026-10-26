#include "arpeggiator.hpp"
#include "audio_engine.hpp"

#include <algorithm>
#include <cmath>

namespace groovebox {

namespace {

// Інтервали ладів відносно тоніки (0..11)
const std::vector<int>& getScaleIntervals(ScaleType scale) {
    static const std::vector<int> kChromatic = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static const std::vector<int> kMajor = {0, 2, 4, 5, 7, 9, 11};
    static const std::vector<int> kMinor = {0, 2, 3, 5, 7, 8, 10};
    static const std::vector<int> kHarmonicMinor = {0, 2, 3, 5, 7, 8, 11};
    static const std::vector<int> kDorian = {0, 2, 3, 5, 7, 9, 10};
    static const std::vector<int> kPhrygian = {0, 1, 3, 5, 7, 8, 10};
    static const std::vector<int> kLydian = {0, 2, 4, 6, 7, 9, 11};
    static const std::vector<int> kMixolydian = {0, 2, 4, 5, 7, 9, 10};
    static const std::vector<int> kPentatonicMinor = {0, 3, 5, 7, 10};
    static const std::vector<int> kPentatonicMajor = {0, 2, 4, 7, 9};
    static const std::vector<int> kBlues = {0, 3, 5, 6, 7, 10};

    switch (scale) {
        case ScaleType::kMajor:           return kMajor;
        case ScaleType::kMinor:           return kMinor;
        case ScaleType::kHarmonicMinor:   return kHarmonicMinor;
        case ScaleType::kDorian:          return kDorian;
        case ScaleType::kPhrygian:        return kPhrygian;
        case ScaleType::kLydian:          return kLydian;
        case ScaleType::kMixolydian:      return kMixolydian;
        case ScaleType::kPentatonicMinor: return kPentatonicMinor;
        case ScaleType::kPentatonicMajor: return kPentatonicMajor;
        case ScaleType::kBlues:           return kBlues;
        case ScaleType::kChromatic:
        default:                          return kChromatic;
    }
}

// Базові гармонічні прогресії ступенів ладу (0-indexed ступінь)
const std::vector<int>& getProgressionDegrees(int prog_id) {
    static const std::vector<int> kStatic = {0};
    static const std::vector<int> kProg1 = {0, 5, 2, 6}; // i - VI - III - VII
    static const std::vector<int> kProg2 = {0, 3, 4, 0}; // I - IV - V - I
    static const std::vector<int> kProg3 = {0, 3, 4, 4}; // i - iv - v - v

    switch (prog_id) {
        case 1: return kProg1;
        case 2: return kProg2;
        case 3: return kProg3;
        default: return kStatic;
    }
}

} // namespace

const char* getScaleName(ScaleType scale) {
    switch (scale) {
        case ScaleType::kChromatic:       return "CHROMATIC";
        case ScaleType::kMajor:           return "MAJOR";
        case ScaleType::kMinor:           return "NAT MINOR";
        case ScaleType::kHarmonicMinor:   return "HARM MIN";
        case ScaleType::kDorian:          return "DORIAN";
        case ScaleType::kPhrygian:        return "PHRYGIAN";
        case ScaleType::kLydian:          return "LYDIAN";
        case ScaleType::kMixolydian:      return "MIXOLYDIAN";
        case ScaleType::kPentatonicMinor: return "PENT MIN";
        case ScaleType::kPentatonicMajor: return "PENT MAJ";
        case ScaleType::kBlues:           return "BLUES";
        default:                          return "UNKNOWN";
    }
}

int quantizeToScale(int midi_note, int root_note, ScaleType scale) {
    if (scale == ScaleType::kChromatic) {
        return std::clamp(midi_note, 0, 127);
    }

    const auto& intervals = getScaleIntervals(scale);
    int norm = midi_note - root_note;
    int oct = norm / 12;
    int semitone = norm % 12;
    if (semitone < 0) {
        semitone += 12;
        oct -= 1;
    }

    // Пошук найближчого ступеня
    int best_diff = 999;
    int best_interval = intervals.front();
    for (int interval : intervals) {
        int diff = std::abs(semitone - interval);
        if (diff < best_diff) {
            best_diff = diff;
            best_interval = interval;
        }
    }

    int result = root_note + oct * 12 + best_interval;
    return std::clamp(result, 0, 127);
}

const char* getArp1DivisionName(Arp1Division div) {
    switch (div) {
        case Arp1Division::kDiv1_1: return "1/1 (16 ST)";
        case Arp1Division::kDiv1_2: return "1/2 (8 ST)";
        case Arp1Division::kDiv1_4: return "1/4 (4 ST)";
        case Arp1Division::kDiv1_8: return "1/8 (2 ST)";
        default: return "1/2";
    }
}

int getArp1StepLength(Arp1Division div) {
    switch (div) {
        case Arp1Division::kDiv1_1: return 16;
        case Arp1Division::kDiv1_2: return 8;
        case Arp1Division::kDiv1_4: return 4;
        case Arp1Division::kDiv1_8: return 2;
        default: return 8;
    }
}

const char* getChordTypeName(ChordType chord) {
    switch (chord) {
        case ChordType::kRootOnly: return "ROOT";
        case ChordType::kOctaves:  return "OCTAVES";
        case ChordType::kFifth:    return "POWER 5TH";
        case ChordType::kTriad:    return "TRIAD";
        case ChordType::kSeventh:  return "7TH";
        case ChordType::kSus4:     return "SUS4";
        case ChordType::kAdd9:     return "ADD9";
        default: return "TRIAD";
    }
}

std::vector<int> getChordIntervals(ChordType chord, ScaleType scale) {
    const auto& intervals = getScaleIntervals(scale);
    const int count = static_cast<int>(intervals.size());
    auto degToSemi = [&](int degree) -> int {
        int oct = degree / count;
        int idx = degree % count;
        return oct * 12 + intervals[idx];
    };

    switch (chord) {
        case ChordType::kRootOnly:
            return {0};
        case ChordType::kOctaves:
            return {0, 12};
        case ChordType::kFifth:
            return {0, 7};
        case ChordType::kTriad:
            return {0, degToSemi(2), degToSemi(4)}; // 1-3-5
        case ChordType::kSeventh:
            return {0, degToSemi(2), degToSemi(4), degToSemi(6)}; // 1-3-5-7
        case ChordType::kSus4:
            return {0, degToSemi(3), degToSemi(4)}; // 1-4-5
        case ChordType::kAdd9:
            return {0, degToSemi(2), degToSemi(4), degToSemi(8)}; // 1-3-5-9
        default:
            return {0, 4, 7};
    }
}

const char* getArp2DivisionName(Arp2Division div) {
    switch (div) {
        case Arp2Division::kDiv1_4:  return "1/4";
        case Arp2Division::kDiv1_8:  return "1/8";
        case Arp2Division::kDiv1_16: return "1/16";
        case Arp2Division::kDiv1_32: return "1/32";
        case Arp2Division::kDiv1_16T:return "1/16T";
        default: return "1/16";
    }
}

const char* getArpPatternName(ArpPattern pattern) {
    switch (pattern) {
        case ArpPattern::kUp:          return "UP";
        case ArpPattern::kDown:        return "DOWN";
        case ArpPattern::kUpDown:      return "UP-DOWN";
        case ArpPattern::kPingPong:    return "PING-PONG";
        case ArpPattern::kConverge:    return "CONVERGE";
        case ArpPattern::kDiverge:     return "DIVERGE";
        case ArpPattern::kRandom:      return "RANDOM";
        case ArpPattern::kBrownianWalk:return "WALK";
        case ArpPattern::kEuclidean:   return "EUCLIDEAN";
        default: return "UP";
    }
}

std::vector<bool> Arpeggiator::generateEuclideanRhythm(int hits, int total_steps) {
    if (total_steps <= 0) return {};
    hits = std::clamp(hits, 0, total_steps);
    std::vector<bool> pattern(total_steps, false);
    if (hits == 0) return pattern;

    // Класичний алгоритм Бйорклунда (Bjorklund)
    int accumulator = 0;
    for (int i = 0; i < total_steps; ++i) {
        accumulator += hits;
        if (accumulator >= total_steps) {
            accumulator -= total_steps;
            pattern[i] = true;
        }
    }
    return pattern;
}

Arpeggiator::Arpeggiator() {
    config_ = ArpConfig{};
}

std::vector<SequencerStep> Arpeggiator::generateSteps(int count) {
    std::vector<SequencerStep> steps(count);
    const auto& prog = getProgressionDegrees(config_.progression_id);
    const auto chord_offsets = getChordIntervals(config_.chord_type, config_.scale);
    const auto euclid = generateEuclideanRhythm(config_.euclid_hits, std::min(count, config_.euclid_length));

    // Побудова повного пулу нот для арпеджиратора (з урахуванням октав)
    std::vector<int> note_pool;
    for (int oct = 0; oct < config_.octaves; ++oct) {
        for (int offset : chord_offsets) {
            note_pool.push_back(offset + oct * 12);
        }
    }
    if (note_pool.empty()) note_pool.push_back(0);

    const int pool_size = static_cast<int>(note_pool.size());
    int walk_idx = 0;
    int arp1_len = getArp1StepLength(config_.arp1_div);

    uint32_t lfsr = config_.seed != 0 ? config_.seed : turing_register_;

    for (int i = 0; i < count; ++i) {
        // Оновлення регістру зсуву (Turing Machine / LFSR)
        uint32_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
        lfsr = (lfsr >> 1) | (bit << 15);

        // Arp 1: Визначення поточної гармонічної бази з прогресії
        int prog_idx = (i / arp1_len) % prog.size();
        int base_deg = prog[prog_idx];
        const auto& intervals = getScaleIntervals(config_.scale);
        int chord_root_offset = 0;
        if (!intervals.empty()) {
            chord_root_offset = intervals[base_deg % intervals.size()] + (base_deg / intervals.size()) * 12;
        }

        // Arp 2: Визначення індексу в пулі нот
        int note_idx = 0;
        switch (config_.pattern) {
            case ArpPattern::kUp:
                note_idx = i % pool_size;
                break;
            case ArpPattern::kDown:
                note_idx = (pool_size - 1) - (i % pool_size);
                break;
            case ArpPattern::kUpDown: {
                int cycle = pool_size > 1 ? (pool_size * 2 - 2) : 1;
                int pos = i % cycle;
                note_idx = (pos < pool_size) ? pos : (cycle - pos);
                break;
            }
            case ArpPattern::kPingPong: {
                int cycle = pool_size > 1 ? (pool_size * 2) : 1;
                int pos = i % cycle;
                note_idx = (pos < pool_size) ? pos : (cycle - 1 - pos);
                break;
            }
            case ArpPattern::kConverge: {
                // Ззовні всередину: 0, N-1, 1, N-2...
                int pos = i % pool_size;
                note_idx = (pos % 2 == 0) ? (pos / 2) : (pool_size - 1 - pos / 2);
                break;
            }
            case ArpPattern::kDiverge: {
                // З середини назовні
                int mid = pool_size / 2;
                int pos = i % pool_size;
                int delta = (pos + 1) / 2;
                note_idx = (pos % 2 == 0) ? (mid + delta) : (mid - delta);
                note_idx = std::clamp(note_idx, 0, pool_size - 1);
                break;
            }
            case ArpPattern::kRandom:
                note_idx = (lfsr >> 4) % pool_size;
                break;
            case ArpPattern::kBrownianWalk: {
                int r = (lfsr & 0x03);
                if (r == 1) walk_idx++;
                else if (r == 2) walk_idx--;
                walk_idx = std::clamp(walk_idx, 0, pool_size - 1);
                note_idx = walk_idx;
                break;
            }
            case ArpPattern::kEuclidean:
            default:
                note_idx = i % pool_size;
                break;
        }

        // Мутація (Turing Machine probability)
        if (config_.mutation_pct > 0) {
            int chance = (lfsr & 0x7F) % 100;
            if (chance < config_.mutation_pct) {
                note_idx = (note_idx + 1 + ((lfsr >> 7) % (pool_size > 1 ? pool_size - 1 : 1))) % pool_size;
            }
        }

        // Квантування кінцевої ноти
        int raw_note = config_.root_note + chord_root_offset + note_pool[note_idx];
        int final_note = quantizeToScale(raw_note, config_.root_note, config_.scale);

        // Ритмічний гейт (Euclidean або звичайний пульс)
        bool trig_active = true;
        if (!euclid.empty()) {
            trig_active = euclid[i % euclid.size()];
        }

        SequencerStep step;
        step.enabled = trig_active;
        step.note = static_cast<uint8_t>(final_note);
        step.velocity = 0.85f;
        step.gate_len = config_.arp2_gate;
        step.slide = config_.slide_auto && (i > 0 && steps[i - 1].note != step.note);
        step.probability = 100;
        step.condition = TrigCondition::kNone;

        steps[i] = step;
    }

    turing_register_ = lfsr;
    return steps;
}

bool Arpeggiator::commitToTrack(AudioEngine& engine, int row, int start_step, int count) {
    if (row < 0 || row >= kTotalRows) return false;
    auto steps = generateSteps(count);
    for (int i = 0; i < count; ++i) {
        int idx = start_step + i;
        if (idx < 0 || idx >= static_cast<int>(kMaxSteps)) break;
        engine.setStep(row, idx, steps[i]);
    }
    return true;
}

} // namespace groovebox
