#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace groovebox {

inline constexpr int kMaxSlices = 32;
inline constexpr int kDrumSlots = 8;
inline constexpr uint32_t kMaxSampleSeconds = 16;

struct SlicePoint {
    uint32_t start_frame = 0;
    uint32_t length_frames = 0;
};

enum class RecMode : uint8_t {
    kManual = 0,
    kThreshold,
    kSyncBar
};

enum class RecState : uint8_t {
    kIdle = 0,
    kArmed,
    kRecording,
    kFinished
};

// Семпл у пам'яті (WAV або живий запис)
struct SampleBuffer {
    std::vector<float> data_l;
    std::vector<float> data_r;
    uint32_t sample_rate = 32000;
    std::string name = "EMPTY";
    bool loaded = false;

    uint32_t lengthFrames() const { return static_cast<uint32_t>(data_l.size()); }
    void clear() {
        data_l.clear();
        data_r.clear();
        loaded = false;
        name = "EMPTY";
    }
};

class SamplerEngine {
public:
    SamplerEngine();

    // --- 1. Live Sampling & Looping ---
    void setRecordingMode(RecMode mode) { rec_mode_ = mode; }
    RecMode recordingMode() const { return rec_mode_; }
    RecState recordingState() const { return rec_state_; }

    void armRecording();
    void startRecording();
    void stopRecording();

    // Обробка вхідного стерео аудіоблоку від ALSA Capture
    void processAudioInput(const float* in_l, const float* in_r, uint32_t frames);

    float inputLevelL() const { return input_level_l_.load(std::memory_order_relaxed); }
    float inputLevelR() const { return input_level_r_.load(std::memory_order_relaxed); }
    uint32_t recordedFrames() const { return rec_write_idx_.load(std::memory_order_relaxed); }

    const SampleBuffer& recordedSample() const { return rec_buffer_; }

    // --- 2. Slicer (нарізка на 4..32 слайси) ---
    void sliceGrid(int slice_count = 16);
    void sliceTransients(float sensitivity = 0.5f);
    int sliceCount() const { return active_slices_; }
    SlicePoint getSlice(int idx) const;

    // --- 3. Завантаження WAV семплів ---
    bool loadWavFile(const std::string& path, SampleBuffer& target);
    bool loadDrumKit(const std::string& kit_dir);

    // Слот барабанів
    const SampleBuffer& drumSlot(int slot) const;

private:
    RecMode rec_mode_ = RecMode::kManual;
    std::atomic<RecState> rec_state_{RecState::kIdle};
    std::atomic<uint32_t> rec_write_idx_{0};
    std::atomic<float> input_level_l_{0.0f};
    std::atomic<float> input_level_r_{0.0f};

    SampleBuffer rec_buffer_;
    std::array<SlicePoint, kMaxSlices> slices_{};
    int active_slices_ = 16;

    std::array<SampleBuffer, kDrumSlots> drum_slots_{};
};

} // namespace groovebox
