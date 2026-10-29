#pragma once

#include "dx7_engine.hpp"
#include "net_audio_streamer.hpp"
#include "sampler_engine.hpp"
#include "synth_engines.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
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
    std::atomic<float> delay_width{1.0f};     // -1.0 .. +1.0 (0 = mono, >0 = ping-pong L->R, <0 = ping-pong R->L)
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
    int active_tracks = 3;
    std::array<float, kMaxTracks> track_levels{};
    std::array<std::string, kMaxTracks> track_names{};
    std::array<float, 256> scope_samples{};
    float send_delay = 0.0f;
    float send_reverb = 0.0f;
    float send_chorus = 0.0f;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool start(int card_index, bool is_usb_48k, bool headphones_plugged);
    void stop();

    void updateRouting(int card_index, bool is_usb_48k, bool headphones_plugged);
    void adjustMasterVolume(float delta);
    float getMasterVolume() const { return params_.master_volume.load(std::memory_order_relaxed); }
    void setBpm(float bpm) { bpm_.store(bpm, std::memory_order_relaxed); }
    float getBpm() const { return bpm_.load(std::memory_order_relaxed); }
    MasterFxParameters getMasterFx() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return master_fx_;
    }
    void setMasterFx(const MasterFxParameters& mfx) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        master_fx_ = mfx;
    }

    // Запис майстер-виходу у WAV (Live Master WAV Recording)
    bool startRecording(const std::string& custom_path = "");
    void stopRecording();
    bool isRecording() const { return is_recording_.load(std::memory_order_relaxed); }
    uint32_t getRecordedSeconds() const;
    std::string getRecordingPath() const;

    // Оверрайд аудіовиходу: Auto (USB за наявності) <-> Force Internal (RK817)
    void toggleOutputOverride();
    bool isOutputForcedInternal() const;

    // Підтримувані частоти дискретизації та розміри періоду/буфера
    static constexpr std::array<uint32_t, 3> kSampleRates = {24000, 32000, 44100};
    static constexpr std::array<uint32_t, 6> kBufferSizes = {96, 128, 256, 512, 1024, 2048};

    // Перемикання частоти дискретизації списком (dir: -1 / +1)
    void stepSampleRate(int dir);
    uint32_t getUserSampleRate() const { return user_sample_rate_.load(std::memory_order_relaxed); }
    void setUserSampleRate(uint32_t sr);

    // Перемикання розміру буфера/періоду списком (dir: -1 / +1)
    void stepBufferSize(int dir);
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
    void setStepChordNote(int row, int step_idx, int slot, uint8_t note);
    uint8_t getStepChordNote(int row, int step_idx, int slot) const;
    void lockStepCutoff(int row, int step_idx, float cutoff_val);

    // Кількість активних треків секвенсора (від 1 до 16)
    int getTrackCount() const { return active_rows_count_; }
    int getActiveRowCount() const { return active_rows_count_; }
    void setActiveRowCount(int count) { active_rows_count_ = std::clamp(count, 1, kMaxTracks); }

    int addTrack(EngineType engine, const std::string& name = "");
    bool deleteTrack(int track_idx);
    void setTrackName(int track, const std::string& name);
    std::string getTrackName(int track) const;

    // Повне скидання звуку (Panic / Kill all voices + clear delay)
    void panic();

    // Соло треку (ексклюзивне соло з відновленням попередньої маски м'ютів)
    void toggleSoloTrack(int track);
    bool isSoloed(int track) const { return soloed_track_ == track; }

    // Поточна позиція відтворення семпла для UI-візуалізатора хвилі (0.0 .. 1.0)
    float trackPlayhead(int track) const {
        if (track >= 0 && track < kMaxTracks) {
            return track_playhead_[track].load(std::memory_order_relaxed);
        }
        return 0.0f;
    }

    // Тригер ноти для конкретного голосу/рядка (row = 0..15)
    void triggerVoice(int row, int midi_note, float velocity = 0.9f, float plock_cutoff = -1.0f, bool slide = false, float gate_len = 0.60f, const SequencerStep* plock_step = nullptr);
    // Поліфонічний тригер ноти для треку (динамічний розподіл на 4 голоси треку)
    void triggerNote(int track, int midi_note, float velocity = 0.9f, float plock_cutoff = -1.0f, bool slide = false, float gate_len = 0.60f, const SequencerStep* plock_step = nullptr);
    // Поліфонічне відпускання ноти (Note-Off) для треку
    void noteOff(int track, int midi_note);
    void noteOffAll();

    using MidiOutCallback = std::function<void(int channel, int note, float velocity, bool note_on)>;
    void setMidiOutCallback(MidiOutCallback cb) { midi_out_cb_ = std::move(cb); }

    // Тригер кроку секвенсора (master_tick 0..unlimited)
    void triggerSequencerStep(uint32_t master_tick);
    uint32_t getPatternLength() const { return pattern_length_.load(std::memory_order_relaxed); }
    void setPatternLength(uint32_t len) { pattern_length_.store(std::clamp(len, 16u, kMaxSteps)); }

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
    uint32_t sampleRate() const { return active_sample_rate_.load(std::memory_order_relaxed); }

    // Отримання поточного значення модуляції для таргету треку (для UI повзунків та анімації)
    float getLiveModOffset(int track, ModTarget target) const;
    float getLiveModValue(int track, ModTarget target, float base_val) const;

    // Публічний метод рендерингу аудіоблоку (для тестів, експорту та офлайн-обробки)
    void renderBlock(int16_t* out_interleaved, uint32_t frames, uint32_t sample_rate);

