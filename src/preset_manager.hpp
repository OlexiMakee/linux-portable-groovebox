#pragma once

#include "synth_engines.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace groovebox {

// Структура окремого тембру / пресету доріжки (Elektron Sound / Instrument Preset)
struct TrackPreset {
    char name[32] = "INIT SOUND";
    EngineType engine = EngineType::kVaMono;
    TrackParameters params{};
};

class PresetManager {
public:
    static PresetManager& instance() {
        static PresetManager mgr;
        return mgr;
    }

    // Отримати список вбудованих фабричних пресетів для даного рушія
    std::vector<TrackPreset> getFactoryPresets(EngineType engine) const {
        std::vector<TrackPreset> list;
        switch (engine) {
            case EngineType::kAcid303: {
                // 1. Classic Acid Saw
                TrackPreset p1;
                std::strncpy(p1.name, "01: ACID CLASSIC", sizeof(p1.name) - 1);
                p1.engine = EngineType::kAcid303;
                p1.params.engine = EngineType::kAcid303;
                p1.params.p[0] = 0.35f; // CUTOFF
                p1.params.p[1] = 0.82f; // RESO
                p1.params.p[2] = 0.65f; // ENV MOD
                p1.params.p[3] = 0.28f; // DECAY
                p1.params.p[4] = 0.70f; // ACCENT
                p1.params.p[5] = 0.00f; // SAW
                p1.params.lpf_cutoff = 0.35f;
                p1.params.lpf_resonance = 0.82f;
                p1.params.lpf_env = 0.65f;
                p1.params.lpf_decay = 0.28f;
                p1.params.lpf_24db = true;
                p1.params.attack = 0.002f;
                p1.params.decay = 0.30f;
                p1.params.sustain = 0.0f;
                p1.params.release = 0.12f;
                list.push_back(p1);

                // 2. Rubber Square Bass
                TrackPreset p2;
                std::strncpy(p2.name, "02: RUBBER SQUARE", sizeof(p2.name) - 1);
                p2.engine = EngineType::kAcid303;
                p2.params.engine = EngineType::kAcid303;
                p2.params.p[0] = 0.22f;
                p2.params.p[1] = 0.45f;
                p2.params.p[2] = 0.80f;
                p2.params.p[3] = 0.18f;
                p2.params.p[4] = 0.50f;
                p2.params.p[5] = 1.00f; // SQUARE
                p2.params.lpf_cutoff = 0.22f;
                p2.params.lpf_resonance = 0.45f;
                p2.params.lpf_env = 0.80f;
                p2.params.lpf_decay = 0.18f;
                p2.params.attack = 0.002f;
                p2.params.decay = 0.25f;
                p2.params.sustain = 0.0f;
                p2.params.release = 0.10f;
                list.push_back(p2);

                // 3. Scream Lead
                TrackPreset p3;
                std::strncpy(p3.name, "03: SCREAM LEAD", sizeof(p3.name) - 1);
                p3.engine = EngineType::kAcid303;
                p3.params.engine = EngineType::kAcid303;
                p3.params.p[0] = 0.52f;
                p3.params.p[1] = 0.92f;
                p3.params.p[2] = 0.90f;
                p3.params.p[3] = 0.45f;
                p3.params.p[4] = 0.85f;
                p3.params.p[5] = 0.00f;
                p3.params.lpf_cutoff = 0.52f;
                p3.params.lpf_resonance = 0.92f;
                p3.params.lpf_env = 0.90f;
                p3.params.lpf_decay = 0.45f;
                p3.params.lpf_24db = true;
                p3.params.attack = 0.002f;
                p3.params.decay = 0.45f;
                p3.params.sustain = 0.15f;
                p3.params.release = 0.20f;
                list.push_back(p3);
                break;
            }
            case EngineType::kVaMono: {
                // 1. Roland JP-8000 Supersaw Lead
                TrackPreset p1;
                std::strncpy(p1.name, "01: JP8000 SUPERSAW", sizeof(p1.name) - 1);
                p1.engine = EngineType::kVaMono;
                p1.params.engine = EngineType::kVaMono;
                p1.params.p[0] = 1.0f;  // WAVE = 5 (Supersaw)
                p1.params.p[1] = 0.65f; // SPREAD
                p1.params.p[2] = 0.35f; // SUB LVL
                p1.params.p[3] = 1.00f; // SUB WAVE = Sine
                p1.params.p[4] = 0.40f; // DETUNE
                p1.params.p[5] = 0.50f; // OSC MIX
                p1.params.lpf_cutoff = 0.78f;
                p1.params.lpf_resonance = 0.30f;
                p1.params.lpf_24db = true;
                p1.params.attack = 0.010f;
                p1.params.decay = 0.60f;
                p1.params.sustain = 0.70f;
                p1.params.release = 0.25f;
                list.push_back(p1);

                // 2. Punchy Pluck Synth
                TrackPreset p2;
                std::strncpy(p2.name, "02: PUNCHY PLUCK", sizeof(p2.name) - 1);
                p2.engine = EngineType::kVaMono;
                p2.params.engine = EngineType::kVaMono;
                p2.params.p[0] = 0.0f;  // WAVE = Saw
                p2.params.p[1] = 0.50f;
                p2.params.p[2] = 0.50f; // Sub Level
                p2.params.p[3] = 0.00f; // Sub Square
                p2.params.lpf_cutoff = 0.25f;
                p2.params.lpf_resonance = 0.50f;
                p2.params.lpf_env = 0.85f;
                p2.params.lpf_decay = 0.16f;
                p2.params.attack = 0.002f;
                p2.params.decay = 0.30f;
                p2.params.sustain = 0.0f;
                p2.params.release = 0.15f;
                list.push_back(p2);

                // 3. Fat Pulse PWM Bass
                TrackPreset p3;
                std::strncpy(p3.name, "03: FAT PULSE PWM", sizeof(p3.name) - 1);
                p3.engine = EngineType::kVaMono;
                p3.params.engine = EngineType::kVaMono;
                p3.params.p[0] = 0.20f; // WAVE = Pulse
                p3.params.p[1] = 0.35f; // PW
                p3.params.p[2] = 0.80f; // Sub Level
                p3.params.p[3] = 0.00f; // Sub Square
                p3.params.lpf_cutoff = 0.32f;
                p3.params.lpf_resonance = 0.40f;
                p3.params.lpf_env = 0.40f;
                p3.params.lpf_decay = 0.25f;
                p3.params.attack = 0.005f;
                p3.params.decay = 0.40f;
                p3.params.sustain = 0.20f;
                p3.params.release = 0.18f;
                list.push_back(p3);

                // 4. Warm Analog Pad
                TrackPreset p4;
                std::strncpy(p4.name, "04: WARM ANALOG PAD", sizeof(p4.name) - 1);
                p4.engine = EngineType::kVaMono;
                p4.params.engine = EngineType::kVaMono;
                p4.params.p[0] = 0.0f; // Saw
                p4.params.p[1] = 0.50f;
                p4.params.p[2] = 0.30f;
                p4.params.p[3] = 1.00f;
                p4.params.lpf_cutoff = 0.55f;
                p4.params.lpf_resonance = 0.20f;
                p4.params.attack = 0.35f;
                p4.params.decay = 0.80f;
                p4.params.sustain = 0.75f;
                p4.params.release = 0.50f;
                list.push_back(p4);
                break;
            }
            case EngineType::kChordPad: {
                // 1. Deep House m7
                TrackPreset p1;
                std::strncpy(p1.name, "01: DEEP HOUSE M7", sizeof(p1.name) - 1);
                p1.engine = EngineType::kChordPad;
                p1.params.engine = EngineType::kChordPad;
                p1.params.p[2] = 0.0f;  // m7 chord
                p1.params.p[4] = 0.30f; // Detune
                p1.params.lpf_cutoff = 0.42f;
                p1.params.lpf_resonance = 0.35f;
                p1.params.attack = 0.010f;
                p1.params.decay = 0.60f;
                p1.params.sustain = 0.40f;
                p1.params.release = 0.25f;
                list.push_back(p1);

                // 2. Lush Maj7
                TrackPreset p2;
                std::strncpy(p2.name, "02: LUSH MAJ7", sizeof(p2.name) - 1);
                p2.engine = EngineType::kChordPad;
                p2.params.engine = EngineType::kChordPad;
                p2.params.p[2] = 0.33f; // Maj7 chord
                p2.params.p[4] = 0.50f;
                p2.params.lpf_cutoff = 0.65f;
                p2.params.lpf_resonance = 0.20f;
                p2.params.attack = 0.20f;
                p2.params.decay = 0.80f;
                p2.params.sustain = 0.60f;
                p2.params.release = 0.40f;
                list.push_back(p2);
                break;
            }
            case EngineType::kSampleOneShot:
            case EngineType::kSliceSampler: {
                // 1. Standard One-Shot
                TrackPreset p1;
                std::strncpy(p1.name, "01: STANDARD ONESHOT", sizeof(p1.name) - 1);
                p1.engine = EngineType::kSampleOneShot;
                p1.params.engine = EngineType::kSampleOneShot;
                p1.params.sample.play_mode = SampleEngineMode::kOneShot;
                p1.params.sample.start = 0.0f;
                p1.params.sample.end = 1.0f;
                p1.params.lpf_cutoff = 1.0f;
                p1.params.attack = 0.001f;
                p1.params.decay = 0.80f;
                p1.params.sustain = 0.80f;
                p1.params.release = 0.20f;
                list.push_back(p1);

                // 2. 16-Step Sliced Loop
                TrackPreset p2;
                std::strncpy(p2.name, "02: SLICED BREAK", sizeof(p2.name) - 1);
                p2.engine = EngineType::kSampleOneShot;
                p2.params.engine = EngineType::kSampleOneShot;
                p2.params.sample.play_mode = SampleEngineMode::kSlicer;
                p2.params.sample.slice_index = 0;
                p2.params.attack = 0.001f;
                p2.params.decay = 0.60f;
                p2.params.sustain = 1.0f;
                p2.params.release = 0.05f;
                list.push_back(p2);
                break;
            }
            case EngineType::kDx7: {
                // 1. Classic E.Piano 1
                TrackPreset p1;
                std::strncpy(p1.name, "01: E.PIANO 1", sizeof(p1.name) - 1);
                p1.engine = EngineType::kDx7;
                p1.params.engine = EngineType::kDx7;
                p1.params.p[0] = 0.0f;  // Preset 0
                p1.params.p[2] = 0.50f; // Bright
                p1.params.lpf_cutoff = 0.90f;
                list.push_back(p1);

                // 2. Solid Bass 1
                TrackPreset p2;
                std::strncpy(p2.name, "02: BASS 1", sizeof(p2.name) - 1);
                p2.engine = EngineType::kDx7;
                p2.params.engine = EngineType::kDx7;
                p2.params.p[0] = 0.032f; // Preset 1
                p2.params.p[2] = 0.45f;
                p2.params.lpf_cutoff = 0.70f;
                list.push_back(p2);
                break;
            }
            default:
                break;
        }
        return list;
    }

