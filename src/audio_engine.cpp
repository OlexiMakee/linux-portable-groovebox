#include "audio_engine.hpp"
#include "device_profile.hpp"

#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
extern "C" {
struct _snd_pcm;
typedef struct _snd_pcm snd_pcm_t;
typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;

typedef enum _snd_pcm_stream {
    SND_PCM_STREAM_PLAYBACK = 0,
} snd_pcm_stream_t;

typedef enum _snd_pcm_format {
    SND_PCM_FORMAT_S16_LE = 2,
    SND_PCM_FORMAT_S24_LE = 6,
    SND_PCM_FORMAT_S32_LE = 10,
    SND_PCM_FORMAT_S24_3LE = 32,
} snd_pcm_format_t;

typedef enum _snd_pcm_access {
    SND_PCM_ACCESS_RW_INTERLEAVED = 3,
} snd_pcm_access_t;

int snd_pcm_open(snd_pcm_t** pcm, const char* name, snd_pcm_stream_t stream, int mode);
int snd_pcm_close(snd_pcm_t* pcm);
int snd_pcm_set_params(
    snd_pcm_t* pcm,
    snd_pcm_format_t format,
    snd_pcm_access_t access,
    unsigned int channels,
    unsigned int rate,
    int soft_resample,
    unsigned int latency_us
);
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t* pcm, const void* buffer, snd_pcm_uframes_t size);
int snd_pcm_recover(snd_pcm_t* pcm, int err, int silent);
const char* snd_strerror(int errnum);
}
#endif

namespace groovebox {

namespace {

inline float midiToFreq(int note) {
    return 440.0f * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
}

inline float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

} // namespace

AudioEngine::AudioEngine()
    : delay_buf_l_(16384, 0.0f),
      delay_buf_r_(16384, 0.0f) {
    for (auto& s : scope_ring_) s.store(0.0f, std::memory_order_relaxed);
    for (auto& t : track_vu_) t.store(0.0f, std::memory_order_relaxed);
    initDefaultPattern();
}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::initDefaultPattern() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    // Початкова розкладка 4 доріжок (кожну можна змінити в меню L2:SYNTH!)
    tracks_[0] = {EngineType::kDrumSyn,  {0.32f, 0.65f, 0.42f, 0.15f, 0.45f, 0.50f}, 0.92f,  0.00f, 0.05f, false};
    tracks_[1] = {EngineType::kVaMono,   {0.48f, 0.35f, 0.55f, 0.38f, 0.60f, 0.50f}, 0.85f, -0.15f, 0.18f, false};
    tracks_[2] = {EngineType::kAcid303,  {0.42f, 0.68f, 0.72f, 0.28f, 0.65f, 0.00f}, 0.80f,  0.20f, 0.32f, false};
    tracks_[3] = {EngineType::kFm2Op,    {0.65f, 0.45f, 0.50f, 0.30f, 0.25f, 0.45f}, 0.75f, -0.25f, 0.40f, false};

    // 16-кроковий стартовий грув з Parameter Locks
    static constexpr int kKick[16] = {36, 0, 0, 0,  36, 0, 0, 0,  36, 0, 0, 36,  36, 0, 48, 0};
    static constexpr int kBass[16] = {0,  0, 36, 36, 0, 39, 36, 0,  0, 36, 48, 36, 0, 41, 43, 46};
    static constexpr int kAcid[16] = {48, 0, 60, 48, 0, 55, 58, 0, 48, 60, 0, 55, 51, 0, 58, 60};
    static constexpr int kFm[16]   = {0,  0, 0,  0, 67, 0,  0, 72,  0,  0, 0,  0, 67, 0, 70,  0};

    for (int s = 0; s < 16; ++s) {
        pattern_[0][s] = {kKick[s] > 0, static_cast<uint8_t>(kKick[s] > 0 ? kKick[s] : 36), 0.95f, false, 0.5f};
        pattern_[1][s] = {kBass[s] > 0, static_cast<uint8_t>(kBass[s] > 0 ? kBass[s] : 36), 0.85f, false, 0.5f};
        pattern_[2][s] = {
            kAcid[s] > 0,
            static_cast<uint8_t>(kAcid[s] > 0 ? kAcid[s] : 48),
            (s % 3 == 0) ? 1.0f : 0.75f,
            (s == 2 || s == 9 || s == 15),
            0.82f
        };
        pattern_[3][s] = {kFm[s] > 0, static_cast<uint8_t>(kFm[s] > 0 ? kFm[s] : 67), 0.85f, false, 0.5f};
    }
}

