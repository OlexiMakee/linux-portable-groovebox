#include "sampler_engine.hpp"
#include "rom_samples.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace groovebox {

SamplerEngine::SamplerEngine() {
    // Резервуємо буфер запису (16 секунд @ 48 кГц stereo = 768 000 семплів)
    rec_buffer_.data_l.resize(48000 * kMaxSampleSeconds, 0.0f);
    rec_buffer_.data_r.resize(48000 * kMaxSampleSeconds, 0.0f);
    rec_buffer_.sample_rate = 48000;
    rec_buffer_.name = "LIVE REC";
    sliceGrid(16);

    // Завантажуємо вбудований фабричний ROM-набір ударних у слоти
    for (int i = 0; i < kDrumSlots; ++i) {
        const auto& item = rom::kDefaultDrumKit[i];
        drum_slots_[i].sample_rate = item.sample_rate;
        drum_slots_[i].name = item.name;
        drum_slots_[i].data_l.resize(item.length);
        drum_slots_[i].data_r.resize(item.length);
        for (uint32_t s = 0; s < item.length; ++s) {
            float val = static_cast<float>(item.data[s]) / 32768.0f;
            drum_slots_[i].data_l[s] = val;
            drum_slots_[i].data_r[s] = val;
        }
        drum_slots_[i].loaded = true;
    }
    for (int i = 0; i < 16; ++i) {
        track_samples_[i] = drum_slots_[i % kDrumSlots];
    }
}

void SamplerEngine::armRecording() {
    rec_write_idx_.store(0, std::memory_order_relaxed);
    rec_state_.store(RecState::kArmed, std::memory_order_relaxed);
}

void SamplerEngine::startRecording() {
    rec_write_idx_.store(0, std::memory_order_relaxed);
    rec_state_.store(RecState::kRecording, std::memory_order_relaxed);
}

void SamplerEngine::startOverdub() {
    rec_state_.store(RecState::kOverdub, std::memory_order_relaxed);
}

void SamplerEngine::stopRecording() {
    rec_state_.store(RecState::kFinished, std::memory_order_relaxed);
    sliceGrid(active_slices_);
}

void SamplerEngine::clearRecordedSample() {
    std::fill(rec_buffer_.data_l.begin(), rec_buffer_.data_l.end(), 0.0f);
    std::fill(rec_buffer_.data_r.begin(), rec_buffer_.data_r.end(), 0.0f);
    rec_write_idx_.store(0, std::memory_order_relaxed);
    loop_play_pos_.store(0, std::memory_order_relaxed);
    active_playback_slice_.store(-1, std::memory_order_relaxed);
    rec_state_.store(RecState::kIdle, std::memory_order_relaxed);
}

