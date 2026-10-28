#pragma once

#include "synth_engines.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace groovebox {

class AudioEngine;

inline constexpr int kNumBanks = 8;        // Банки A, B, C, D, E, F, G, H
inline constexpr int kPatternsPerBank = 16;// Патерни 01 .. 16 (разом 128)
inline constexpr int kTotalPatterns = kNumBanks * kPatternsPerBank;

// Повні дані одного патерну Elektron (звуки 4 треків + секвенція 16 рядків x 128 кроків)
struct PatternData {
    char name[16] = "PATTERN";
    float bpm = 120.0f;
    uint32_t pattern_length = 64;
    std::array<TrackParameters, kTracksCount> tracks{};
    std::array<uint32_t, kTotalRows> row_lengths{};
    std::array<std::array<SequencerStep, kMaxSteps>, kTotalRows> steps{};
    bool initialized = false;
};

class PatternManager {
public:
    PatternManager();

    // Поточний та відкладений патерн (Elektron Queued Pattern Change)
    int currentBank() const { return current_bank_; }
    int currentPattern() const { return current_pattern_; }
    int queuedBank() const { return queued_bank_; }
    int queuedPattern() const { return queued_pattern_; }

    std::string activePatternName() const;
    static std::string formatPatternCode(int bank, int pattern);

    // Вибір патерну (квантовано або миттєво) з автозбереженням поточного
    void selectPattern(int bank, int pattern, bool quantized = true);
    void switchPatternNow(int bank, int pattern, AudioEngine& engine, float& bpm);
    // Вибір по номеру MIDI Program Change (0..127)
    void selectProgramChange(int program, bool quantized = true);

    // Викликається наприкінці циклу такту/бару секвенсора
    bool checkAndApplyQueued(AudioEngine& engine, float& bpm);

    // Зберегти стан рушія у поточний слот
    void captureCurrent(const AudioEngine& engine, float bpm);
    // Завантажити активний слот у рушій
    void applyCurrent(AudioEngine& engine, float& bpm);

    // Збереження та завантаження всього проєкту на диск (128 патернів)
    bool saveProject(const std::string& filepath, const AudioEngine& engine, float bpm);
    bool loadProject(const std::string& filepath, AudioEngine& engine, float& bpm);

    // Шляхи та файловий менеджер проєктів
    std::string currentProjectPath() const { return current_project_path_; }
    void setCurrentProjectPath(std::string p) { current_project_path_ = std::move(p); }
    static std::string projectsDirectory();
    static std::string defaultProjectPath();
    static std::vector<std::string> listProjectFiles();

private:
    int current_bank_ = 0;    // 0..7 (A..H)
    int current_pattern_ = 0; // 0..15 (01..16)
    int queued_bank_ = -1;
    int queued_pattern_ = -1;
    std::string current_project_path_;

    std::vector<PatternData> patterns_;
};

} // namespace groovebox