TrackParameters AudioEngine::getTrackParams(int track) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return tracks_[std::clamp(track, 0, 3)];
}

void AudioEngine::setTrackParams(int track, const TrackParameters& tp) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    tracks_[std::clamp(track, 0, 3)] = tp;
}

void AudioEngine::cycleTrackEngine(int track, int dir) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& tr = tracks_[std::clamp(track, 0, 3)];
    int count = static_cast<int>(EngineType::kCount);
    int next = (static_cast<int>(tr.engine) + dir + count) % count;
    tr.engine = static_cast<EngineType>(next);
}

void AudioEngine::adjustTrackParam(int track, int param_idx, float delta) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& tr = tracks_[std::clamp(track, 0, 3)];
    if (param_idx >= 0 && param_idx < kParamsPerTrack) {
        tr.p[param_idx] = std::clamp(tr.p[param_idx] + delta, 0.0f, 1.0f);
    }
}

SequencerStep AudioEngine::getStep(int track, int step_idx) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return pattern_[std::clamp(track, 0, 3)][step_idx & 15];
}

void AudioEngine::toggleStep(int track, int step_idx) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& st = pattern_[std::clamp(track, 0, 3)][step_idx & 15];
    st.enabled = !st.enabled;
}

void AudioEngine::adjustStepNote(int track, int step_idx, int semitones) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& st = pattern_[std::clamp(track, 0, 3)][step_idx & 15];
    int n = std::clamp(static_cast<int>(st.note) + semitones, 24, 96);
    st.note = static_cast<uint8_t>(n);
    st.enabled = true;
}

void AudioEngine::lockStepCutoff(int track, int step_idx, float cutoff_val) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& st = pattern_[std::clamp(track, 0, 3)][step_idx & 15];
    st.has_plock = true;
    st.plock_cutoff = std::clamp(cutoff_val, 0.05f, 0.95f);
}

void AudioEngine::applyRk817HardwareMux(bool headphones_plugged) {
    current_hp_.store(headphones_plugged);
#if defined(__linux__)
    const auto& prof = activeProfile();
    if (!prof.mux_control.empty()) {
        char cmd[256];
        std::snprintf(
            cmd, sizeof(cmd),
            "amixer -c %d cset name='%s' '%s' >/dev/null 2>&1",
            prof.internal_card,
            prof.mux_control.c_str(),
            headphones_plugged ? prof.mux_hp.c_str() : prof.mux_spk.c_str()
        );
        int rc = std::system(cmd);
        (void)rc;
    }
#endif
}

void AudioEngine::applyRk817HardwareVolume(float vol_norm) {
#if defined(__linux__)
    const auto& prof = activeProfile();
    if (!prof.volume_control.empty()) {
        int v_range = prof.volume_max - prof.volume_min;
        int hw_val = (vol_norm <= 0.01f) ? prof.volume_min
                     : static_cast<int>(prof.volume_min + (v_range * 0.72f) + vol_norm * (v_range * 0.28f));
        hw_val = std::clamp(hw_val, prof.volume_min, prof.volume_max);
        char cmd[256];
        std::snprintf(
            cmd, sizeof(cmd),
            "amixer -c %d cset name='%s' %d,%d >/dev/null 2>&1 &",
            prof.internal_card,
            prof.volume_control.c_str(),
            hw_val, hw_val
        );
        int rc = std::system(cmd);
        (void)rc;
    }
#else
    (void)vol_norm;
#endif
}