    // Застосувати пресет до цільового треку зі збереженням індивідуального роутингу (level, pan, mute, preset_idx)
    void applyPreset(TrackParameters& target, const TrackPreset& preset) const {
        float saved_level = target.level;
        float saved_pan = target.pan;
        bool saved_muted = target.muted;
        int saved_transpose = target.transpose;
        int saved_preset = target.preset_idx;

        target = preset.params;
        target.engine = preset.engine;
        target.level = saved_level;
        target.pan = saved_pan;
        target.muted = saved_muted;
        target.transpose = saved_transpose;
        target.preset_idx = saved_preset;
    }

    // Бінарне збереження пресету у файл (.snd)
    bool savePreset(const std::string& filepath, const TrackPreset& preset) const {
        std::ofstream ofs(filepath, std::ios::binary);
        if (!ofs.is_open()) return false;
        // Заголовок Magic 'X','U','S','1' (XU10 Sound v1)
        static constexpr uint32_t kMagic = 0x31535558;
        ofs.write(reinterpret_cast<const char*>(&kMagic), sizeof(kMagic));
        ofs.write(preset.name, sizeof(preset.name));
        auto eng_val = static_cast<uint8_t>(preset.engine);
        ofs.write(reinterpret_cast<const char*>(&eng_val), sizeof(eng_val));
        ofs.write(reinterpret_cast<const char*>(&preset.params), sizeof(preset.params));
        return ofs.good();
    }

    // Бінарне завантаження пресету з файлу (.snd)
    bool loadPreset(const std::string& filepath, TrackPreset& preset) const {
        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs.is_open()) return false;
        uint32_t magic = 0;
        ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (magic != 0x31535558) return false;
        ifs.read(preset.name, sizeof(preset.name));
        uint8_t eng_val = 0;
        ifs.read(reinterpret_cast<char*>(&eng_val), sizeof(eng_val));
        preset.engine = static_cast<EngineType>(eng_val);
        ifs.read(reinterpret_cast<char*>(&preset.params), sizeof(preset.params));
        return ifs.good();
    }
};

} // namespace groovebox