void SamplerEngine::processAudioInput(const float* in_l, const float* in_r, uint32_t frames) {
    if (!in_l) return;

    float peak_l = 0.0f;
    float peak_r = 0.0f;

    for (uint32_t i = 0; i < frames; ++i) {
        float sl = in_l[i];
        float sr = in_r ? in_r[i] : sl;
        peak_l = std::max(peak_l, std::fabs(sl));
        peak_r = std::max(peak_r, std::fabs(sr));

        auto state = rec_state_.load(std::memory_order_relaxed);

        // Пороговий автозапуск (Threshold Trig)
        if (state == RecState::kArmed && rec_mode_ == RecMode::kThreshold) {
            if (peak_l > 0.035f || peak_r > 0.035f) {
                state = RecState::kRecording;
                rec_state_.store(state, std::memory_order_relaxed);
            }
        }

        if (state == RecState::kRecording) {
            uint32_t idx = rec_write_idx_.load(std::memory_order_relaxed);
            uint32_t target = target_frames_.load(std::memory_order_relaxed);
            if (idx < rec_buffer_.data_l.size() && (target == 0 || idx < target)) {
                rec_buffer_.data_l[idx] = sl;
                rec_buffer_.data_r[idx] = sr;
                rec_write_idx_.store(idx + 1, std::memory_order_relaxed);
                if (target > 0) {
                    active_playback_slice_.store(static_cast<int>((idx * 8) / target), std::memory_order_relaxed);
                }
            } else {
                if (rec_mode_ == RecMode::kSyncBar) {
                    rec_state_.store(RecState::kOverdub, std::memory_order_relaxed);
                    loop_play_pos_.store(0, std::memory_order_relaxed);
                    sliceGrid(8);
                } else {
                    rec_state_.store(RecState::kFinished, std::memory_order_relaxed);
                    sliceGrid(active_slices_);
                }
            }
        } else if (state == RecState::kOverdub) {
            uint32_t target = target_frames_.load(std::memory_order_relaxed);
            if (target > 0) {
                uint32_t cur_pos = loop_play_pos_.load(std::memory_order_relaxed);
                uint32_t next_pos = (cur_pos + 1) % target;
                float fb = overdub_feedback_.load(std::memory_order_relaxed);
                rec_buffer_.data_l[cur_pos] = std::clamp(rec_buffer_.data_l[cur_pos] * fb + sl, -1.0f, 1.0f);
                rec_buffer_.data_r[cur_pos] = std::clamp(rec_buffer_.data_r[cur_pos] * fb + sr, -1.0f, 1.0f);
                loop_play_pos_.store(next_pos, std::memory_order_relaxed);
                active_playback_slice_.store(static_cast<int>((cur_pos * 8) / target), std::memory_order_relaxed);
            }
        }
    }

    float prev_l = input_level_l_.load(std::memory_order_relaxed);
    float prev_r = input_level_r_.load(std::memory_order_relaxed);
    input_level_l_.store(std::max(peak_l, prev_l * 0.85f), std::memory_order_relaxed);
    input_level_r_.store(std::max(peak_r, prev_r * 0.85f), std::memory_order_relaxed);
}

void SamplerEngine::sliceGrid(int slice_count) {
    active_slices_ = std::clamp(slice_count, 1, kMaxSlices);
    uint32_t total = rec_write_idx_.load(std::memory_order_relaxed);
    if (total == 0) total = static_cast<uint32_t>(rec_buffer_.data_l.size());
    uint32_t slice_len = total / active_slices_;

    for (int i = 0; i < active_slices_; ++i) {
        slices_[i].start_frame = i * slice_len;
        slices_[i].length_frames = (i == active_slices_ - 1) ? (total - slices_[i].start_frame) : slice_len;
    }
}

void SamplerEngine::sliceTransients(float sensitivity) {
    uint32_t total = rec_write_idx_.load(std::memory_order_relaxed);
    if (total < 1024) {
        sliceGrid(16);
        return;
    }

    std::vector<uint32_t> transient_points;
    transient_points.push_back(0);

    float threshold = 0.05f + (1.0f - std::clamp(sensitivity, 0.0f, 1.0f)) * 0.35f;
    float prev_energy = 0.0f;
    constexpr uint32_t kWindow = 256;

    for (uint32_t i = kWindow; i + kWindow < total; i += kWindow) {
        float energy = 0.0f;
        for (uint32_t j = 0; j < kWindow; ++j) {
            float s = rec_buffer_.data_l[i + j];
            energy += s * s;
        }
        energy /= static_cast<float>(kWindow);

        if (energy > threshold && energy > prev_energy * 2.2f) {
            // Знайдено перепад енергії (транзієнт)
            if (i - transient_points.back() > 2000) { // мінімум ~50мс між слайсами
                transient_points.push_back(i);
                if (transient_points.size() >= static_cast<size_t>(kMaxSlices)) break;
            }
        }
        prev_energy = energy;
    }

    if (transient_points.size() < 4) {
        sliceGrid(16);
        return;
    }

    active_slices_ = static_cast<int>(transient_points.size());
    for (int i = 0; i < active_slices_; ++i) {
        slices_[i].start_frame = transient_points[i];
        uint32_t next = (i + 1 < active_slices_) ? transient_points[i + 1] : total;
        slices_[i].length_frames = next - transient_points[i];
    }
}