bool AudioEngine::start(int card_index, bool is_usb_48k, bool headphones_plugged) {
    stop();

    current_card_.store(card_index);
    current_usb_48k_.store(is_usb_48k);
    const auto& prof = activeProfile();
    const uint32_t target_rate = is_usb_48k ? static_cast<uint32_t>(prof.usb_rate) : static_cast<uint32_t>(prof.internal_rate);
    active_sample_rate_.store(target_rate);
    active_period_frames_.store(256);

    applyRk817HardwareMux(headphones_plugged);
    applyRk817HardwareVolume(params_.master_volume.load());

    running_.store(true);

#if defined(__linux__)
    alsa_thread_ = std::thread(&AudioEngine::alsaThreadLoop, this);
    return true;
#else
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_AudioSpec want{}, have{};
    want.freq = static_cast<int>(target_rate);
    want.format = AUDIO_S16LSB;
    want.channels = 2;
    want.samples = 256;
    want.userdata = this;
    want.callback = [](void* userdata, Uint8* stream, int len) {
        auto* self = static_cast<AudioEngine*>(userdata);
        uint32_t frames = static_cast<uint32_t>(len) / (2 * sizeof(int16_t));
        self->renderBlock(reinterpret_cast<int16_t*>(stream), frames, self->active_sample_rate_.load());
    };
    sdl_audio_dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (sdl_audio_dev_ != 0) {
        active_sample_rate_.store(have.freq);
        SDL_PauseAudioDevice(sdl_audio_dev_, 0);
    }
    return true;
#endif
}

void AudioEngine::stop() {
    if (!running_.exchange(false)) return;
#if defined(__linux__)
    if (alsa_thread_.joinable()) {
        alsa_thread_.join();
    }
#else
    if (sdl_audio_dev_ != 0) {
        SDL_CloseAudioDevice(sdl_audio_dev_);
        sdl_audio_dev_ = 0;
    }
#endif
}

void AudioEngine::updateRouting(int card_index, bool is_usb_48k, bool headphones_plugged) {
    if (card_index != current_card_.load() || is_usb_48k != current_usb_48k_.load()) {
        start(card_index, is_usb_48k, headphones_plugged);
        return;
    }
    if (headphones_plugged != current_hp_.load()) {
        applyRk817HardwareMux(headphones_plugged);
    }
}

void AudioEngine::adjustMasterVolume(float delta) {
    float cur = params_.master_volume.load();
    float next = std::clamp(cur + delta, 0.0f, 1.0f);
    params_.master_volume.store(next);
    if (current_card_.load() == 0) {
        applyRk817HardwareVolume(next);
    }
}

void AudioEngine::triggerNote(int track, int midi_note, float velocity, float plock_cutoff) {
    if (track < 0 || track >= 4) return;
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (tracks_[track].muted) return;

    auto& v = voices_[track];
    v.freq = midiToFreq(midi_note);
    v.env = std::clamp(velocity, 0.1f, 1.0f);
    v.filter_env = 1.0f;
    v.pitch_env = 1.0f;
    v.plock_cutoff = plock_cutoff;
    v.active = true;

    const float sr = static_cast<float>(active_sample_rate_.load());
    const float dec_param = std::clamp(tracks_[track].p[3], 0.05f, 0.98f);
    const float dec_sec = 0.05f + dec_param * 0.85f;
    v.decay_rate = std::exp(-1.0f / (sr * dec_sec));
}

void AudioEngine::triggerSequencerStep(uint32_t step_idx) {
    const uint32_t st = step_idx & 15u;
    std::array<SequencerStep, 4> steps;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (int t = 0; t < 4; ++t) {
            steps[t] = pattern_[t][st];
        }
    }
    for (int t = 0; t < 4; ++t) {
        if (steps[t].enabled) {
            triggerNote(
                t,
                steps[t].note,
                steps[t].velocity,
                steps[t].has_plock ? steps[t].plock_cutoff : -1.0f
            );
        }
    }
}

