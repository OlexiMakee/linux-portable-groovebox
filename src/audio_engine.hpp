#pragma once

#include "dx7_engine.hpp"
#include "net_audio_streamer.hpp"
#include "sampler_engine.hpp"
#include "synth_engines.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace groovebox {

// Спільні макро-параметри живого виступу (з аналогових стіків та VOL+/VOL-)
struct SynthParams {
    std::atomic<float> master_volume{0.75f};  // 0.0 .. 1.0 (керується VOL+ / VOL-)
    std::atomic<float> cutoff_macro{0.0f};    // -0.45 .. +0.45 (лівий стік X — офсет фільтра)
    std::atomic<float> resonance_macro{0.0f}; // -0.45 .. +0.45 (правий стік Y — офсет резонансу)
    std::atomic<float> env_mod_macro{0.0f};   // -0.45 .. +0.45 (лівий стік Y — офсет Env Mod)
    std::atomic<float> fm_macro{0.0f};        // -0.45 .. +0.45 (правий стік X — офсет FM/Detune)
    std::atomic<float> stick_lx{0.0f};        // -0.45 .. +0.45 (raw left stick X)
    std::atomic<float> stick_ly{0.0f};        // -0.45 .. +0.45 (raw left stick Y)
    std::atomic<float> stick_rx{0.0f};        // -0.45 .. +0.45 (raw right stick X)
    std::atomic<float> stick_ry{0.0f};        // -0.45 .. +0.45 (raw right stick Y)
    std::atomic<float> delay_feedback{0.42f}; // Стерео-ділей фідбек
    std::atomic<float> delay_mix{0.25f};      // Рівень повернення стерео-ділею
    std::atomic<float> reverb_decay{0.50f};   // Рівень реверберації
    std::atomic<float> reverb_mix{0.20f};     // Повернення реверберації
    std::atomic<float> chorus_rate{0.80f};    // Частота хоруса (0.1..5.0 Гц)
    std::atomic<float> chorus_depth{0.45f};   // Глибина хоруса (0.0..1.0)
    std::atomic<float> chorus_mix{0.00f};     // Мікс хоруса (0.0..1.0)
    std::atomic<float> drive_amount{0.00f};   // Насичення овердрайву (0.0..1.0)
    std::atomic<float> drive_tone{0.65f};     // Тембр овердрайву (0.0..1.0)
};

