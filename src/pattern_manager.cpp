#include "pattern_manager.hpp"
#include "audio_engine.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace groovebox {

namespace {
constexpr uint32_t kProjectMagic = 0x47425831u; // "GBX1"
constexpr uint32_t kProjectVersion = 1u;
} // namespace

PatternManager::PatternManager() {
    for (int i = 0; i < kTotalPatterns; ++i) {
        int b = i / kPatternsPerBank;
        int p = i % kPatternsPerBank;
        std::snprintf(patterns_[i].name, sizeof(patterns_[i].name), "%c%02d", 'A' + b, p + 1);
        patterns_[i].bpm = 120.0f;
        patterns_[i].pattern_length = 64;
        patterns_[i].row_lengths.fill(16);
        patterns_[i].initialized = false;
    }
}

std::string PatternManager::formatPatternCode(int bank, int pattern) {
    char b[16];
    std::snprintf(b, sizeof(b), "%c%02d", 'A' + std::clamp(bank, 0, kNumBanks - 1), std::clamp(pattern, 0, kPatternsPerBank - 1) + 1);
    return std::string(b);
}

std::string PatternManager::activePatternName() const {
    if (queued_pattern_ >= 0) {
        return formatPatternCode(current_bank_, current_pattern_) + "->" + formatPatternCode(queued_bank_, queued_pattern_);
    }
    return formatPatternCode(current_bank_, current_pattern_);
}

void PatternManager::selectPattern(int bank, int pattern, bool quantized) {
    bank = std::clamp(bank, 0, kNumBanks - 1);
    pattern = std::clamp(pattern, 0, kPatternsPerBank - 1);
    if (!quantized) {
        current_bank_ = bank;
        current_pattern_ = pattern;
        queued_bank_ = -1;
        queued_pattern_ = -1;
    } else {
        queued_bank_ = bank;
        queued_pattern_ = pattern;
    }
}

void PatternManager::selectProgramChange(int program, bool quantized) {
    program = std::clamp(program, 0, kTotalPatterns - 1);
    selectPattern(program / kPatternsPerBank, program % kPatternsPerBank, quantized);
}

bool PatternManager::checkAndApplyQueued(AudioEngine& engine, float& bpm) {
    if (queued_bank_ < 0 || queued_pattern_ < 0) return false;
    // Зберігаємо поточний стан перед переходом
    captureCurrent(engine, bpm);
    current_bank_ = queued_bank_;
    current_pattern_ = queued_pattern_;
    queued_bank_ = -1;
    queued_pattern_ = -1;
    applyCurrent(engine, bpm);
    return true;
}

void PatternManager::captureCurrent(const AudioEngine& engine, float bpm) {
    const int idx = current_bank_ * kPatternsPerBank + current_pattern_;
    auto& p = patterns_[idx];
    p.bpm = bpm;
    p.pattern_length = engine.getPatternLength();

    for (int t = 0; t < kTracksCount; ++t) {
        p.tracks[t] = engine.getTrackParams(t);
    }

    const int act = engine.getActiveRowCount();
    for (int r = 0; r < act; ++r) {
        p.row_lengths[r] = engine.getRowLength(r);
        for (uint32_t s = 0; s < p.pattern_length && s < kMaxSteps; ++s) {
            p.steps[r][s] = engine.getStep(r, s);
        }
    }
    p.initialized = true;
}

void PatternManager::applyCurrent(AudioEngine& engine, float& bpm) {
    const int idx = current_bank_ * kPatternsPerBank + current_pattern_;
    const auto& p = patterns_[idx];
    if (!p.initialized) return;

    bpm = p.bpm;
    engine.setPatternLength(p.pattern_length);

    for (int t = 0; t < kTracksCount; ++t) {
        engine.setTrackParams(t, p.tracks[t]);
    }

    const int act = engine.getActiveRowCount();
    for (int r = 0; r < act; ++r) {
        engine.setRowLength(r, p.row_lengths[r]);
        for (uint32_t s = 0; s < p.pattern_length && s < kMaxSteps; ++s) {
            engine.setStep(r, s, p.steps[r][s]);
        }
    }
}

std::string PatternManager::defaultProjectPath() {
#if defined(__linux__)
    return "/storage/dev/groovebox/projects/default_project.gbx";
#else
    return "default_project.gbx";
#endif
}

bool PatternManager::saveProject(const std::string& filepath, const AudioEngine& engine, float bpm) {
    const_cast<PatternManager*>(this)->captureCurrent(engine, bpm);

    std::ofstream os(filepath, std::ios::binary);
    if (!os.is_open()) return false;

    os.write(reinterpret_cast<const char*>(&kProjectMagic), sizeof(kProjectMagic));
    os.write(reinterpret_cast<const char*>(&kProjectVersion), sizeof(kProjectVersion));
    os.write(reinterpret_cast<const char*>(&current_bank_), sizeof(current_bank_));
    os.write(reinterpret_cast<const char*>(&current_pattern_), sizeof(current_pattern_));

    for (int i = 0; i < kTotalPatterns; ++i) {
        os.write(reinterpret_cast<const char*>(&patterns_[i]), sizeof(PatternData));
    }

    return os.good();
}

bool PatternManager::loadProject(const std::string& filepath, AudioEngine& engine, float& bpm) {
    std::ifstream is(filepath, std::ios::binary);
    if (!is.is_open()) return false;

    uint32_t magic = 0;
    uint32_t ver = 0;
    is.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    is.read(reinterpret_cast<char*>(&ver), sizeof(ver));
    if (magic != kProjectMagic || ver != kProjectVersion) return false;

    is.read(reinterpret_cast<char*>(&current_bank_), sizeof(current_bank_));
    is.read(reinterpret_cast<char*>(&current_pattern_), sizeof(current_pattern_));

    for (int i = 0; i < kTotalPatterns; ++i) {
        is.read(reinterpret_cast<char*>(&patterns_[i]), sizeof(PatternData));
    }

    if (is.good()) {
        applyCurrent(engine, bpm);
        return true;
    }
    return false;
}

} // namespace groovebox
