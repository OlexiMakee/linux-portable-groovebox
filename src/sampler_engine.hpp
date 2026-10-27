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
    kOverdub,
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
    void startOverdub();
    void stopRecording();
    void clearRecordedSample();

    float overdubFeedback() const { return overdub_feedback_.load(std::memory_order_relaxed); }
    void setOverdubFeedback(float fb) { overdub_feedback_.store(std::clamp(fb, 0.20f, 1.0f), std::memory_order_relaxed); }

    int activePlaybackSlice() const { return active_playback_slice_.load(std::memory_order_relaxed); }
    void setActivePlaybackSlice(int s) { active_playback_slice_.store(s, std::memory_order_relaxed); }
    uint32_t loopPlaybackFrame() const { return loop_play_pos_.load(std::memory_order_relaxed); }

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
    void setDrumSlot(int slot, SampleBuffer buf);

    // --- 4. Live Looper & Slice Assignment ---
    void assignTrimmedToSlot(int slot, float trim_st = 0.0f, float trim_end = 1.0f);
    void sliceToDrumSlots(float trim_st = 0.0f, float trim_end = 1.0f);
    void armSyncedRecording(uint32_t bars, float bpm, uint32_t sample_rate);

private:
    RecMode rec_mode_ = RecMode::kManual;
    std::atomic<RecState> rec_state_{RecState::kIdle};
    std::atomic<uint32_t> rec_write_idx_{0};
    std::atomic<uint32_t> target_frames_{0}; // для темпо-синхронізованого запису
    std::atomic<uint32_t> loop_play_pos_{0};
    std::atomic<int> active_playback_slice_{-1};
    std::atomic<float> overdub_feedback_{0.85f};
    std::atomic<float> input_level_l_{0.0f};
    std::atomic<float> input_level_r_{0.0f};

    SampleBuffer rec_buffer_;
    std::array<SlicePoint, kMaxSlices> slices_{};
    int active_slices_ = 16;

    std::array<SampleBuffer, kDrumSlots> drum_slots_{};
};

} // namespace groovebox