void AudioEngine::renderBlock(int16_t* out_interleaved, uint32_t frames, uint32_t sample_rate) {
    const float inv_sr = 1.0f / static_cast<float>(sample_rate);
    const float master_vol = params_.master_volume.load(std::memory_order_relaxed);
    const float cut_macro = params_.cutoff_macro.load(std::memory_order_relaxed);
    const float res_macro = params_.resonance_macro.load(std::memory_order_relaxed);
    const float env_macro = params_.env_mod_macro.load(std::memory_order_relaxed);
    const float fm_macro  = params_.fm_macro.load(std::memory_order_relaxed);
    const float dly_fb    = params_.delay_feedback.load(std::memory_order_relaxed);
    const float dly_mix   = params_.delay_mix.load(std::memory_order_relaxed);

    std::array<TrackParameters, 4> local_tracks;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        local_tracks = tracks_;
    }

    float max_l = 0.0f;
    float max_r = 0.0f;
    std::array<float, 4> trk_peak{0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t sc_idx = scope_write_idx_.load(std::memory_order_relaxed);
    const uint32_t delay_len = (sample_rate == 48000u) ? 12000u : 8000u; // ~250ms tempo delay

    for (uint32_t i = 0; i < frames; ++i) {
        float mix_l = 0.0f;
        float mix_r = 0.0f;
        float send_l = 0.0f;
        float send_r = 0.0f;

        for (int t = 0; t < 4; ++t) {
            auto& v = voices_[t];
            const auto& tp = local_tracks[t];
            if (tp.muted || !v.active || v.env < 0.0005f) {
                v.active = false;
                continue;
            }

            float cur_freq = v.freq;
            if (tp.engine == EngineType::kDrumSyn) {
                // Pitch envelope для пружного аналогового Kick / Tom / Zap
                cur_freq = v.freq * (0.65f + tp.p[0] * 1.1f + v.pitch_env * tp.p[1] * 4.2f);
                v.pitch_env *= 0.994f;
            }

            const float dt = std::clamp(cur_freq * inv_sr, 0.0001f, 0.45f);
            v.phase1 += dt;
            if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;

            float raw = 0.0f;
            switch (tp.engine) {
                case EngineType::kVaMono: {
                    v.phase2 += dt * 0.5f;
                    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                    float pw = std::clamp(0.15f + tp.p[5] * 0.70f, 0.1f, 0.9f);
                    float saw = (2.0f * v.phase1 - 1.0f) - polyBlep(v.phase1, dt);
                    float pulse = (v.phase1 < pw ? 1.0f : -1.0f)
                                  + polyBlep(v.phase1, dt)
                                  - polyBlep(std::fmod(v.phase1 - pw + 1.0f, 1.0f), dt);
                    float sub = (v.phase2 < 0.5f) ? 0.65f : -0.65f;
                    raw = (saw * 0.50f + pulse * 0.35f) + sub * tp.p[4];
                    break;
                }
                case EngineType::kFm2Op: {
                    static constexpr float kRatios[8] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 3.5f, 4.0f, 7.0f};
                    int r_idx = std::clamp(static_cast<int>(tp.p[2] * 7.99f), 0, 7);
                    v.phase2 += dt * kRatios[r_idx];
                    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                    float mod_idx = std::clamp(tp.p[1] + fm_macro, 0.0f, 1.0f) * 6.5f * v.filter_env;
                    float fb = tp.p[4] * v.last_out * 1.2f;
                    float mod = std::sin((v.phase2 + fb) * 6.2831853f) * mod_idx;
                    raw = std::sin(v.phase1 * 6.2831853f + mod);
                    v.last_out = raw;
                    break;
                }
                case EngineType::kAcid303: {
                    float saw = (2.0f * v.phase1 - 1.0f) - polyBlep(v.phase1, dt);
                    float sq = (v.phase1 < 0.5f ? 0.9f : -0.9f)
                               + polyBlep(v.phase1, dt)
                               - polyBlep(std::fmod(v.phase1 + 0.5f, 1.0f), dt);
                    raw = saw * (1.0f - tp.p[5]) + sq * tp.p[5];
                    break;
                }
                case EngineType::kChordPad: {
                    // 4 інтервали акордів: Minor7, Major7, Sus4, Fifth+Oct
                    static constexpr float kChords[4][2] = {
                        {1.189207f, 1.498307f}, // m7 (+3, +7)
                        {1.259921f, 1.498307f}, // Maj7 (+4, +7)
                        {1.334840f, 1.498307f}, // Sus4 (+5, +7)
                        {1.498307f, 2.000000f}  // Power (+7, +12)
                    };
                    int c_idx = std::clamp(static_cast<int>(tp.p[2] * 3.99f), 0, 3);
                    float det = 1.0f + (tp.p[4] + std::fabs(fm_macro)) * 0.012f;
                    v.phase2 += dt * kChords[c_idx][0] * det;
                    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                    v.phase3 += dt * kChords[c_idx][1];
                    if (v.phase3 >= 1.0f) v.phase3 -= 1.0f;
                    float s1 = (2.0f * v.phase1 - 1.0f) - polyBlep(v.phase1, dt);
                    float s2 = (2.0f * v.phase2 - 1.0f) - polyBlep(v.phase2, dt);
                    float s3 = (2.0f * v.phase3 - 1.0f) - polyBlep(v.phase3, dt);
                    raw = (s1 + s2 + s3) * 0.38f;
                    break;
                }
                case EngineType::kDrumSyn:
                default: {
                    // Синусоїдальне тіло барабана + Xorshift шум для Snare/Hat
                    rng_state_ ^= rng_state_ << 13;
                    rng_state_ ^= rng_state_ >> 17;
                    rng_state_ ^= rng_state_ << 5;
                    float noise = static_cast<float>(static_cast<int32_t>(rng_state_)) * (1.0f / 2147483648.0f);
                    float body = std::sin(v.phase1 * 6.2831853f);
                    raw = body * (1.0f - tp.p[3] * 0.75f) + noise * tp.p[3] * v.filter_env;
                    break;
                }
            }

            // Резонансний SVF-фільтр з урахуванням P-Lock кроку та стіків
            float base_cut = (v.plock_cutoff >= 0.0f) ? v.plock_cutoff : tp.p[0];
            float f_cut = std::clamp(
                base_cut + cut_macro + v.filter_env * std::clamp(tp.p[2] + env_macro, 0.0f, 1.0f) * 0.58f,
                0.04f,
                0.88f
            );
            float reso = std::clamp(tp.p[1] + res_macro, 0.0f, 0.90f);
            float q_damp = std::clamp(1.0f - reso * 0.88f, 0.10f, 1.0f);

            v.svf_low += f_cut * v.svf_band;
            float high = raw - v.svf_low - q_damp * v.svf_band;
            v.svf_band += f_cut * high;
            v.svf_band = std::clamp(v.svf_band, -1.85f, 1.85f);

            float out_s = ((tp.engine == EngineType::kDrumSyn) ? raw : v.svf_low) * v.env * tp.level;
            v.env *= v.decay_rate;
            v.filter_env *= (v.decay_rate * 0.9982f);

            float abs_s = std::fabs(out_s);
            if (abs_s > trk_peak[t]) trk_peak[t] = abs_s;

            float pan_l = std::clamp(1.0f - tp.pan, 0.0f, 1.0f) * 0.5f;
            float pan_r = std::clamp(1.0f + tp.pan, 0.0f, 1.0f) * 0.5f;
            mix_l += out_s * pan_l;
            mix_r += out_s * pan_r;
            send_l += out_s * tp.delay_send * pan_l;
            send_r += out_s * tp.delay_send * pan_r;
        }

        // Майстер стерео пінг-понг ділей
        uint32_t read_idx = (delay_idx_ + delay_buf_l_.size() - delay_len) % delay_buf_l_.size();
        float d_l = delay_buf_l_[read_idx];
        float d_r = delay_buf_r_[read_idx];
        delay_buf_l_[delay_idx_] = std::clamp(send_l + d_r * dly_fb, -0.95f, 0.95f);
        delay_buf_r_[delay_idx_] = std::clamp(send_r + d_l * dly_fb, -0.95f, 0.95f);
        delay_idx_ = (delay_idx_ + 1) % delay_buf_l_.size();

        mix_l = std::clamp((mix_l + d_l * dly_mix) * master_vol, -0.98f, 0.98f);
        mix_r = std::clamp((mix_r + d_r * dly_mix) * master_vol, -0.98f, 0.98f);

        max_l = std::max(max_l, std::fabs(mix_l));
        max_r = std::max(max_r, std::fabs(mix_r));

        if ((i & 1u) == 0u) {
            scope_ring_[sc_idx & 255u].store((mix_l + mix_r) * 0.5f, std::memory_order_relaxed);
            ++sc_idx;
        }

        out_interleaved[i * 2]     = static_cast<int16_t>(mix_l * 32000.0f);
        out_interleaved[i * 2 + 1] = static_cast<int16_t>(mix_r * 32000.0f);
    }

    scope_write_idx_.store(sc_idx & 255u, std::memory_order_relaxed);

    float prev_l = vu_l_.load(std::memory_order_relaxed);
    float prev_r = vu_r_.load(std::memory_order_relaxed);
    float next_l = std::max(max_l, prev_l * 0.88f);
    float next_r = std::max(max_r, prev_r * 0.88f);
    vu_l_.store(next_l, std::memory_order_relaxed);
    vu_r_.store(next_r, std::memory_order_relaxed);
    peak_hold_.store(std::max({next_l, next_r, peak_hold_.load(std::memory_order_relaxed) * 0.96f}), std::memory_order_relaxed);

    for (int t = 0; t < 4; ++t) {
        float prev_t = track_vu_[t].load(std::memory_order_relaxed);
        track_vu_[t].store(std::max(trk_peak[t], prev_t * 0.86f), std::memory_order_relaxed);
    }
}

