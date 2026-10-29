#include "pattern_manager.hpp"
#include "audio_engine.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace groovebox {

namespace {
constexpr uint32_t kProjectMagic = 0x47425831u; // "GBX1"
constexpr uint32_t kProjectVersion = 2u;
} // namespace

PatternManager::PatternManager() {
    patterns_.resize(kTotalPatterns);
    for (int i = 0; i < kTotalPatterns; ++i) {
        int b = i / kPatternsPerBank;
        int p = i % kPatternsPerBank;
        std::snprintf(patterns_[i].name, sizeof(patterns_[i].name), "%c%02d", 'A' + b, p + 1);
        patterns_[i].bpm = 120.0f;
        patterns_[i].pattern_length = 64;
        patterns_[i].row_lengths.fill(16);
        patterns_[i].initialized = false;
    }
    current_project_path_ = defaultProjectPath();
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

void PatternManager::switchPatternNow(int bank, int pattern, AudioEngine& engine, float& bpm) {
    bank = std::clamp(bank, 0, kNumBanks - 1);
    pattern = std::clamp(pattern, 0, kPatternsPerBank - 1);
    if (bank == current_bank_ && pattern == current_pattern_) return;

    // 1. Зберігаємо поточний патерн, щоб жодна нота не втратилася
    captureCurrent(engine, bpm);

    // 2. Перемикаємося на новий патерн
    current_bank_ = bank;
    current_pattern_ = pattern;
    queued_bank_ = -1;
    queued_pattern_ = -1;

    // 3. Якщо цільовий патерн ще не був ініціалізований — копіюємо актуальні звуки треків і очищаємо кроки
    const int idx = current_bank_ * kPatternsPerBank + current_pattern_;
    auto& p = patterns_[idx];
    if (!p.initialized) {
        p.bpm = bpm;
        p.pattern_length = 16;
        p.active_rows_count = 8;
        for (int t = 0; t < kTracksCount; ++t) {
            p.tracks[t] = engine.getTrackParams(t);
        }
        for (int r = 0; r < kTotalRows; ++r) {
            p.row_lengths[r] = 16;
            for (uint32_t s = 0; s < kMaxSteps; ++s) {
                p.steps[r][s] = SequencerStep{false, false, false, 48, {48, 255, 255, 255}, 0.90f, 0.60f, 100, TrigCondition::kNone, false, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
            }
        }
        p.initialized = true;
    }

    // 4. Завантажуємо патерн у рушій
    applyCurrent(engine, bpm);
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
    p.active_rows_count = act;
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
    engine.setActiveRowCount(p.active_rows_count);

    for (int t = 0; t < kTracksCount; ++t) {
        engine.setTrackParams(t, p.tracks[t]);
    }

    const int act = p.active_rows_count;
    for (int r = 0; r < act; ++r) {
        engine.setRowLength(r, p.row_lengths[r]);
        for (uint32_t s = 0; s < p.pattern_length && s < kMaxSteps; ++s) {
            engine.setStep(r, s, p.steps[r][s]);
        }
    }
}

std::string PatternManager::projectsDirectory() {
#if defined(__linux__)
    return "/storage/dev/groovebox/projects";
#else
    return "projects";
#endif
}

std::string PatternManager::defaultProjectPath() {
    return projectsDirectory() + "/default_project.xgproj";
}

std::vector<std::string> PatternManager::listProjectFiles() {
    std::vector<std::string> out;
    std::string dir = projectsDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::exists(dir, ec)) return out;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext == ".xgproj" || ext == ".gbx") {
            out.push_back(entry.path().filename().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool PatternManager::saveProject(const std::string& filepath, const AudioEngine& engine, float bpm,
                                 const std::array<SongStep, 16>* song_chain,
                                 int song_rows_count) {
    const_cast<PatternManager*>(this)->captureCurrent(engine, bpm);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(filepath).parent_path(), ec);

    std::ofstream os(filepath, std::ios::binary);
    if (!os.is_open()) return false;

    os.write(reinterpret_cast<const char*>(&kProjectMagic), sizeof(kProjectMagic));
    os.write(reinterpret_cast<const char*>(&kProjectVersion), sizeof(kProjectVersion));
    os.write(reinterpret_cast<const char*>(&current_bank_), sizeof(current_bank_));
    os.write(reinterpret_cast<const char*>(&current_pattern_), sizeof(current_pattern_));
    os.write(reinterpret_cast<const char*>(&bpm), sizeof(bpm));
    os.write(reinterpret_cast<const char*>(&song_rows_count), sizeof(song_rows_count));

    std::array<SongStep, 16> dummy_sc{};
    dummy_sc[0].enabled = true;
    const auto* sc_to_write = song_chain ? song_chain : &dummy_sc;
    os.write(reinterpret_cast<const char*>(sc_to_write->data()), sizeof(SongStep) * 16);

    MasterFxParameters mfx = engine.getMasterFx();
    os.write(reinterpret_cast<const char*>(&mfx), sizeof(MasterFxParameters));

    for (int i = 0; i < kTotalPatterns; ++i) {
        os.write(reinterpret_cast<const char*>(&patterns_[i]), sizeof(PatternData));
    }

    if (os.good()) {
        const_cast<PatternManager*>(this)->current_project_path_ = filepath;
        return true;
    }
    return false;
}

bool PatternManager::loadProject(const std::string& filepath, AudioEngine& engine, float& bpm,
                                 std::array<SongStep, 16>* song_chain,
                                 int* song_rows_count) {
    std::ifstream is(filepath, std::ios::binary);
    if (!is.is_open()) return false;

    uint32_t magic = 0;
    uint32_t ver = 0;
    is.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    is.read(reinterpret_cast<char*>(&ver), sizeof(ver));
    if (magic != kProjectMagic || (ver != 1u && ver != 2u)) return false;

    is.read(reinterpret_cast<char*>(&current_bank_), sizeof(current_bank_));
    is.read(reinterpret_cast<char*>(&current_pattern_), sizeof(current_pattern_));

    if (ver >= 2u) {
        is.read(reinterpret_cast<char*>(&bpm), sizeof(bpm));
        int loaded_rows_count = 1;
        is.read(reinterpret_cast<char*>(&loaded_rows_count), sizeof(loaded_rows_count));
        if (song_rows_count) *song_rows_count = std::clamp(loaded_rows_count, 1, 16);

        std::array<SongStep, 16> loaded_sc{};
        is.read(reinterpret_cast<char*>(loaded_sc.data()), sizeof(SongStep) * 16);
        if (song_chain) *song_chain = loaded_sc;

        MasterFxParameters loaded_mfx{};
        is.read(reinterpret_cast<char*>(&loaded_mfx), sizeof(MasterFxParameters));
        engine.setMasterFx(loaded_mfx);
    }

    for (int i = 0; i < kTotalPatterns; ++i) {
        is.read(reinterpret_cast<char*>(&patterns_[i]), sizeof(PatternData));
    }

    if (is.good()) {
        current_project_path_ = filepath;
        applyCurrent(engine, bpm);
        return true;
    }
    return false;
}

} // namespace groovebox
