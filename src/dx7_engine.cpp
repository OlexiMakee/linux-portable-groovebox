#include "dx7_engine.hpp"
#include "rom_samples.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace groovebox {

// 32 DX7 Algorithms Definition:
// For each algorithm:
//   mod_in[op]: bitmask of operators (0..5) that modulate operator op
//   carrier_mask: bitmask of operators whose output sums into audio output
//   feedback_op: operator index (0..5) that feeds back into itself
struct Dx7AlgoDef {
    uint8_t mod_in[kDx7Operators];
    uint8_t carrier_mask;
    int8_t feedback_op;
};

// DX7 operators in standard documentation are numbered 1 to 6 (we use 0-indexed: Op1=0, Op2=1, ..., Op6=5).
// Standard Yamaha convention: Op1 is bottom left (carrier), Op6 is top right (modulator with feedback).
static constexpr Dx7AlgoDef kAlgorithms[32] = {
    // Algo 1: 6->6, 6->5->4->3->2->1 (carrier)
    {{0x02, 0x04, 0x08, 0x10, 0x20, 0x00}, 0x01, 5},
    // Algo 2: 2->2, 2->1; 6->5->4->3->1 (carrier)
    {{0x06, 0x00, 0x08, 0x10, 0x20, 0x00}, 0x01, 1},
    // Algo 3: 6->6, 6->5->4->3, 2->1 (carriers: 1, 3)
    {{0x02, 0x00, 0x08, 0x10, 0x20, 0x00}, 0x05, 5},
    // Algo 4: 4->4, 4->3; 6->5->2->1 (carriers: 1, 3)
    {{0x02, 0x10, 0x08, 0x00, 0x20, 0x00}, 0x05, 3},
    // Algo 5: 6->6, 6->5, 4->3, 2->1 (carriers: 1, 3, 5 - Classic E.PIANO!)
    {{0x02, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x15, 5},
    // Algo 6: 5->5, 6->5, 4->3, 2->1 (carriers: 1, 3, 5)
    {{0x02, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x15, 4},
    // Algo 7: 6->6, 6->5, 4->3, 3->2->1 (carriers: 1, 3, 5)
    {{0x02, 0x04, 0x08, 0x00, 0x20, 0x00}, 0x15, 5},
    // Algo 8: 4->4, 4->3, 6->5, 2->1 (carriers: 1, 3, 5)
    {{0x02, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x15, 3},
    // Algo 9: 2->2, 2->1, 6->5, 4->3 (carriers: 1, 3, 5)
    {{0x02, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x15, 1},
    // Algo 10: 3->3, 3->2->1; 6->5->4 (carriers: 1, 4)
    {{0x02, 0x04, 0x00, 0x10, 0x20, 0x00}, 0x09, 2},
    // Algo 11: 6->6, 6->5->4; 3->2->1 (carriers: 1, 4)
    {{0x02, 0x04, 0x00, 0x10, 0x20, 0x00}, 0x09, 5},
    // Algo 12: 2->2, 2->1; 6->5->4, 3->4 (carriers: 1, 4)
    {{0x02, 0x00, 0x00, 0x34, 0x20, 0x00}, 0x09, 1},
    // Algo 13: 6->6, 6->5->4, 3->4; 2->1 (carriers: 1, 4)
    {{0x02, 0x00, 0x00, 0x34, 0x20, 0x00}, 0x09, 5},
    // Algo 14: 6->6, 6->5->4, 4->3->2->1 (carrier: 1)
    {{0x02, 0x04, 0x08, 0x10, 0x20, 0x00}, 0x01, 5},
    // Algo 15: 2->2, 2->1; 6->5->4, 4->3->1 (carrier: 1)
    {{0x06, 0x04, 0x08, 0x10, 0x20, 0x00}, 0x01, 1},
    // Algo 16: 6->6, 6->5->4->3->1; 2->1 (carrier: 1 - Classic BASS 1!)
    {{0x06, 0x00, 0x08, 0x10, 0x20, 0x00}, 0x01, 5},
    // Algo 17: 2->2, 2->1; 6->5->4->3->1 (carrier: 1)
    {{0x06, 0x00, 0x08, 0x10, 0x20, 0x00}, 0x01, 1},
    // Algo 18: 3->3, 3->2->1; 6->5->4->1 (carrier: 1 - SYN-LEAD!)
    {{0x0A, 0x04, 0x00, 0x10, 0x20, 0x00}, 0x01, 2},
    // Algo 19: 6->6, 6->5->4->1; 3->2->1 (carrier: 1 - PIANO 1!)
    {{0x0A, 0x04, 0x00, 0x10, 0x20, 0x00}, 0x01, 5},
    // Algo 20: 3->3, 3->2->1; 6->5->1; 4->1 (carrier: 1)
    {{0x1A, 0x04, 0x00, 0x00, 0x20, 0x00}, 0x01, 2},
    // Algo 21: 3->3, 3->2->1; 6->5->1; 5->4 (carriers: 1, 4)
    {{0x12, 0x04, 0x00, 0x10, 0x20, 0x00}, 0x09, 2},
    // Algo 22: 6->6, 6->5->4->1; 3->1; 2->1 (carrier: 1 - BRASS 1!)
    {{0x0E, 0x00, 0x00, 0x10, 0x20, 0x00}, 0x01, 5},
    // Algo 23: 6->6, 6->5->1; 4->1; 3->1; 2->1 (carrier: 1)
    {{0x1E, 0x00, 0x00, 0x00, 0x20, 0x00}, 0x01, 5},
    // Algo 24: 6->6, 6->5->1; 4->1; 3->1; 2->1 (carriers: 1, 2, 3, 4)
    {{0x10, 0x00, 0x00, 0x00, 0x20, 0x00}, 0x0F, 5},
    // Algo 25: 6->6, 6->5->1; 4->1; 3->1; 2->1 (carriers: 1, 2, 3, 4)
    {{0x00, 0x00, 0x00, 0x00, 0x20, 0x00}, 0x0F, 5},
    // Algo 26: 6->6, 6->5->4; 3->2->1 (carriers: 1, 2, 4)
    {{0x02, 0x00, 0x00, 0x10, 0x20, 0x00}, 0x0B, 5},
    // Algo 27: 3->3, 3->2->1; 6->5->4 (carriers: 1, 2, 4)
    {{0x02, 0x04, 0x00, 0x10, 0x20, 0x00}, 0x0B, 2},
    // Algo 28: 5->5, 6->5; 4->3->1; 2->1 (carriers: 1, 3, 5)
    {{0x06, 0x00, 0x08, 0x00, 0x20, 0x00}, 0x15, 4},
    // Algo 29: 6->6, 6->5; 4->1; 3->1; 2->1 (carriers: 1, 2, 3, 5)
    {{0x00, 0x00, 0x00, 0x00, 0x20, 0x00}, 0x17, 5},
    // Algo 30: 5->5, 6->5; 4->1; 3->1; 2->1 (carriers: 1, 2, 3, 5)
    {{0x00, 0x00, 0x00, 0x00, 0x20, 0x00}, 0x17, 4},
    // Algo 31: 6->6, 6->5; 4; 3; 2; 1 (carriers: 1, 2, 3, 4, 5)
    {{0x00, 0x00, 0x00, 0x00, 0x20, 0x00}, 0x1F, 5},
    // Algo 32: All 6 parallel (carriers: 1, 2, 3, 4, 5, 6 - Organ / Additive!)
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0x3F, 5}
};

// Fast 4096-entry Sine Table
static constexpr int kSineLutSize = 4096;
static float s_sine_lut[kSineLutSize + 1];
static bool s_sine_lut_init = false;

static void ensureSineLut() {
    if (s_sine_lut_init) return;
    for (int i = 0; i <= kSineLutSize; ++i) {
        float phase = static_cast<float>(i) / static_cast<float>(kSineLutSize);
        s_sine_lut[i] = std::sin(phase * 6.28318530718f);
    }
    s_sine_lut_init = true;
}

static inline float fastSin(float phase) {
    if (phase < 0.0f) phase -= std::floor(phase);
    else if (phase >= 1.0f) phase -= std::floor(phase);

    float pos = phase * static_cast<float>(kSineLutSize);
    int idx = static_cast<int>(pos);
    float frac = pos - static_cast<float>(idx);
    float s0 = s_sine_lut[idx];
    float s1 = s_sine_lut[idx + 1];
    return s0 + frac * (s1 - s0);
}

// Convert DX7 rate (0..99) to seconds
static inline float dx7RateToTime(uint8_t rate) {
    if (rate >= 99) return 0.002f;
    float r = static_cast<float>(rate);
    return 0.002f + std::pow(2.0f, (99.0f - r) * 0.12f) * 0.015f;
}

// Convert DX7 level (0..99) to normalized linear gain (0.0 .. 1.0)
static inline float dx7LevelToGain(uint8_t level) {
    if (level == 0) return 0.0f;
    // DX7 levels are logarithmic: roughly 0.75 dB per step
    float db = (static_cast<float>(level) - 99.0f) * 0.75f;
    return std::pow(10.0f, db * 0.05f);
}

Dx7Voice::Dx7Voice() {
    ensureSineLut();
    reset();
}

void Dx7Voice::reset() {
    for (int i = 0; i < kDx7Operators; ++i) {
        ops_[i].phase = 0.0f;
        ops_[i].level = 0.0f;
        ops_[i].target_level = 0.0f;
        ops_[i].rate_step = 0.0f;
        ops_[i].eg_stage = 5; // Idle
        ops_[i].out_val = 0.0f;
    }
    fb_history_[0] = 0.0f;
    fb_history_[1] = 0.0f;
    active_ = false;
}

float Dx7Voice::calcOpFreq(const Dx7OpParams& op, float base_freq) const {
    if (op.osc_mode == 1) {
        // Fixed frequency mode
        float coarse = static_cast<float>(op.freq_coarse);
        float fine = static_cast<float>(op.freq_fine) * 0.01f;
        return std::pow(10.0f, (coarse + fine) * 0.25f);
    }
    // Ratio mode
    float coarse = (op.freq_coarse == 0) ? 0.5f : static_cast<float>(op.freq_coarse);
    float fine = static_cast<float>(op.freq_fine) * 0.01f;
    float ratio = coarse + fine;
    float detune_factor = 1.0f + static_cast<float>(op.detune) * 0.0015f;
    return base_freq * ratio * detune_factor;
}

void Dx7Voice::trigger(const Dx7Patch& patch, float note_num, float velocity, float sample_rate) {
    ensureSineLut();
    base_freq_ = 440.0f * std::pow(2.0f, (note_num - 69.0f) / 12.0f);
    velocity_ = std::clamp(velocity, 0.01f, 1.0f);
    active_ = true;

    fb_history_[0] = 0.0f;
    fb_history_[1] = 0.0f;

    for (int i = 0; i < kDx7Operators; ++i) {
        auto& s = ops_[i];
        const auto& p = patch.ops[i];
        if (patch.osc_sync) s.phase = 0.0f;
        s.eg_stage = 0; // Stage 1 (R1 -> L1)
        s.level = dx7LevelToGain(p.levels[3]); // Start from L4
        s.target_level = dx7LevelToGain(p.levels[0]);
        float t = dx7RateToTime(p.rates[0]);
        s.rate_step = (s.target_level - s.level) / (t * sample_rate);
    }
}

void Dx7Voice::release(float sample_rate) {
    if (!active_) return;
    for (int i = 0; i < kDx7Operators; ++i) {
        auto& s = ops_[i];
        s.eg_stage = 4; // Stage 4: Release (R4 -> L4)
        s.target_level = 0.0f;
        // Rate 4 is release
        s.rate_step = -s.level / (0.05f * sample_rate + 0.001f);
    }
}

void Dx7Voice::advanceEg(int op_idx, const Dx7OpParams& op, float decay_scale, float inv_sr) {
    auto& s = ops_[op_idx];
    if (s.eg_stage >= 5) return; // Idle

    s.level += s.rate_step;

    if (s.eg_stage == 0) { // R1 -> L1
        if ((s.rate_step >= 0.0f && s.level >= s.target_level) ||
            (s.rate_step < 0.0f && s.level <= s.target_level)) {
            s.level = s.target_level;
            s.eg_stage = 1;
            s.target_level = dx7LevelToGain(op.levels[1]);
            float t = dx7RateToTime(op.rates[1]) * decay_scale;
            s.rate_step = (s.target_level - s.level) * inv_sr / std::max(0.001f, t);
        }
    } else if (s.eg_stage == 1) { // R2 -> L2
        if ((s.rate_step >= 0.0f && s.level >= s.target_level) ||
            (s.rate_step < 0.0f && s.level <= s.target_level)) {
            s.level = s.target_level;
            s.eg_stage = 2;
            s.target_level = dx7LevelToGain(op.levels[2]);
            float t = dx7RateToTime(op.rates[2]) * decay_scale;
            s.rate_step = (s.target_level - s.level) * inv_sr / std::max(0.001f, t);
        }
    } else if (s.eg_stage == 2) { // R3 -> L3 (Sustain stage)
        if ((s.rate_step >= 0.0f && s.level >= s.target_level) ||
            (s.rate_step < 0.0f && s.level <= s.target_level)) {
            s.level = s.target_level;
            s.eg_stage = 3; // Sustain reached
            s.rate_step = 0.0f;
        }
    } else if (s.eg_stage == 4) { // R4 -> L4 (Release)
        if (s.level <= 0.0005f) {
            s.level = 0.0f;
            s.eg_stage = 5; // Idle
        }
    }
}

float Dx7Voice::process(const Dx7Patch& patch, float brightness, float decay_scale, float sample_rate) {
    if (!active_) return 0.0f;

    const float inv_sr = 1.0f / sample_rate;
    int algo_idx = std::clamp(static_cast<int>(patch.algorithm), 0, 31);
    const auto& algo = kAlgorithms[algo_idx];

    // Check if all carriers are finished
    bool any_carrier_active = false;
    for (int i = 0; i < kDx7Operators; ++i) {
        if ((algo.carrier_mask & (1 << i)) && ops_[i].eg_stage < 5 && ops_[i].level > 0.0005f) {
            any_carrier_active = true;
            break;
        }
    }
    if (!any_carrier_active && ops_[0].eg_stage == 4) {
        active_ = false;
        return 0.0f;
    }

    // Process operators from 5 down to 0 (Op6 to Op1)
    float audio_out = 0.0f;
    float fb_out = (fb_history_[0] + fb_history_[1]) * 0.5f;

    for (int i = kDx7Operators - 1; i >= 0; --i) {
        const auto& op_params = patch.ops[i];
        advanceEg(i, op_params, decay_scale, inv_sr);

        float op_freq = calcOpFreq(op_params, base_freq_);
        float phase_inc = op_freq * inv_sr;
        ops_[i].phase += phase_inc;
        if (ops_[i].phase >= 1.0f) ops_[i].phase -= std::floor(ops_[i].phase);

        // Sum phase modulation from operators modulating op 'i'
        float phase_mod = 0.0f;
        uint8_t mod_mask = algo.mod_in[i];
        for (int m = 0; m < kDx7Operators; ++m) {
            if (mod_mask & (1 << m)) {
                phase_mod += ops_[m].out_val;
            }
        }

        // Apply feedback if this operator has self-feedback
        if (algo.feedback_op == i && patch.feedback > 0) {
            float fb_amt = static_cast<float>(1 << (patch.feedback - 1)) * 0.12f;
            phase_mod += fb_out * fb_amt;
        }

        float sin_out = fastSin(ops_[i].phase + phase_mod);

        // Scale by operator level and envelope
        float op_out_level = dx7LevelToGain(op_params.output_level);
        if (!(algo.carrier_mask & (1 << i))) {
            // Modulator brightness scaling
            op_out_level *= brightness;
        }

        float val = sin_out * ops_[i].level * op_out_level;
        ops_[i].out_val = val;

        if (algo.feedback_op == i) {
            fb_history_[1] = fb_history_[0];
            fb_history_[0] = val;
        }

        // Accumulate carrier outputs
        if (algo.carrier_mask & (1 << i)) {
            audio_out += val;
        }
    }

    return audio_out * velocity_ * 0.55f;
}

// -------------------------------------------------------------
// Dx7Engine Bank & Preset Management
// -------------------------------------------------------------

Dx7Engine::Dx7Engine() {
    ensureSineLut();
    loadEmbeddedRom();
}

void Dx7Engine::loadEmbeddedRom() {
    loadCartridge(rom::kDx7Rom1A, sizeof(rom::kDx7Rom1A));
}

bool Dx7Engine::parseVoice128(const uint8_t* data, Dx7Patch& p) {
    if (!data) return false;

    // Operator data in DX7 voice is stored in reverse order: Op6 down to Op1
    // Each operator is 17 bytes: 17 * 6 = 102 bytes
    for (int op = 0; op < kDx7Operators; ++op) {
        int src_op = 5 - op; // map Op6..Op1 to ops_[5]..ops_[0]
        const uint8_t* op_data = data + op * 17;
        auto& target = p.ops[src_op];

        target.rates[0] = op_data[0] & 0x7F;
        target.rates[1] = op_data[1] & 0x7F;
        target.rates[2] = op_data[2] & 0x7F;
        target.rates[3] = op_data[3] & 0x7F;

        target.levels[0] = op_data[4] & 0x7F;
        target.levels[1] = op_data[5] & 0x7F;
        target.levels[2] = op_data[6] & 0x7F;
        target.levels[3] = op_data[7] & 0x7F;

        target.key_vel_sens = (op_data[13] >> 2) & 0x07;
        target.output_level = op_data[14] & 0x7F;
        target.osc_mode = op_data[15] & 0x01;
        target.freq_coarse = (op_data[15] >> 1) & 0x1F;
        target.freq_fine = op_data[16] & 0x7F;
        target.detune = static_cast<int8_t>((op_data[13] >> 5) & 0x07) - 3;
    }

    // Offset 110: Algorithm (0..31)
    p.algorithm = data[110] & 0x1F;
    // Offset 111: Feedback (bits 0..2) & Osc Sync (bit 3)
    p.feedback = data[111] & 0x07;
    p.osc_sync = (data[111] & 0x08) != 0;

    // Offset 118..127: 10-char ASCII voice name
    char name_buf[11]{};
    for (int c = 0; c < 10; ++c) {
        uint8_t ch = data[118 + c];
        name_buf[c] = (ch >= 32 && ch < 127) ? static_cast<char>(ch) : ' ';
    }
    // Trim trailing whitespace
    int end = 9;
    while (end >= 0 && name_buf[end] == ' ') {
        name_buf[end] = '\0';
        end--;
    }
    p.name = (end >= 0) ? std::string(name_buf) : "INIT VOICE";
    return true;
}

bool Dx7Engine::loadCartridge(const uint8_t* syx_data, size_t size) {
    if (!syx_data || size < 4096) return false;

    // If file has standard 6-byte SysEx header (F0 43 00 09 20 00), data starts at offset 6
    size_t offset = (size >= 4104 && syx_data[0] == 0xF0) ? 6 : 0;
    if (offset + 4096 > size) return false;

    patches_.clear();
    patches_.reserve(kDx7PresetsPerCartridge);

    for (int i = 0; i < kDx7PresetsPerCartridge; ++i) {
        Dx7Patch patch;
        parseVoice128(syx_data + offset + i * 128, patch);
        patches_.push_back(std::move(patch));
    }
    return true;
}

bool Dx7Engine::loadCartridgeFile(const std::string& filepath) {
    std::ifstream is(filepath, std::ios::binary);
    if (!is.is_open()) return false;

    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    return loadCartridge(buf.data(), buf.size());
}

const Dx7Patch& Dx7Engine::getPatch(int index) const {
    static const Dx7Patch kFallback;
    if (patches_.empty()) return kFallback;
    int idx = std::clamp(index, 0, static_cast<int>(patches_.size()) - 1);
    return patches_[idx];
}

} // namespace groovebox