SlicePoint SamplerEngine::getSlice(int idx) const {
    if (idx < 0 || idx >= active_slices_) return SlicePoint{0, 0};
    return slices_[idx];
}

bool SamplerEngine::loadWavFile(const std::string& path, SampleBuffer& target) {
    std::ifstream is(path, std::ios::binary);
    if (!is.is_open()) return false;

    char header[44];
    is.read(header, 44);
    if (is.gcount() < 44) return false;

    // Перевірка "RIFF" та "WAVE"
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t num_channels = *reinterpret_cast<uint16_t*>(header + 22);
    uint32_t sample_rate = *reinterpret_cast<uint32_t*>(header + 24);
    uint16_t bits_per_sample = *reinterpret_cast<uint16_t*>(header + 34);
    uint32_t data_size = *reinterpret_cast<uint32_t*>(header + 40);

    if (num_channels == 0 || bits_per_sample != 16) {
        return false; // Поки підтримуємо стандартний 16-бітний WAV
    }

    uint32_t total_samples = data_size / 2;
    uint32_t frames = total_samples / num_channels;

    std::vector<int16_t> raw(total_samples);
    is.read(reinterpret_cast<char*>(raw.data()), data_size);

    target.data_l.resize(frames);
    target.data_r.resize(frames);
    target.sample_rate = sample_rate;

    for (uint32_t i = 0; i < frames; ++i) {
        if (num_channels == 1) {
            float s = static_cast<float>(raw[i]) / 32768.0f;
            target.data_l[i] = s;
            target.data_r[i] = s;
        } else {
            target.data_l[i] = static_cast<float>(raw[i * 2]) / 32768.0f;
            target.data_r[i] = static_cast<float>(raw[i * 2 + 1]) / 32768.0f;
        }
    }

    target.name = path.substr(path.find_last_of("/\\") + 1);
    target.loaded = true;
    return true;
}

bool SamplerEngine::loadDrumKit(const std::string& kit_dir) {
    static const char* kFileNames[kDrumSlots] = {
        "bd.wav", "sd.wav", "ch.wav", "oh.wav", "cp.wav", "pc.wav", "ht.wav", "cr.wav"
    };
    bool any_loaded = false;
    for (int i = 0; i < kDrumSlots; ++i) {
        std::string p = kit_dir + "/" + kFileNames[i];
        if (loadWavFile(p, drum_slots_[i])) {
            any_loaded = true;
        }
    }
    return any_loaded;
}

const SampleBuffer& SamplerEngine::drumSlot(int slot) const {
    int s = std::clamp(slot, 0, kDrumSlots - 1);
    return drum_slots_[s];
}

void SamplerEngine::setDrumSlot(int slot, SampleBuffer buf) {
    int s = std::clamp(slot, 0, kDrumSlots - 1);
    drum_slots_[s] = std::move(buf);
}

void SamplerEngine::armSyncedRecording(uint32_t bars, float bpm, uint32_t sample_rate) {
    float beat_sec = 60.0f / std::max(40.0f, bpm);
    float total_sec = bars * 4.0f * beat_sec;
    target_frames_.store(static_cast<uint32_t>(total_sec * sample_rate), std::memory_order_relaxed);
    rec_mode_ = RecMode::kSyncBar;
    armRecording();
}