// Стан аудіорушія для відображення в UI, осцилографі та VU-метрі
struct AudioEngineTelemetry {
    uint32_t sample_rate = 32000;
    uint32_t period_frames = 128;
    uint32_t xrun_count = 0;
    std::string device_label = "RK817 SPK (32000 Hz)";
    float vu_left = 0.0f;
    float vu_right = 0.0f;
    float peak_hold = 0.0f;
    std::array<float, 4> track_levels{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 256> scope_samples{};
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool start(int card_index, bool is_usb_48k, bool headphones_plugged);
    void stop();

    void updateRouting(int card_index, bool is_usb_48k, bool headphones_plugged);
    void adjustMasterVolume(float delta);

    // Оверрайд аудіовиходу: Auto (USB за наявності) <-> Force Internal (RK817)
    void toggleOutputOverride();
    bool isOutputForcedInternal() const;

    // Циклічне перемикання розміру буфера/періоду (128 -> 256 -> 512 -> 1024 -> 2048)
    void cycleBufferSize();
    uint32_t getTargetPeriodFrames() const { return target_period_frames_.load(std::memory_order_relaxed); }

    // Доступ та модифікація модульних параметрів 4 доріжок і 16 рядків x 128 кроків секвенсора
    TrackParameters getTrackParams(int track) const;
    void setTrackParams(int track, const TrackParameters& tp);
    void cycleTrackEngine(int track, int dir);
    void adjustTrackParam(int track, int param_idx, float delta);

    SequencerStep getStep(int row, int step_idx) const;
    void setStep(int row, int step_idx, const SequencerStep& step);
    void toggleStep(int row, int step_idx);
    void clearStep(int row, int step_idx);
    void clearRow(int row);
    void deleteRow(int row);
    void randomizeRow(int row);
    void previewStep(int row, int step_idx);
    void toggleParamTrig(int row, int step_idx); // Жовтий Elektron Trig (Parameter Lock без Note-On)
    void adjustStepNote(int row, int step_idx, int semitones);
    void lockStepCutoff(int row, int step_idx, float cutoff_val);

    // Кількість активних рядків секвенсора (за замовчуванням 8: 4 drums + 4 VA)
    int getActiveRowCount() const { return active_rows_count_; }
    void setActiveRowCount(int count) { active_rows_count_ = std::clamp(count, 1, kTotalRows); }

    // Повне скидання звуку (Panic / Kill all voices + clear delay)
    void panic();

    // Тригер ноти для конкретного голосу/рядка (row = 0..15)
    void triggerVoice(int row, int midi_note, float velocity = 0.9f, float plock_cutoff = -1.0f, bool slide = false, float gate_len = 0.60f);
    // Поліфонічний тригер ноти для треку (динамічний розподіл на 4 голоси треку)
    void triggerNote(int track, int midi_note, float velocity = 0.9f, float plock_cutoff = -1.0f, bool slide = false, float gate_len = 0.60f);
    // Поліфонічне відпускання ноти (Note-Off) для треку
    void noteOff(int track, int midi_note);
    void noteOffAll();

    using MidiOutCallback = std::function<void(int channel, int note, float velocity, bool note_on)>;
    void setMidiOutCallback(MidiOutCallback cb) { midi_out_cb_ = std::move(cb); }

    // Тригер кроку секвенсора (master_tick 0..unlimited)
    void triggerSequencerStep(uint32_t master_tick);
    uint32_t getPatternLength() const { return pattern_length_.load(std::memory_order_relaxed); }
    void setPatternLength(uint32_t len) { pattern_length_.store(std::clamp(len, 16u, 128u)); }

    // Поліметрія: незалежні довжини для кожного з 16 рядків (16..128 кроків)
    uint32_t getRowLength(int row) const;
    void setRowLength(int row, uint32_t len);
    uint32_t cycleRowLength(int row);

    // Мапінг рядків на треки та голоси (моно-девайси займають 1 рядок, полі - кілька)
    int getRowTrack(int row) const;
    int getRowVoice(int row) const;

    // Elektron Fill Mode (активується затисканням кліка лівого стіка L3)
    void setFillMode(bool fill) { fill_mode_.store(fill, std::memory_order_relaxed); }
    bool getFillMode() const { return fill_mode_.load(std::memory_order_relaxed); }

    SynthParams& params() { return params_; }
    const SynthParams& params() const { return params_; }

    NetAudioStreamer& netStreamer() { return net_streamer_; }

    void setSampler(SamplerEngine* s) { sampler_ptr_ = s; }
    SamplerEngine& sampler() { return sampler_ptr_ ? *sampler_ptr_ : local_sampler_; }
    const SamplerEngine& sampler() const { return sampler_ptr_ ? *sampler_ptr_ : local_sampler_; }

    Dx7Engine& dx7() { return dx7_engine_; }
    const Dx7Engine& dx7() const { return dx7_engine_; }

    AudioEngineTelemetry getTelemetry() const;

private:
    void initDefaultPattern();
    void renderBlock(int16_t* out_interleaved, uint32_t frames, uint32_t sample_rate);
    void applyRk817HardwareMux(bool headphones_plugged);
    void applyRk817HardwareVolume(float vol_norm);

#if defined(__linux__)
    void alsaThreadLoop();
#endif

    SynthParams params_;
    mutable std::mutex state_mutex_;
    std::array<TrackParameters, 4> tracks_{};
    std::atomic<uint32_t> pattern_length_{64};
    std::array<std::atomic<uint32_t>, kTotalRows> row_lengths_{};
    std::array<uint8_t, kTotalRows> row_track_{};
    std::array<uint8_t, kTotalRows> row_voice_{};
    std::array<std::array<SequencerStep, kMaxSteps>, kTotalRows> pattern_{};
    int active_rows_count_ = 8;
    std::atomic<bool> fill_mode_{false};
    uint32_t cycle_counter_ = 0;
    std::array<bool, kTotalRows> prev_step_fired_{};
    uint32_t voice_alloc_counter_ = 0;
    int allocateVoice(int track, int midi_note);
    MidiOutCallback midi_out_cb_;

    struct VoiceState {
        bool active = false;
        uint8_t note_num = 48;
        uint32_t trig_time = 0;
        float freq = 110.0f;
        float current_pitch = 48.0f;
        float target_pitch = 48.0f;
        float glide_coef = 0.0f;
        bool is_legato = false;
        int32_t gate_samples_left = -1;
        float phase1 = 0.0f;
        float phase2 = 0.0f;
        float phase3 = 0.0f;
        float phase4 = 0.0f;
        AdsrEnvelope amp_adsr{};
        AdsrEnvelope filter_adsr{};
        float pitch_env = 0.0f;
        float velocity = 1.0f;
        float svf_low = 0.0f;
        float svf_band = 0.0f;
        float last_out = 0.0f;
        float plock_cutoff = -1.0f;
        uint8_t drum_slot = 0;
        Dx7Voice dx7_voice{};
    };

    std::array<VoiceState, kTotalRows> voices_{};
    uint32_t rng_state_ = 0x12345678u;

    // Стерео пінг-понг ділей буфер (до 0.5 сек на 48k)
    std::vector<float> delay_buf_l_;
    std::vector<float> delay_buf_r_;
    uint32_t delay_idx_ = 0;

    // Стерео ревербератор
    std::vector<float> reverb_buf_l_;
    std::vector<float> reverb_buf_r_;
    uint32_t reverb_idx_ = 0;

    // Стерео хорус та сатуратор
    std::vector<float> chorus_buf_l_;
    std::vector<float> chorus_buf_r_;
    uint32_t chorus_idx_ = 0;
    float chorus_phase_ = 0.0f;
    float drive_lp_l_ = 0.0f;
    float drive_lp_r_ = 0.0f;

    NetAudioStreamer net_streamer_;

    std::atomic<bool> running_{false};
    std::atomic<int> current_card_{-1};
    std::atomic<bool> current_usb_48k_{false};
    std::atomic<bool> current_hp_{false};
    std::atomic<bool> force_internal_{false};
    std::atomic<uint32_t> active_sample_rate_{32000};
    std::atomic<uint32_t> target_period_frames_{1024};
    std::atomic<uint32_t> active_period_frames_{1024};
    std::atomic<uint32_t> xrun_count_{0};

    std::atomic<float> vu_l_{0.0f};
    std::atomic<float> vu_r_{0.0f};
    std::atomic<float> peak_hold_{0.0f};
    std::array<std::atomic<float>, 4> track_vu_{};

    std::array<std::atomic<float>, 256> scope_ring_{};
    std::atomic<uint32_t> scope_write_idx_{0};

    SamplerEngine local_sampler_;
    SamplerEngine* sampler_ptr_ = nullptr;
    Dx7Engine dx7_engine_;

#if defined(__linux__)
    std::thread alsa_thread_;
#else
    uint32_t sdl_audio_dev_ = 0;
#endif
};

} // namespace groovebox