private:
    void initDefaultPattern();
    void applyRk817HardwareMux(bool headphones_plugged);
    void applyRk817HardwareVolume(float vol_norm);

#if defined(__linux__)
    void alsaThreadLoop();
#endif

    SynthParams params_;
    mutable std::mutex state_mutex_;
    std::array<TrackParameters, kMaxTracks> tracks_{};
    std::atomic<uint32_t> pattern_length_{64};
    std::array<std::atomic<uint32_t>, kMaxTracks> row_lengths_{};
    std::array<uint8_t, kMaxTracks> row_track_{};
    std::array<uint8_t, kMaxTracks> row_voice_{};
    std::array<std::array<SequencerStep, kMaxSteps>, kMaxTracks> pattern_{};
    int active_rows_count_ = 3;
    std::atomic<bool> fill_mode_{false};
    uint32_t cycle_counter_ = 0;
    std::array<bool, kMaxTracks> prev_step_fired_{};
    std::array<std::atomic<float>, kMaxTracks> track_vu_{};
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
        bool loop_forward = true;
        AdsrEnvelope amp_adsr{};
        AdsrEnvelope filter_adsr{};
        AdsrEnvelope mod_env1{};
        AdsrEnvelope mod_env2{};
        float pitch_env = 0.0f;
        float velocity = 1.0f;
        float lpf_s1 = 0.0f;
        float lpf_s2 = 0.0f;
        float lpf_s1_b = 0.0f;
        float lpf_s2_b = 0.0f;
        float hpf_s1 = 0.0f;
        float hpf_s2 = 0.0f;
        float hpf_s1_b = 0.0f;
        float hpf_s2_b = 0.0f;
        float lpf_env_val = 0.0f;
        float lpf_decay_coef = 0.999f;
        float hpf_env_val = 0.0f;
        float hpf_decay_coef = 0.999f;
        float last_out = 0.0f;
        float plock_cutoff = -1.0f;
        std::array<float, 6> plock_p = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
        float plock_sample_start = -1.0f;
        float plock_sample_end = -1.0f;
        float plock_sample_loop = -1.0f;
        float plock_delay_send = -1.0f;
        float plock_reverb_send = -1.0f;
        uint8_t drum_slot = 0;
        Dx7Voice dx7_voice{};
    };

    std::array<VoiceState, kTotalVoices> voices_{};
    uint32_t rng_state_ = 0x12345678u;

    std::array<float, kMaxTracks> track_lfo1_phase_{};
    std::array<float, kMaxTracks> track_lfo2_phase_{};
    std::array<float, kMaxTracks> track_lfo3_phase_{};
    std::array<float, kMaxTracks> track_lfo4_phase_{};
    mutable std::array<std::array<float, static_cast<size_t>(ModTarget::kCount)>, kMaxTracks> live_mod_offsets_{};

    // Стерео пінг-понг ділей буфер (до 0.5 сек на 48k)
    std::vector<float> delay_buf_l_;
    std::vector<float> delay_buf_r_;
    uint32_t delay_idx_ = 0;

    // Справжня алгоритмічна стерео реверберація Freeverb (8 Comb + 4 Allpass)
    struct CombFilter {
        std::vector<float> buffer;
        size_t buf_idx = 0;
        float filter_store = 0.0f;

        void init(size_t size) {
            buffer.assign(size, 0.0f);
            buf_idx = 0;
            filter_store = 0.0f;
        }

        inline float process(float input, float feedback, float damp) {
            if (buffer.empty()) return 0.0f;
            float output = buffer[buf_idx];
            filter_store = (output * (1.0f - damp)) + (filter_store * damp);
            buffer[buf_idx] = input + (filter_store * feedback);
            buf_idx = (buf_idx + 1 >= buffer.size()) ? 0 : (buf_idx + 1);
            return output;
        }

        void clear() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            buf_idx = 0;
            filter_store = 0.0f;
        }
    };

    struct AllpassFilter {
        std::vector<float> buffer;
        size_t buf_idx = 0;

        void init(size_t size) {
            buffer.assign(size, 0.0f);
            buf_idx = 0;
        }

        inline float process(float input) {
            if (buffer.empty()) return input;
            float bufout = buffer[buf_idx];
            float output = -input + bufout;
            buffer[buf_idx] = input + (bufout * 0.5f);
            buf_idx = (buf_idx + 1 >= buffer.size()) ? 0 : (buf_idx + 1);
            return output;
        }

        void clear() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            buf_idx = 0;
        }
    };

    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpasses = 4;
    std::array<CombFilter, kNumCombs> comb_l_{};
    std::array<CombFilter, kNumCombs> comb_r_{};
    std::array<AllpassFilter, kNumAllpasses> allpass_l_{};
    std::array<AllpassFilter, kNumAllpasses> allpass_r_{};
    uint32_t reverb_sample_rate_ = 0;
    void initReverb(uint32_t sample_rate);

    // Стерео хорус та сатуратор
    std::vector<float> chorus_buf_l_;
    std::vector<float> chorus_buf_r_;
    uint32_t chorus_idx_ = 0;
    float chorus_phase_ = 0.0f;
    float drive_lp_l_ = 0.0f;
    float drive_lp_r_ = 0.0f;

    // Стан індивідуального ланцюжка ефектів для кожного треку (Per-Track FX Chain)
    struct TrackFxState {
        int srr_step = 0;
        float srr_l = 0.0f;
        float srr_r = 0.0f;

        float dist_hp_l = 0.0f;
        float dist_hp_r = 0.0f;
        float dist_bp_l = 0.0f;
        float dist_bp_r = 0.0f;
        float dist_lp_l = 0.0f;
        float dist_lp_r = 0.0f;

        std::vector<float> chorus_l;
        std::vector<float> chorus_r;
        size_t chorus_idx = 0;
        float chorus_phase = 0.0f;

        std::vector<float> delay_l;
        std::vector<float> delay_r;
        size_t delay_idx = 0;

        std::vector<float> rev_pre_l;
        std::vector<float> rev_pre_r;
        size_t rev_pre_idx = 0;
        std::array<CombFilter, 4> comb_l;
        std::array<CombFilter, 4> comb_r;
        std::array<AllpassFilter, 2> allpass_l;
        std::array<AllpassFilter, 2> allpass_r;
        float rev_lp_l = 0.0f;
        float rev_lp_r = 0.0f;
        float rev_hp_l = 0.0f;
        float rev_hp_r = 0.0f;

        float sc_env = 0.0f;
        float sc_gain = 1.0f;
    };
    std::array<TrackFxState, kMaxTracks> track_fx_{};
    std::array<float, kMaxTracks> last_track_sample_{};
    void initTrackFx(uint32_t sample_rate);

    // Стан шини мастерингу
    MasterFxParameters master_fx_{};
    float mst_sc_env_ = 0.0f;
    float mst_sc_gain_ = 1.0f;
    float mst_hpf1_low_l_ = 0.0f, mst_hpf1_band_l_ = 0.0f;
    float mst_hpf1_low_r_ = 0.0f, mst_hpf1_band_r_ = 0.0f;
    float mst_hpf2_low_l_ = 0.0f, mst_hpf2_band_l_ = 0.0f;
    float mst_hpf2_low_r_ = 0.0f, mst_hpf2_band_r_ = 0.0f;
    float mst_bell_x1_l_ = 0.0f, mst_bell_x2_l_ = 0.0f;
    float mst_bell_x1_r_ = 0.0f, mst_bell_x2_r_ = 0.0f;
    float mst_bell2_x1_l_ = 0.0f, mst_bell2_x2_l_ = 0.0f;
    float mst_bell2_x1_r_ = 0.0f, mst_bell2_x2_r_ = 0.0f;
    float mst_bell3_x1_l_ = 0.0f, mst_bell3_x2_l_ = 0.0f;
    float mst_bell3_x1_r_ = 0.0f, mst_bell3_x2_r_ = 0.0f;
    float mst_lpf1_low_l_ = 0.0f, mst_lpf1_band_l_ = 0.0f;
    float mst_lpf1_low_r_ = 0.0f, mst_lpf1_band_r_ = 0.0f;
    float mst_lpf2_low_l_ = 0.0f, mst_lpf2_band_l_ = 0.0f;
    float mst_lpf2_low_r_ = 0.0f, mst_lpf2_band_r_ = 0.0f;

    NetAudioStreamer net_streamer_;

    // Підсистема потокового запису майстер-сигналу у WAV на диск
    std::atomic<bool> is_recording_{false};
    std::atomic<uint32_t> rec_frames_total_{0};
    std::string rec_filepath_;
    std::thread rec_thread_;
    std::mutex rec_mutex_;
    std::condition_variable rec_cv_;
    static constexpr size_t kRecRingCapacity = 65536;
    std::vector<int16_t> rec_ring_buf_;
    std::atomic<size_t> rec_write_pos_{0};
    std::atomic<size_t> rec_read_pos_{0};
    void recThreadLoop();
    void pushRecordingSamples(const int16_t* interleaved_pcm, uint32_t frames);

    std::atomic<bool> running_{false};
    std::atomic<int> current_card_{-1};
    std::atomic<bool> current_usb_48k_{false};
    std::atomic<bool> current_hp_{false};
    std::atomic<bool> force_internal_{false};
    std::atomic<uint32_t> user_sample_rate_{0};
    std::atomic<uint32_t> active_sample_rate_{32000};
    std::atomic<uint32_t> target_period_frames_{1024};
    std::atomic<uint32_t> active_period_frames_{1024};
    std::atomic<uint32_t> xrun_count_{0};

    std::atomic<float> vu_l_{0.0f};
    std::atomic<float> vu_r_{0.0f};
    std::atomic<float> peak_hold_{0.0f};
    std::atomic<float> bpm_{120.0f};
    std::atomic<float> send_delay_peak_{0.0f};
    std::atomic<float> send_reverb_peak_{0.0f};
    std::atomic<float> send_chorus_peak_{0.0f};

    std::array<std::atomic<float>, 256> scope_ring_{};
    std::atomic<uint32_t> scope_write_idx_{0};

    std::array<std::atomic<float>, kMaxTracks> track_playhead_{};
    std::array<bool, kMaxTracks> prev_mute_mask_{};
    int soloed_track_ = -1;

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
