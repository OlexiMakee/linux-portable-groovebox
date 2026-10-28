#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace groovebox {

inline constexpr int kDx7Operators = 6;
inline constexpr int kDx7PresetsPerCartridge = 32;

// Standard DX7 operator parameters unpacked from 128-byte voice
struct Dx7OpParams {
    std::array<uint8_t, 4> rates{99, 99, 99, 99};  // R1..R4 (0..99)
    std::array<uint8_t, 4> levels{99, 99, 99, 0};   // L1..L4 (0..99)
    uint8_t output_level = 99;                       // 0..99
    uint8_t key_vel_sens = 0;                        // 0..7
    uint8_t osc_mode = 0;                            // 0 = ratio, 1 = fixed freq
    uint8_t freq_coarse = 1;                         // 0..31
    uint8_t freq_fine = 0;                           // 0..99
    int8_t detune = 0;                               // -7..+7
};

struct Dx7Patch {
    std::string name = "INIT VOICE";
    uint8_t algorithm = 0;                           // 0..31 (algos 1..32)
    uint8_t feedback = 0;                            // 0..7
    bool osc_sync = true;
    std::array<Dx7OpParams, kDx7Operators> ops{};
};

// Operator runtime voice state
struct Dx7OpState {
    float phase = 0.0f;
    float level = 0.0f;
    float target_level = 0.0f;
    float rate_step = 0.0f;
    uint8_t eg_stage = 0; // 0=R1, 1=R2, 2=R3, 3=Sustain, 4=R4(Release), 5=Idle
    float out_val = 0.0f;
};

class Dx7Voice {
public:
    Dx7Voice();

    void trigger(const Dx7Patch& patch, float note_num, float velocity, float sample_rate);
    void release(float sample_rate);
    void reset();

    // Renders one sample of DX7 audio
    float process(const Dx7Patch& patch, float brightness, float decay_scale, float sample_rate,
                  int override_algo = -1, int override_feedback = -1, float detune_scale = 1.0f);

    bool isActive() const { return active_; }

private:
    float calcOpFreq(const Dx7OpParams& op, float base_freq, float detune_scale = 1.0f) const;
    void advanceEg(int op_idx, const Dx7OpParams& op, float decay_scale, float inv_sr);

    std::array<Dx7OpState, kDx7Operators> ops_{};
    float fb_history_[2]{0.0f, 0.0f};
    float base_freq_ = 440.0f;
    float velocity_ = 1.0f;
    bool active_ = false;
};

// DX7 Bank Librarian & Engine
class Dx7Engine {
public:
    Dx7Engine();

    // Load standard 4104-byte .syx cartridge (32 voices)
    bool loadCartridge(const uint8_t* syx_data, size_t size);
    bool loadCartridgeFile(const std::string& filepath);

    int patchCount() const { return static_cast<int>(patches_.size()); }
    const Dx7Patch& getPatch(int index) const;

private:
    void loadEmbeddedRom();
    static bool parseVoice128(const uint8_t* data, Dx7Patch& out_patch);

    std::vector<Dx7Patch> patches_;
};

} // namespace groovebox