void SamplerEngine::assignTrimmedToSlot(int slot, float trim_st, float trim_end) {
    int s = std::clamp(slot, 0, kDrumSlots - 1);
    uint32_t total = rec_write_idx_.load(std::memory_order_relaxed);
    if (total == 0) total = static_cast<uint32_t>(rec_buffer_.data_l.size());
    if (total == 0) return;

    uint32_t st_f = static_cast<uint32_t>(std::clamp(trim_st, 0.0f, 0.99f) * total);
    uint32_t end_f = static_cast<uint32_t>(std::clamp(trim_end, trim_st + 0.01f, 1.0f) * total);
    if (end_f <= st_f) end_f = std::min(total, st_f + 64);

    uint32_t len = end_f - st_f;
    SampleBuffer buf;
    buf.sample_rate = rec_buffer_.sample_rate;
    buf.name = "SMPL " + std::to_string(s + 1);
    buf.data_l.resize(len);
    buf.data_r.resize(len);

    float peak = 0.001f;
    for (uint32_t i = 0; i < len; ++i) {
        float l = rec_buffer_.data_l[st_f + i];
        float r = rec_buffer_.data_r[st_f + i];
        buf.data_l[i] = l;
        buf.data_r[i] = r;
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    // Автонормалізація до 0.95
    if (peak > 0.001f) {
        float gain = 0.95f / peak;
        for (uint32_t i = 0; i < len; ++i) {
            buf.data_l[i] = std::clamp(buf.data_l[i] * gain, -1.0f, 1.0f);
            buf.data_r[i] = std::clamp(buf.data_r[i] * gain, -1.0f, 1.0f);
        }
    }
    buf.loaded = true;
    drum_slots_[s] = std::move(buf);
}

void SamplerEngine::sliceToDrumSlots(float trim_st, float trim_end) {
    uint32_t total = rec_write_idx_.load(std::memory_order_relaxed);
    if (total == 0) total = static_cast<uint32_t>(rec_buffer_.data_l.size());
    if (total == 0) return;

    uint32_t st_f = static_cast<uint32_t>(std::clamp(trim_st, 0.0f, 0.99f) * total);
    uint32_t end_f = static_cast<uint32_t>(std::clamp(trim_end, trim_st + 0.01f, 1.0f) * total);
    if (end_f <= st_f) end_f = std::min(total, st_f + 512);

    uint32_t span = end_f - st_f;
    uint32_t slice_len = span / kDrumSlots;
    if (slice_len < 16) return;

    for (int i = 0; i < kDrumSlots; ++i) {
        SampleBuffer buf;
        buf.sample_rate = rec_buffer_.sample_rate;
        buf.name = "SLC " + std::to_string(i + 1);
        buf.data_l.resize(slice_len);
        buf.data_r.resize(slice_len);
        uint32_t offset = st_f + i * slice_len;

        float peak = 0.001f;
        for (uint32_t s = 0; s < slice_len; ++s) {
            float l = rec_buffer_.data_l[offset + s];
            float r = rec_buffer_.data_r[offset + s];
            buf.data_l[s] = l;
            buf.data_r[s] = r;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        if (peak > 0.001f) {
            float gain = 0.95f / peak;
            for (uint32_t s = 0; s < slice_len; ++s) {
                buf.data_l[s] = std::clamp(buf.data_l[s] * gain, -1.0f, 1.0f);
                buf.data_r[s] = std::clamp(buf.data_r[s] * gain, -1.0f, 1.0f);
            }
        }
        buf.loaded = true;
        drum_slots_[i] = std::move(buf);
    }
}

const SampleBuffer& SamplerEngine::trackSample(int track) const {
    int t = std::clamp(track, 0, 15);
    if (track_samples_[t].loaded) return track_samples_[t];
    if (drum_slots_[t % kDrumSlots].loaded) return drum_slots_[t % kDrumSlots];
    return rec_buffer_;
}

SampleBuffer& SamplerEngine::trackSample(int track) {
    int t = std::clamp(track, 0, 15);
    return track_samples_[t];
}

void SamplerEngine::setTrackSample(int track, SampleBuffer buf) {
    int t = std::clamp(track, 0, 15);
    track_samples_[t] = std::move(buf);
}

} // namespace groovebox