#if defined(__linux__)
void AudioEngine::alsaThreadLoop() {
    // Піднімаємо пріоритет аудіопотоку до Real-Time (SCHED_FIFO 75),
    // щоб ізохронні USB-пакети dwc2 ніколи не переривалися графікою чи фоновими процесами!
    {
        struct sched_param sp{};
        sp.sched_priority = 75;
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    }

    snd_pcm_t* pcm = nullptr;
    snd_pcm_format_t active_fmt = SND_PCM_FORMAT_S16_LE;

    int card = current_card_.load();
    const uint32_t rate = active_sample_rate_.load();
    // 24 мс буфер (4.5 періоди по 256 семплів) для стабільного USB Isochronous потоку через контролер dwc2 без XRUN
    const unsigned int latency_us = current_usb_48k_.load() ? 24000u : 16000u;

    // 1. Якщо підключено зовнішній USB Audio інтерфейс (Digitakt II або інший Class-Compliant card > 0),
    //    відкриваємо ВИКЛЮЧНО його (щоб не було конфлікту двох різних кварцових генераторів USB vs I2S RK817!)
    if (card > 0) {
        char usb_dev[32];
        std::snprintf(usb_dev, sizeof(usb_dev), "hw:%d,0", card);
        int err = snd_pcm_open(&pcm, usb_dev, SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0) {
            std::fprintf(stderr, "[ALSA] Failed to open USB %s: %s\n", usb_dev, snd_strerror(err));
            pcm = nullptr;
        } else {
            static constexpr snd_pcm_format_t kTryFormats[] = {
                SND_PCM_FORMAT_S32_LE,
                SND_PCM_FORMAT_S24_3LE,
                SND_PCM_FORMAT_S24_LE,
                SND_PCM_FORMAT_S16_LE
            };
            bool ok = false;
            for (snd_pcm_format_t fmt : kTryFormats) {
                if (snd_pcm_set_params(
                        pcm,
                        fmt,
                        SND_PCM_ACCESS_RW_INTERLEAVED,
                        2,
                        rate,
                        1,
                        latency_us
                    ) >= 0) {
                    active_fmt = fmt;
                    ok = true;
                    std::printf("[ALSA] Opened EXCLUSIVE USB %s at %u Hz stereo (fmt=%d, latency=%u us)\n",
                                usb_dev, rate, static_cast<int>(fmt), latency_us);
                    break;
                }
            }
            if (!ok) {
                std::fprintf(stderr, "[ALSA] Failed to set any format on USB %s (%u Hz)\n", usb_dev, rate);
                snd_pcm_close(pcm);
                pcm = nullptr;
            }
        }
    }

    // 2. Вбудований ЦАП (internal_card з профілю) відкриваємо ТІЛЬКИ тоді, коли немає активного USB Audio!
    if (!pcm) {
        char int_dev[32];
        std::snprintf(int_dev, sizeof(int_dev), "hw:%d,0", activeProfile().internal_card);
        int err = snd_pcm_open(&pcm, int_dev, SND_PCM_STREAM_PLAYBACK, 0);
        if (err >= 0) {
            if (snd_pcm_set_params(
                    pcm,
                    SND_PCM_FORMAT_S16_LE,
                    SND_PCM_ACCESS_RW_INTERLEAVED,
                    2,
                    rate,
                    1,
                    latency_us
                ) < 0) {
                snd_pcm_close(pcm);
                pcm = nullptr;
            } else {
                active_fmt = SND_PCM_FORMAT_S16_LE;
                std::printf("[ALSA] Opened internal %s at %u Hz stereo S16_LE\n", int_dev, rate);
            }
        }
    }

    constexpr uint32_t kBlockFrames = 256;
    active_period_frames_.store(kBlockFrames, std::memory_order_relaxed);
    std::array<int16_t, kBlockFrames * 2> buf16{};
    std::array<int32_t, kBlockFrames * 2> buf32{};
    std::array<uint8_t, kBlockFrames * 2 * 3> buf24_3{};

    while (running_.load(std::memory_order_relaxed)) {
        renderBlock(buf16.data(), kBlockFrames, rate);

        if (pcm) {
            snd_pcm_sframes_t written = 0;
            if (active_fmt == SND_PCM_FORMAT_S32_LE) {
                for (size_t i = 0; i < buf16.size(); ++i) {
                    buf32[i] = static_cast<int32_t>(buf16[i]) << 16;
                }
                written = snd_pcm_writei(pcm, buf32.data(), kBlockFrames);
            } else if (active_fmt == SND_PCM_FORMAT_S24_LE) {
                for (size_t i = 0; i < buf16.size(); ++i) {
                    buf32[i] = static_cast<int32_t>(buf16[i]) << 8;
                }
                written = snd_pcm_writei(pcm, buf32.data(), kBlockFrames);
            } else if (active_fmt == SND_PCM_FORMAT_S24_3LE) {
                for (size_t i = 0; i < buf16.size(); ++i) {
                    int32_t v = static_cast<int32_t>(buf16[i]) << 8;
                    buf24_3[i * 3 + 0] = static_cast<uint8_t>(v & 0xFF);
                    buf24_3[i * 3 + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
                    buf24_3[i * 3 + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
                }
                written = snd_pcm_writei(pcm, buf24_3.data(), kBlockFrames);
            } else {
                written = snd_pcm_writei(pcm, buf16.data(), kBlockFrames);
            }
            if (written < 0) {
                snd_pcm_recover(pcm, static_cast<int>(written), 1);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    if (pcm) {
        snd_pcm_close(pcm);
    }
}
#endif

AudioEngineTelemetry AudioEngine::getTelemetry() const {
    AudioEngineTelemetry t;
    t.sample_rate = active_sample_rate_.load(std::memory_order_relaxed);
    t.period_frames = active_period_frames_.load(std::memory_order_relaxed);
    if (current_usb_48k_.load(std::memory_order_relaxed)) {
        t.device_label = "USB Audio (" + std::to_string(t.sample_rate) + " Hz)";
    } else {
        const auto& prof = activeProfile();
        t.device_label = prof.name + (current_hp_.load(std::memory_order_relaxed) ? " HP (" : " SPK (")
                         + std::to_string(t.sample_rate) + " Hz)";
    }
    t.vu_left = vu_l_.load(std::memory_order_relaxed);
    t.vu_right = vu_r_.load(std::memory_order_relaxed);
    t.peak_hold = peak_hold_.load(std::memory_order_relaxed);

    for (int i = 0; i < 4; ++i) {
        t.track_levels[i] = track_vu_[i].load(std::memory_order_relaxed);
    }

    uint32_t r_idx = scope_write_idx_.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < 256; ++i) {
        t.scope_samples[i] = scope_ring_[(r_idx + i) & 255u].load(std::memory_order_relaxed);
    }
    return t;
}

} // namespace groovebox
