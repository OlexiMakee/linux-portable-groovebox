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
int snd_pcm_drop(snd_pcm_t* pcm);
int snd_pcm_prepare(snd_pcm_t* pcm);
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
      delay_buf_r_(16384, 0.0f),
      chorus_buf_l_(2048, 0.0f),
      chorus_buf_r_(2048, 0.0f) {
    for (auto& s : scope_ring_) s.store(0.0f, std::memory_order_relaxed);
    for (auto& t : track_vu_) t.store(0.0f, std::memory_order_relaxed);
    initReverb(32000);
    initTrackFx(32000);
    initDefaultPattern();
    net_streamer_.start(8000, 32000);
}

AudioEngine::~AudioEngine() {
    stop();
    net_streamer_.stop();
}

void AudioEngine::initReverb(uint32_t sample_rate) {
    if (sample_rate == 0) sample_rate = 32000;
    reverb_sample_rate_ = sample_rate;
    static constexpr int kCombTuning[kNumCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr int kAllpassTuning[kNumAllpasses] = {556, 441, 341, 225};
    static constexpr int kStereoSpread = 23;
    float scale = static_cast<float>(sample_rate) / 44100.0f;
    for (int i = 0; i < kNumCombs; ++i) {
        size_t sl = static_cast<size_t>(std::max(16, static_cast<int>(kCombTuning[i] * scale)));
        size_t sr = static_cast<size_t>(std::max(16, static_cast<int>((kCombTuning[i] + kStereoSpread) * scale)));
        comb_l_[i].init(sl);
        comb_r_[i].init(sr);
    }
    for (int i = 0; i < kNumAllpasses; ++i) {
        size_t sl = static_cast<size_t>(std::max(8, static_cast<int>(kAllpassTuning[i] * scale)));
        size_t sr = static_cast<size_t>(std::max(8, static_cast<int>((kAllpassTuning[i] + kStereoSpread) * scale)));
        allpass_l_[i].init(sl);
        allpass_r_[i].init(sr);
    }
}

void AudioEngine::initTrackFx(uint32_t sample_rate) {
    if (sample_rate == 0) sample_rate = 32000;
    static constexpr int kCombTuning4[4] = {1116, 1277, 1422, 1557};
    static constexpr int kAllpassTuning2[2] = {556, 341};
    static constexpr int kStereoSpread = 23;
    float scale = static_cast<float>(sample_rate) / 44100.0f;

    for (int t = 0; t < kMaxTracks; ++t) {
        auto& fx = track_fx_[t];
        fx.srr_step = 0;
        fx.srr_l = fx.srr_r = 0.0f;
        fx.dist_hp_l = fx.dist_hp_r = 0.0f;
        fx.dist_bp_l = fx.dist_bp_r = 0.0f;
        fx.dist_lp_l = fx.dist_lp_r = 0.0f;

        fx.chorus_l.assign(2048, 0.0f);
        fx.chorus_r.assign(2048, 0.0f);
        fx.chorus_idx = 0;
        fx.chorus_phase = 0.0f;

        fx.delay_l.assign(65536, 0.0f);
        fx.delay_r.assign(65536, 0.0f);
        fx.delay_idx = 0;

        fx.rev_pre_l.assign(8192, 0.0f);
        fx.rev_pre_r.assign(8192, 0.0f);
        fx.rev_pre_idx = 0;

        for (int i = 0; i < 4; ++i) {
            size_t sl = static_cast<size_t>(std::max(16, static_cast<int>(kCombTuning4[i] * scale)));
            size_t sr = static_cast<size_t>(std::max(16, static_cast<int>((kCombTuning4[i] + kStereoSpread) * scale)));
            fx.comb_l[i].init(sl);
            fx.comb_r[i].init(sr);
        }
        for (int i = 0; i < 2; ++i) {
            size_t sl = static_cast<size_t>(std::max(8, static_cast<int>(kAllpassTuning2[i] * scale)));
            size_t sr = static_cast<size_t>(std::max(8, static_cast<int>((kAllpassTuning2[i] + kStereoSpread) * scale)));
            fx.allpass_l[i].init(sl);
            fx.allpass_r[i].init(sr);
        }
        fx.rev_lp_l = fx.rev_lp_r = 0.0f;
        fx.rev_hp_l = fx.rev_hp_r = 0.0f;
        fx.sc_env = 0.0f;
        fx.sc_gain = 1.0f;
    }
}

void AudioEngine::initDefaultPattern() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    // За замовчуванням 1 інструмент = 1 доріжка:
    // T1: SAMPLE ONESHOT (Kick 808)
    // T2: ACID 303 BASSLINE (Legato Slide, Accent)
    // T3: YAMAHA DX7 6-OP (ROM1A E.PIANO 1 / BASS 1 / LEAD)
    // T4: CHORD PAD (4-Voice Polyphonic Detuned Pad)
    tracks_[0] = TrackParameters{};
    std::snprintf(tracks_[0].name, sizeof(tracks_[0].name), "KICK 808");
    tracks_[0].engine = EngineType::kSampleOneShot;
    tracks_[0].p = {0.00f, 0.50f, 0.40f, 0.85f, 0.15f, 0.15f};
    tracks_[0].attack = 0.002f; tracks_[0].decay = 2.00f; tracks_[0].sustain = 0.00f; tracks_[0].release = 0.10f;
    tracks_[0].level = 0.90f; tracks_[0].pan = 0.00f; tracks_[0].delay_send = 0.0f; tracks_[0].reverb_send = 0.0f;

    static const char* kDefaultKickCandidates[] = {
        "/storage/roms/groovebox/samples/drums/01_tr808/bd.wav",
        "/storage/dev/groovebox/res/samples/drums/01_tr808/bd.wav",
        "res/samples/drums/01_tr808/bd.wav",
        "../res/samples/drums/01_tr808/bd.wav"
    };
    for (const char* kp : kDefaultKickCandidates) {
        if (sampler().loadWavFile(kp, sampler().trackSample(0))) {
            break;
        }
    }

    tracks_[1] = TrackParameters{};
    std::snprintf(tracks_[1].name, sizeof(tracks_[1].name), "303 BASS");
    tracks_[1].engine = EngineType::kAcid303;
    tracks_[1].p = {0.62f, 0.55f, 0.60f, 0.22f, 0.70f, 0.00f};
    tracks_[1].attack = 0.005f; tracks_[1].decay = 0.28f; tracks_[1].sustain = 0.00f; tracks_[1].release = 0.05f;
    tracks_[1].level = 0.85f; tracks_[1].pan = -0.15f; tracks_[1].delay_send = 0.0f; tracks_[1].reverb_send = 0.0f;

    tracks_[2] = TrackParameters{};
    std::snprintf(tracks_[2].name, sizeof(tracks_[2].name), "DX7 KEYS");
    tracks_[2].engine = EngineType::kDx7;
    tracks_[2].p = {0.32f, 0.75f, 0.60f, 0.40f, 0.00f, 0.30f};
    tracks_[2].attack = 0.005f; tracks_[2].decay = 0.45f; tracks_[2].sustain = 0.00f; tracks_[2].release = 0.05f;
    tracks_[2].level = 0.85f; tracks_[2].pan = 0.15f; tracks_[2].delay_send = 0.0f; tracks_[2].reverb_send = 0.0f;

    for (int i = 3; i < kMaxTracks; ++i) {
        tracks_[i] = TrackParameters{};
        std::snprintf(tracks_[i].name, sizeof(tracks_[i].name), "TRACK %02d", i + 1);
    }

    pattern_length_.store(64);
    active_rows_count_ = 3; // Рівно 3 доріжки за замовчуванням: Kick, 303, DX7

    // Мапінг 1:1 — кожен рядок відповідає своєму інструменту
    for (int i = 0; i < kMaxTracks; ++i) {
        row_track_[i] = static_cast<uint8_t>(i);
        row_voice_[i] = 0;
    }

    // Дефолтна довжина для всіх рядків: 16 кроків, усі кроки чисті та порожні
    for (int r = 0; r < kMaxTracks; ++r) {
        row_lengths_[r].store(16, std::memory_order_relaxed);
        for (uint32_t s = 0; s < kMaxSteps; ++s) {
            uint8_t def_note = 48;
            if (r == 0) def_note = 48;      // Sampler Root C2
            else if (r == 1) def_note = 36; // 303 C1
            else if (r == 2) def_note = 60; // DX7 C3
            else def_note = 48;
            pattern_[r][s] = {false, false, false, def_note, {def_note, 255, 255, 255}, 0.90f, 0.60f, 100, TrigCondition::kNone, false, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
        }
    }
}

TrackParameters AudioEngine::getTrackParams(int track) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return tracks_[std::clamp(track, 0, kMaxTracks - 1)];
}

void AudioEngine::setTrackParams(int track, const TrackParameters& tp) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    tracks_[std::clamp(track, 0, kMaxTracks - 1)] = tp;
}

void AudioEngine::cycleTrackEngine(int track, int dir) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& tr = tracks_[std::clamp(track, 0, kMaxTracks - 1)];
    int count = static_cast<int>(EngineType::kCount);
    int next = (static_cast<int>(tr.engine) + dir + count) % count;
    tr.engine = static_cast<EngineType>(next);
}

void AudioEngine::adjustTrackParam(int track, int param_idx, float delta) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& tr = tracks_[std::clamp(track, 0, kMaxTracks - 1)];
    if (param_idx >= 0 && param_idx < kParamsPerTrack) {
        tr.p[param_idx] = std::clamp(tr.p[param_idx] + delta, 0.0f, 1.0f);
    }
}

int AudioEngine::addTrack(EngineType engine, const std::string& name) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (active_rows_count_ >= kMaxTracks) return -1;
    int idx = active_rows_count_;
    auto& trk = tracks_[idx];
    trk = TrackParameters{};
    trk.engine = engine;
    if (!name.empty()) {
        std::snprintf(trk.name, sizeof(trk.name), "%s", name.c_str());
    } else {
        std::snprintf(trk.name, sizeof(trk.name), "%s %02d", getEngineTypeName(engine), idx + 1);
    }
    row_lengths_[idx].store(16, std::memory_order_relaxed);
    row_track_[idx] = static_cast<uint8_t>(idx);
    row_voice_[idx] = 0;
    pattern_[idx].fill(SequencerStep{});
    active_rows_count_++;
    return idx;
}

bool AudioEngine::deleteTrack(int track_idx) {
    deleteRow(track_idx);
    return true;
}

void AudioEngine::setTrackName(int track, const std::string& name) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int t = std::clamp(track, 0, kMaxTracks - 1);
    std::snprintf(tracks_[t].name, sizeof(tracks_[t].name), "%s", name.c_str());
}

std::string AudioEngine::getTrackName(int track) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int t = std::clamp(track, 0, kMaxTracks - 1);
    return tracks_[t].name[0] ? std::string(tracks_[t].name) : ("TRK " + std::to_string(t + 1));
}

SequencerStep AudioEngine::getStep(int row, int step_idx) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int r = std::clamp(row, 0, kTotalRows - 1);
    int s = std::clamp(step_idx, 0, static_cast<int>(kMaxSteps - 1));
    return pattern_[r][s];
}

void AudioEngine::setStep(int row, int step_idx, const SequencerStep& step) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int r = std::clamp(row, 0, kTotalRows - 1);
    int s = std::clamp(step_idx, 0, static_cast<int>(kMaxSteps - 1));
    pattern_[r][s] = step;
}

void AudioEngine::toggleStep(int row, int step_idx) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int r = std::clamp(row, 0, kTotalRows - 1);
    int s = std::clamp(step_idx, 0, static_cast<int>(kMaxSteps - 1));
    auto& st = pattern_[r][s];
    if (!st.enabled) {
        st.enabled = true;
        st.is_param_trig = false; // Звичайний Note Trig (Зелений/Бірюзовий)
    } else if (!st.is_param_trig) {
        st.is_param_trig = true;  // Жовтий Elektron Param Trig (тільки автоматизація)
    } else {
        st.enabled = false;
        st.is_param_trig = false; // Вимкнено
    }
}

void AudioEngine::clearStep(int row, int step_idx) {
    int r = std::clamp(row, 0, kTotalRows - 1);
    uint32_t s = std::clamp(static_cast<uint32_t>(step_idx), 0u, kMaxSteps - 1);
    std::lock_guard<std::mutex> lock(state_mutex_);
    pattern_[r][s].enabled = false;
    pattern_[r][s].is_param_trig = false;
    pattern_[r][s].has_plock = false;
    pattern_[r][s].plock_cutoff = -1.0f;
    pattern_[r][s].plock_val = -1.0f;
}

void AudioEngine::clearRow(int row) {
    int r = std::clamp(row, 0, kTotalRows - 1);
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (uint32_t s = 0; s < kMaxSteps; ++s) {
        pattern_[r][s].enabled = false;
        pattern_[r][s].is_param_trig = false;
        pattern_[r][s].has_plock = false;
        pattern_[r][s].plock_cutoff = -1.0f;
        pattern_[r][s].plock_val = -1.0f;
    }
}

void AudioEngine::deleteRow(int row) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (active_rows_count_ <= 1 || row < 0 || row >= active_rows_count_) return;
    for (int r = row; r < active_rows_count_ - 1; ++r) {
        tracks_[r] = tracks_[r + 1];
        pattern_[r] = pattern_[r + 1];
        row_lengths_[r].store(row_lengths_[r + 1].load(std::memory_order_relaxed), std::memory_order_relaxed);
        row_track_[r] = static_cast<uint8_t>(r);
        row_voice_[r] = 0;
    }
    tracks_[active_rows_count_ - 1] = TrackParameters{};
    pattern_[active_rows_count_ - 1].fill(SequencerStep{});
    row_lengths_[active_rows_count_ - 1].store(16, std::memory_order_relaxed);
    active_rows_count_--;
}

void AudioEngine::previewStep(int row, int step_idx) {
    int r = std::clamp(row, 0, kTotalRows - 1);
    uint32_t s = std::clamp(static_cast<uint32_t>(step_idx), 0u, kMaxSteps - 1);
    SequencerStep st;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        st = pattern_[r][s];
    }
    uint8_t note = st.note;
    if (!st.enabled) {
        note = 48 + (r % 4) * 4;
    }
    float vel = st.enabled ? st.velocity : 0.85f;
    float cut = st.has_plock ? st.plock_cutoff : -1.0f;
    int t = getRowTrack(r);
    triggerNote(t, note, vel, cut, st.slide, st.gate_len);
    if (st.enabled) {
        for (int cn = 1; cn < 4; ++cn) {
            uint8_t extra_n = st.chord_notes[cn];
            if (extra_n != 255 && extra_n != note) {
                triggerNote(t, extra_n, vel, cut, st.slide, st.gate_len);
            }
        }
    }
}

void AudioEngine::randomizeRow(int row) {
    int r = std::clamp(row, 0, kTotalRows - 1);
    uint32_t len = getRowLength(r);
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto randByte = [this]() -> uint32_t {
        rng_state_ = rng_state_ * 1664525u + 1013904223u;
        return (rng_state_ >> 16) & 0xFF;
    };

    static constexpr uint8_t kScale[8] = {36, 39, 41, 43, 46, 48, 51, 53};

    for (uint32_t s = 0; s < len; ++s) {
        uint32_t rnd = randByte();
        bool trig = (rnd < 120);
        uint8_t note = (r == 0) ? 48 : kScale[randByte() % 8];
        float vel = 0.80f + (randByte() % 20) * 0.01f;
        float gate = 0.50f + (randByte() % 30) * 0.01f;

        pattern_[r][s].enabled = trig;
        pattern_[r][s].is_param_trig = false;
        pattern_[r][s].slide = (randByte() < 30);
        pattern_[r][s].note = note;
        pattern_[r][s].velocity = vel;
        pattern_[r][s].gate_len = gate;
        pattern_[r][s].probability = 100;
        pattern_[r][s].condition = TrigCondition::kNone;
        pattern_[r][s].has_plock = false;
        pattern_[r][s].plock_cutoff = -1.0f;
    }
}

void AudioEngine::toggleParamTrig(int row, int step_idx) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int r = std::clamp(row, 0, kTotalRows - 1);
    int s = std::clamp(step_idx, 0, static_cast<int>(kMaxSteps - 1));
    auto& st = pattern_[r][s];
    st.enabled = true;
    st.is_param_trig = !st.is_param_trig;
}

void AudioEngine::adjustStepNote(int row, int step_idx, int semitones) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int r = std::clamp(row, 0, kTotalRows - 1);
    int s = std::clamp(step_idx, 0, static_cast<int>(kMaxSteps - 1));
    auto& st = pattern_[r][s];
    int n = std::clamp(static_cast<int>(st.note) + semitones, 24, 96);
    st.note = static_cast<uint8_t>(n);
    st.chord_notes[0] = st.note;
    st.enabled = true;
    st.is_param_trig = false;
}

void AudioEngine::setStepChordNote(int row, int step_idx, int slot, uint8_t note) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int r = std::clamp(row, 0, kTotalRows - 1);
    int s = std::clamp(step_idx, 0, static_cast<int>(kMaxSteps - 1));
    int sl = std::clamp(slot, 0, 3);
    auto& st = pattern_[r][s];
    st.chord_notes[sl] = note;
    if (sl == 0 && note != 255) st.note = note;
}

uint8_t AudioEngine::getStepChordNote(int row, int step_idx, int slot) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int r = std::clamp(row, 0, kTotalRows - 1);
    int s = std::clamp(step_idx, 0, static_cast<int>(kMaxSteps - 1));
    int sl = std::clamp(slot, 0, 3);
    return pattern_[r][s].chord_notes[sl];
}

void AudioEngine::lockStepCutoff(int row, int step_idx, float cutoff_val) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int r = std::clamp(row, 0, kTotalRows - 1);
    int s = std::clamp(step_idx, 0, static_cast<int>(kMaxSteps - 1));
    auto& st = pattern_[r][s];
    st.has_plock = true;
    st.plock_cutoff = std::clamp(cutoff_val, 0.05f, 0.95f);
}

void AudioEngine::panic() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (auto& v : voices_) {
        v.active = false;
        v.amp_adsr.reset();
        v.filter_adsr.reset();
        v.phase1 = v.phase2 = v.phase3 = v.phase4 = 0.0f;
        v.last_out = 0.0f;
        v.svf_low = v.svf_band = 0.0f;
        v.hpf_low = v.hpf_band = 0.0f;
        v.lpf_env_val = v.hpf_env_val = 0.0f;
        v.gate_samples_left = -1;
        v.is_legato = false;
    }
    std::fill(delay_buf_l_.begin(), delay_buf_l_.end(), 0.0f);
    std::fill(delay_buf_r_.begin(), delay_buf_r_.end(), 0.0f);
    for (auto& c : comb_l_) c.clear();
    for (auto& c : comb_r_) c.clear();
    for (auto& a : allpass_l_) a.clear();
    for (auto& a : allpass_r_) a.clear();
    vu_l_.store(0.0f, std::memory_order_relaxed);
    vu_r_.store(0.0f, std::memory_order_relaxed);
    peak_hold_.store(0.0f, std::memory_order_relaxed);
    for (auto& t : track_vu_) t.store(0.0f, std::memory_order_relaxed);
}

void AudioEngine::toggleSoloTrack(int track) {
    if (track < 0 || track >= kMaxTracks) return;
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (soloed_track_ == track) {
        // Unsolo: відновлюємо попередню маску м'ютів
        for (int i = 0; i < kMaxTracks; ++i) {
            tracks_[i].muted = prev_mute_mask_[i];
        }
        soloed_track_ = -1;
    } else {
        if (soloed_track_ == -1) {
            // Зберігаємо поточну маску м'ютів перед переходом у режим соло
            for (int i = 0; i < kMaxTracks; ++i) {
                prev_mute_mask_[i] = tracks_[i].muted;
            }
        }
        // Вмикаємо соло для обраного треку, усі інші глушимо
        for (int i = 0; i < kMaxTracks; ++i) {
            tracks_[i].muted = (i != track);
        }
        soloed_track_ = track;
    }
}

uint32_t AudioEngine::getRowLength(int row) const {
    int r = std::clamp(row, 0, kTotalRows - 1);
    return row_lengths_[r].load(std::memory_order_relaxed);
}

void AudioEngine::setRowLength(int row, uint32_t len) {
    int r = std::clamp(row, 0, kTotalRows - 1);
    row_lengths_[r].store(std::clamp(len, 1u, kMaxSteps), std::memory_order_relaxed);
}

uint32_t AudioEngine::cycleRowLength(int row) {
    int r = std::clamp(row, 0, kTotalRows - 1);
    uint32_t cur = row_lengths_[r].load(std::memory_order_relaxed);
    uint32_t next = (cur >= kMaxSteps) ? 4u : std::min(kMaxSteps, ((cur / 4u) + 1u) * 4u);
    row_lengths_[r].store(next, std::memory_order_relaxed);
    return next;
}

int AudioEngine::getRowTrack(int row) const {
    int r = std::clamp(row, 0, kTotalRows - 1);
    return row_track_[r];
}

int AudioEngine::getRowVoice(int row) const {
    int r = std::clamp(row, 0, kTotalRows - 1);
    return row_voice_[r];
}

void AudioEngine::triggerVoice(int row, int midi_note, float velocity, float plock_cutoff, bool slide, float gate_len) {
    if (row < 0 || row >= kTotalVoices) return;
    std::lock_guard<std::mutex> lock(state_mutex_);
    int t = row / kVoicesPerTrack;
    if (t < 0 || t >= kMaxTracks) return;
    if (tracks_[t].muted) return;

    if (tracks_[t].lfo1.mode == LfoMode::kTrig) {
        track_lfo1_phase_[t] = 0.0f;
    }
    if (tracks_[t].lfo2.mode == LfoMode::kTrig) {
        track_lfo2_phase_[t] = 0.0f;
    }
    if (tracks_[t].lfo3.mode == LfoMode::kTrig) {
        track_lfo3_phase_[t] = 0.0f;
    }
    if (tracks_[t].lfo4.mode == LfoMode::kTrig) {
        track_lfo4_phase_[t] = 0.0f;
    }

    if (tracks_[t].engine == EngineType::kMidiOut) {
        int ch = std::clamp(static_cast<int>(tracks_[t].p[0] * 15.99f), 0, 15);
        if (midi_out_cb_) {
            midi_out_cb_(ch, midi_note, std::clamp(velocity, 0.05f, 1.0f), true);
        }
        voices_[row].active = true;
        voices_[row].note_num = static_cast<uint8_t>(midi_note);
        voices_[row].gate_samples_left = static_cast<int32_t>(0.125f * std::clamp(gate_len, 0.1f, 128.0f) * static_cast<float>(active_sample_rate_.load()));
        return;
    }

    auto& v = voices_[row];
    const float sr = static_cast<float>(active_sample_rate_.load());
    v.active = true;
    v.velocity = std::clamp(velocity, 0.05f, 1.0f);
    v.plock_cutoff = plock_cutoff;
    v.pitch_env = 1.0f;
    // Constant-time RC glide (TB-303 класика = 65 мс, налаштовується параметром TRK.GLIDE)
    v.glide_coef = std::exp(-1.0f / (std::clamp(tracks_[t].glide, 0.005f, 0.5f) * sr));
    // Транспозиція треку (TRK.TRANSP); нота 0 у барабанах означає «дефолтна нота голосу»
    if (midi_note > 0) {
        midi_note = std::clamp(midi_note + tracks_[t].transpose, 1, 127);
    }

    // Тривалість кроку секвенсора з урахуванням дискретної довжини (1..128 кроків, 1 крок = ~125мс)
    float step_sec = 0.125f;
    v.gate_samples_left = static_cast<int32_t>(step_sec * std::clamp(gate_len, 0.1f, 128.0f) * sr);

    // Ініціалізація огинаючих фільтрів LPF та HPF
    v.lpf_env_val = 1.0f;
    v.lpf_decay_coef = std::exp(-1.0f / (std::clamp(tracks_[t].lpf_decay, 0.01f, 4.0f) * sr));
    v.hpf_env_val = 1.0f;
    v.hpf_decay_coef = std::exp(-1.0f / (std::clamp(tracks_[t].hpf_decay, 0.01f, 4.0f) * sr));

    if (tracks_[t].engine == EngineType::kAcid303 && slide && v.amp_adsr.isActive()) {
        // Roland TB-303 Legato Slide: нова цільова нота, без скидання фази чи огинаючої!
        v.target_pitch = static_cast<float>(midi_note);
        v.is_legato = true;
    } else {
        v.target_pitch = static_cast<float>(midi_note);
        v.current_pitch = v.target_pitch;
        v.is_legato = false;
        v.freq = midiToFreq(midi_note);

        if (tracks_[t].engine == EngineType::kDx7) {
            int p_idx = std::clamp(static_cast<int>(tracks_[t].p[0] * (dx7_engine_.patchCount() - 1) + 0.5f), 0, dx7_engine_.patchCount() - 1);
            v.dx7_voice.trigger(dx7_engine_.getPatch(p_idx), static_cast<float>(midi_note), velocity, sr);
            v.amp_adsr.trigger(0.001f, 0.99f, 1.0f, sr);
            v.filter_adsr.trigger(0.001f, 0.99f, 1.0f, sr);
        } else if (tracks_[t].engine == EngineType::kSampleOneShot || tracks_[t].engine == EngineType::kSliceSampler) {
            const auto& sb = (tracks_[t].engine == EngineType::kSampleOneShot) ? sampler().trackSample(t) : sampler().recordedSample();
            float total_f = (sb.loaded && sb.lengthFrames() > 0) ? static_cast<float>(sb.lengthFrames()) : 1000.0f;
            float start_f = std::clamp(tracks_[t].sample.start, 0.0f, 0.999f) * total_f;
            float end_f = std::clamp(tracks_[t].sample.end, tracks_[t].sample.start + 0.001f, 1.0f) * total_f;
            if (tracks_[t].sample.loop_mode == SampleLoopMode::kReverse) {
                v.phase1 = end_f - 1.0f;
                v.loop_forward = false;
            } else {
                v.phase1 = start_f;
                v.loop_forward = true;
            }
            v.amp_adsr.trigger(tracks_[t].attack, tracks_[t].decay, tracks_[t].sustain, sr);
            v.filter_adsr.trigger(tracks_[t].attack * 0.6f, tracks_[t].decay, tracks_[t].sustain, sr);
        } else {
            v.amp_adsr.trigger(tracks_[t].attack, tracks_[t].decay, tracks_[t].sustain, sr);
            v.filter_adsr.trigger(tracks_[t].attack * 0.6f, tracks_[t].decay, tracks_[t].sustain, sr);
        }
        v.mod_env1.trigger(tracks_[t].env1.attack, tracks_[t].env1.decay, tracks_[t].env1.sustain, sr);
        v.mod_env2.trigger(tracks_[t].env2.attack, tracks_[t].env2.decay, tracks_[t].env2.sustain, sr);
    }
}

int AudioEngine::allocateVoice(int track, int midi_note) {
    if (track < 0 || track >= kMaxTracks) return 0;
    const int base = track * kVoicesPerTrack;

    // 1. Якщо та ж нота вже звучить у голосі треку — перевикористовуємо її
    for (int i = 0; i < kVoicesPerTrack; ++i) {
        int r = base + i;
        if (voices_[r].active && voices_[r].note_num == midi_note) {
            return r;
        }
    }

    // 2. Шукаємо вільний (idle) голос для справжньої поліфонії
    for (int i = 0; i < kVoicesPerTrack; ++i) {
        int r = base + i;
        if (!voices_[r].active || voices_[r].amp_adsr.stage == AdsrStage::kIdle) {
            return r;
        }
    }

    // 3. Шукаємо голос у стадії kRelease з найменшим значенням амплітуди
    int best_rel = -1;
    float min_val = 999.0f;
    for (int i = 0; i < kVoicesPerTrack; ++i) {
        int r = base + i;
        if (voices_[r].amp_adsr.stage == AdsrStage::kRelease && voices_[r].amp_adsr.value < min_val) {
            min_val = voices_[r].amp_adsr.value;
            best_rel = r;
        }
    }
    if (best_rel >= 0) return best_rel;

    // 4. Voice Stealing: крадемо найстаріший голос (LRU)
    int oldest_r = base;
    uint32_t min_time = 0xFFFFFFFFu;
    for (int i = 0; i < kVoicesPerTrack; ++i) {
        int r = base + i;
        if (voices_[r].trig_time < min_time) {
            min_time = voices_[r].trig_time;
            oldest_r = r;
        }
    }
    return oldest_r;
}

void AudioEngine::triggerNote(int track, int midi_note, float velocity, float plock_cutoff, bool slide, float gate_len) {
    if (track < 0 || track >= kMaxTracks) return;
    int row = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        row = allocateVoice(track, midi_note);
        voices_[row].note_num = static_cast<uint8_t>(midi_note);
        voices_[row].trig_time = ++voice_alloc_counter_;
    }
    triggerVoice(row, midi_note, velocity, plock_cutoff, slide, gate_len);
}

void AudioEngine::noteOff(int track, int midi_note) {
    if (track < 0 || track >= kMaxTracks) return;
    const int base = track * kVoicesPerTrack;
    const float sr = static_cast<float>(active_sample_rate_.load(std::memory_order_relaxed));
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (int i = 0; i < kVoicesPerTrack; ++i) {
        int r = base + i;
        if (voices_[r].active && voices_[r].note_num == midi_note) {
            voices_[r].amp_adsr.release(tracks_[track].release, sr);
            voices_[r].filter_adsr.release(tracks_[track].release, sr);
        }
    }
}

void AudioEngine::noteOffAll() {
    const float sr = static_cast<float>(active_sample_rate_.load(std::memory_order_relaxed));
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (int r = 0; r < kTotalVoices; ++r) {
        int t = r / kVoicesPerTrack;
        voices_[r].amp_adsr.release(tracks_[t].release, sr);
        voices_[r].filter_adsr.release(tracks_[t].release, sr);
    }
}

void AudioEngine::triggerSequencerStep(uint32_t master_tick) {
    bool is_fill = fill_mode_.load(std::memory_order_relaxed);
    std::array<SequencerStep, kTotalRows> steps;
    int num_rows = active_rows_count_;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (master_tick == 0) {
            cycle_counter_ = 0;
        } else if (master_tick % 16 == 0) {
            cycle_counter_++;
        }
        for (int r = 0; r < num_rows; ++r) {
            uint32_t r_len = row_lengths_[r].load(std::memory_order_relaxed);
            if (r_len == 0) r_len = 16;
            uint32_t st = master_tick % r_len;
            steps[r] = pattern_[r][st];
        }
    }

    auto randByte = [this]() -> uint32_t {
        rng_state_ = rng_state_ * 1664525u + 1013904223u;
        return (rng_state_ >> 16) & 0xFF;
    };

    for (int r = 0; r < num_rows; ++r) {
        if (!steps[r].enabled) {
            prev_step_fired_[r] = false;
            continue;
        }

        // Перевірка Elektron Probability (0..100%)
        if (steps[r].probability < 100) {
            uint32_t roll = (randByte() % 100);
            if (roll >= steps[r].probability) {
                prev_step_fired_[r] = false;
                continue;
            }
        }

        // Перевірка Elektron Trig Conditions
        bool condition_met = true;
        uint32_t c = cycle_counter_;
        switch (steps[r].condition) {
            case TrigCondition::kNone:     condition_met = true; break;
            case TrigCondition::kFill:     condition_met = is_fill; break;
            case TrigCondition::kNotFill:  condition_met = !is_fill; break;
            case TrigCondition::kPre:      condition_met = prev_step_fired_[r]; break;
            case TrigCondition::kNotPre:   condition_met = !prev_step_fired_[r]; break;
            case TrigCondition::k1_2:      condition_met = ((c % 2) == 0); break;
            case TrigCondition::k2_2:      condition_met = ((c % 2) == 1); break;
            case TrigCondition::k1_4:      condition_met = ((c % 4) == 0); break;
            case TrigCondition::k2_4:      condition_met = ((c % 4) == 1); break;
            case TrigCondition::k3_4:      condition_met = ((c % 4) == 2); break;
            case TrigCondition::k4_4:      condition_met = ((c % 4) == 3); break;
            case TrigCondition::kFirst:    condition_met = (c == 0); break;
            case TrigCondition::kNotFirst: condition_met = (c > 0); break;
            default: condition_met = true; break;
        }

        if (!condition_met) {
            prev_step_fired_[r] = false;
            continue;
        }

        prev_step_fired_[r] = true;

        int t = getRowTrack(r);
        if (steps[r].is_param_trig) {
            // Жовтий Elektron Trig (Param Trig / P-Lock): лише оновлюємо параметри, без нової ноти!
            std::lock_guard<std::mutex> lock(state_mutex_);
            int base = t * kVoicesPerTrack;
            for (int vi = 0; vi < kVoicesPerTrack; ++vi) {
                voices_[base + vi].plock_cutoff = steps[r].has_plock ? steps[r].plock_cutoff : -1.0f;
            }
        } else {
            // Звичайний Note Trig (головна нота)
            triggerNote(
                t,
                steps[r].note,
                steps[r].velocity,
                steps[r].has_plock ? steps[r].plock_cutoff : -1.0f,
                steps[r].slide,
                steps[r].gate_len
            );

            // Поліфонічні ноти акорду (слоти 1..3 для Poly-треків)
            for (int cn = 1; cn < 4; ++cn) {
                uint8_t extra_n = steps[r].chord_notes[cn];
                if (extra_n != 255 && extra_n != steps[r].note) {
                    triggerNote(
                        t,
                        extra_n,
                        steps[r].velocity,
                        steps[r].has_plock ? steps[r].plock_cutoff : -1.0f,
                        steps[r].slide,
                        steps[r].gate_len
                    );
                }
            }
        }
    }
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
    const bool use_usb = (card_index > 0) && is_usb_48k && !force_internal_.load(std::memory_order_relaxed);
    const uint32_t target_rate = use_usb ? static_cast<uint32_t>(prof.usb_rate) : static_cast<uint32_t>(prof.internal_rate);
    active_sample_rate_.store(target_rate);
    active_period_frames_.store(target_period_frames_.load(std::memory_order_relaxed));

    if (card_index >= 0) {
        applyRk817HardwareMux(headphones_plugged);
        applyRk817HardwareVolume(params_.master_volume.load());
    }

    running_.store(true);

    if (card_index < 0) {
        // Офлайн режим тестування без запуску фонових апаратних потоків
        return true;
    }

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
    panic();
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
    if (current_card_.load() == 0 || force_internal_.load(std::memory_order_relaxed)) {
        applyRk817HardwareVolume(next);
    }
}

void AudioEngine::toggleOutputOverride() {
    bool next = !force_internal_.load(std::memory_order_relaxed);
    force_internal_.store(next, std::memory_order_relaxed);
    int card = current_card_.load();
    bool is_usb = current_usb_48k_.load();
    bool hp = current_hp_.load();
    start(card, is_usb, hp);
}

void AudioEngine::cycleBufferSize() {
    uint32_t cur = target_period_frames_.load(std::memory_order_relaxed);
    uint32_t next = 1024;
    if (cur == 96) next = 128;
    else if (cur == 128) next = 256;
    else if (cur == 256) next = 512;
    else if (cur == 512) next = 1024;
    else if (cur == 1024) next = 2048;
    else next = 96;
    target_period_frames_.store(next, std::memory_order_relaxed);
    std::printf("[AudioEngine] Buffer size cycled to %u frames (%.1f ms)\n",
                next, (next * 1000.0f) / active_sample_rate_.load());
    int card = current_card_.load();
    bool is_usb = current_usb_48k_.load();
    bool hp = current_hp_.load();
    start(card, is_usb, hp);
}

bool AudioEngine::isOutputForcedInternal() const {
    return force_internal_.load(std::memory_order_relaxed);
}

void AudioEngine::renderBlock(int16_t* out_interleaved, uint32_t frames, uint32_t sample_rate) {
    const float inv_sr = 1.0f / static_cast<float>(sample_rate);
    const float master_vol = params_.master_volume.load(std::memory_order_relaxed);
    const float cut_macro = params_.cutoff_macro.load(std::memory_order_relaxed);
    const float res_macro = params_.resonance_macro.load(std::memory_order_relaxed);
    const float env_macro = params_.env_mod_macro.load(std::memory_order_relaxed);
    const float fm_macro  = params_.fm_macro.load(std::memory_order_relaxed);
    const float raw_lx    = params_.stick_lx.load(std::memory_order_relaxed);
    const float raw_ly    = params_.stick_ly.load(std::memory_order_relaxed);
    const float raw_rx    = params_.stick_rx.load(std::memory_order_relaxed);
    const float raw_ry    = params_.stick_ry.load(std::memory_order_relaxed);
    const float dly_fb    = params_.delay_feedback.load(std::memory_order_relaxed);
    const float dly_mix   = params_.delay_mix.load(std::memory_order_relaxed);
    const float rev_dec   = params_.reverb_decay.load(std::memory_order_relaxed);
    const float rev_mix   = params_.reverb_mix.load(std::memory_order_relaxed);
    const float chr_rate  = params_.chorus_rate.load(std::memory_order_relaxed);
    const float chr_depth = params_.chorus_depth.load(std::memory_order_relaxed);
    const float chr_mix   = params_.chorus_mix.load(std::memory_order_relaxed);
    const float drv_amt   = params_.drive_amount.load(std::memory_order_relaxed);
    const float drv_tone  = params_.drive_tone.load(std::memory_order_relaxed);

    std::array<TrackParameters, kMaxTracks> local_tracks;
    MasterFxParameters local_mfx;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        local_tracks = tracks_;
        local_mfx = master_fx_;
    }

    auto computeLfo = [](const LfoParameters& lfo, float& phase, float dt, float rate_mod, float depth_mod) -> float {
        float eff_depth = std::clamp(lfo.depth + depth_mod, 0.0f, 1.0f);
        if (eff_depth <= 0.001f) return 0.0f;
        float mult = (lfo.multiplier > 0) ? static_cast<float>(lfo.multiplier) : 1.0f;
        float eff_rate = std::clamp(lfo.rate + rate_mod, 0.0f, 1.0f);
        float rate_hz = (0.05f + eff_rate * 19.95f) * mult;
        phase += rate_hz * dt;
        if (phase >= 1.0f) phase -= std::floor(phase);
        float p = phase;
        float raw = 0.0f;
        switch (lfo.wave) {
            case LfoWave::kSine:
                raw = std::sin(p * 6.2831853f);
                break;
            case LfoWave::kTriangle:
                raw = 4.0f * std::fabs(p - 0.5f) - 1.0f;
                break;
            case LfoWave::kSaw:
                raw = 2.0f * p - 1.0f;
                break;
            case LfoWave::kSquare:
                raw = (p < 0.5f) ? 1.0f : -1.0f;
                break;
            case LfoWave::kRandomSnh:
                if (p < 0.05f) lfo.value = ((float)(rand() % 1000) / 500.0f) - 1.0f;
                raw = lfo.value;
                break;
            default:
                raw = 0.0f;
                break;
        }
        return raw * eff_depth * 0.40f;
    };

    float dt_block = inv_sr * static_cast<float>(frames);
    std::array<std::array<float, static_cast<size_t>(ModTarget::kCount)>, kMaxTracks> block_mod{};

    // 1. Аналогові стіки (Mode 1 & Mode 2 для обох стіків)
    for (int t = 0; t < active_rows_count_; ++t) {
        const auto& tp = local_tracks[t];
        auto addStk = [&](ModTarget tgt, float amt, float raw) {
            if (tgt != ModTarget::kNone && tgt < ModTarget::kCount && amt > 0.001f) {
                block_mod[t][static_cast<size_t>(tgt)] += raw * amt;
            }
        };
        // Left stick Mode 1 & 2
        addStk(tp.stick_l_m1.dest_x, tp.stick_l_m1.amount_x, raw_lx);
        addStk(tp.stick_l_m1.dest_y, tp.stick_l_m1.amount_y, raw_ly);
        addStk(tp.stick_l_m2.dest_x, tp.stick_l_m2.amount_x, raw_lx);
        addStk(tp.stick_l_m2.dest_y, tp.stick_l_m2.amount_y, raw_ly);
        // Right stick Mode 1 & 2
        addStk(tp.stick_r_m1.dest_x, tp.stick_r_m1.amount_x, raw_rx);
        addStk(tp.stick_r_m1.dest_y, tp.stick_r_m1.amount_y, raw_ry);
        addStk(tp.stick_r_m2.dest_x, tp.stick_r_m2.amount_x, raw_rx);
        addStk(tp.stick_r_m2.dest_y, tp.stick_r_m2.amount_y, raw_ry);
    }

    // 2. 4 LFO з підтримкою крос-модуляції швидкості та глибини
    for (int t = 0; t < active_rows_count_; ++t) {
        auto& tp = local_tracks[t];
        auto evalLfo = [&](const LfoParameters& lfo, float& phase, ModTarget r_tgt, ModTarget d_tgt) {
            float r_mod = block_mod[t][static_cast<size_t>(r_tgt)];
            float d_mod = block_mod[t][static_cast<size_t>(d_tgt)];
            float val = computeLfo(lfo, phase, dt_block, r_mod, d_mod);
            if (lfo.dest != ModTarget::kNone && lfo.dest < ModTarget::kCount) {
                block_mod[t][static_cast<size_t>(lfo.dest)] += val;
            }
        };
        evalLfo(tp.lfo1, track_lfo1_phase_[t], ModTarget::kLfo1Rate, ModTarget::kLfo1Depth);
        evalLfo(tp.lfo2, track_lfo2_phase_[t], ModTarget::kLfo2Rate, ModTarget::kLfo2Depth);
        evalLfo(tp.lfo3, track_lfo3_phase_[t], ModTarget::kLfo3Rate, ModTarget::kLfo3Depth);
        evalLfo(tp.lfo4, track_lfo4_phase_[t], ModTarget::kLfo4Rate, ModTarget::kLfo4Depth);
    }

    // 3. 2 ADSR огинаючі модуляцій
    for (int t = 0; t < active_rows_count_; ++t) {
        const auto& tp = local_tracks[t];
        const int base_v = t * kVoicesPerTrack;
        float e1_sum = 0.0f;
        float e2_sum = 0.0f;
        int active_v_cnt = 0;
        for (int vi = 0; vi < kVoicesPerTrack; ++vi) {
            auto& v = voices_[base_v + vi];
            if (v.active) {
                e1_sum += v.mod_env1.process();
                e2_sum += v.mod_env2.process();
                active_v_cnt++;
            }
        }
        if (active_v_cnt > 0) {
            float inv_c = 1.0f / static_cast<float>(active_v_cnt);
            if (tp.env1.dest != ModTarget::kNone && tp.env1.dest < ModTarget::kCount) {
                block_mod[t][static_cast<size_t>(tp.env1.dest)] += (e1_sum * inv_c) * tp.env1.amount;
            }
            if (tp.env2.dest != ModTarget::kNone && tp.env2.dest < ModTarget::kCount) {
                block_mod[t][static_cast<size_t>(tp.env2.dest)] += (e2_sum * inv_c) * tp.env2.amount;
            }
        }
        // Зберігаємо зміщення для візуалізації в UI
        live_mod_offsets_[t] = block_mod[t];
    }

    auto getTrackMod = [&](int trk, ModTarget dest) -> float {
        if (dest == ModTarget::kNone || dest >= ModTarget::kCount) return 0.0f;
        return block_mod[trk][static_cast<size_t>(dest)];
    };

    if (reverb_sample_rate_ != sample_rate) {
        initReverb(sample_rate);
        initTrackFx(sample_rate);
    }

    float max_l = 0.0f;
    float max_r = 0.0f;
    float max_dly_send = 0.0f;
    float max_rev_send = 0.0f;
    float max_chr_send = 0.0f;
    std::array<float, kMaxTracks> trk_peak{};
    uint32_t sc_idx = scope_write_idx_.load(std::memory_order_relaxed);
    const uint32_t delay_len = (sample_rate == 48000u) ? 12000u : 8000u; // ~250ms tempo delay

    for (uint32_t i = 0; i < frames; ++i) {
        float mix_l = 0.0f;
        float mix_r = 0.0f;
        float send_l = 0.0f;
        float send_r = 0.0f;
        float rev_l = 0.0f;
        float rev_r = 0.0f;

        for (int t = 0; t < active_rows_count_; ++t) {
            const auto& tp = local_tracks[t];
            if (tp.muted) continue;

            float cut_offset = cut_macro + getTrackMod(t, StickDest::kCutoff);
            float env_offset = env_macro + getTrackMod(t, StickDest::kEnvMod);
            float res_offset = res_macro + getTrackMod(t, StickDest::kResonance);
            float hpf_offset = getTrackMod(t, StickDest::kHpfCutoff);
            float hpf_env_offset = getTrackMod(t, StickDest::kHpfEnv);
            float lpf_reso = std::clamp(tp.lpf_resonance + res_offset, 0.0f, 0.95f);
            float lpf_q = std::clamp(2.0f - 1.85f * lpf_reso, 0.15f, 2.0f);
            float hpf_reso = std::clamp(tp.hpf_resonance, 0.0f, 0.95f);
            float hpf_q = std::clamp(2.0f - 1.85f * hpf_reso, 0.15f, 2.0f);
            float trk_level = std::clamp(tp.level + getTrackMod(t, StickDest::kLevel), 0.0f, 1.0f);
            float trk_pan = std::clamp(tp.pan + getTrackMod(t, StickDest::kPan), -1.0f, 1.0f);
            float pan_l = std::clamp(1.0f - trk_pan, 0.0f, 1.0f) * 0.5f;
            float pan_r = std::clamp(1.0f + trk_pan, 0.0f, 1.0f) * 0.5f;
            float d_send = std::clamp(tp.delay_send + getTrackMod(t, StickDest::kDelaySend), 0.0f, 1.0f);
            float r_send = std::clamp(tp.reverb_send + getTrackMod(t, StickDest::kReverbSend), 0.0f, 1.0f);

            float trk_sum = 0.0f;
            const int base_v = t * kVoicesPerTrack;

            for (int vi = 0; vi < kVoicesPerTrack; ++vi) {
                auto& v = voices_[base_v + vi];
                if (!v.active) continue;

                if (v.gate_samples_left > 0) {
                    v.gate_samples_left -= 1;
                    if (v.gate_samples_left <= 0) {
                        if (tp.engine == EngineType::kMidiOut) {
                            int ch = std::clamp(static_cast<int>(tp.p[0] * 15.99f), 0, 15);
                            if (midi_out_cb_) midi_out_cb_(ch, v.note_num, 0.0f, false);
                            v.active = false;
                            continue;
                        }
                        if (tp.engine == EngineType::kDx7) {
                            v.dx7_voice.release(static_cast<float>(sample_rate));
                        }
                        v.amp_adsr.release(tp.release, static_cast<float>(sample_rate));
                        v.filter_adsr.release(tp.release, static_cast<float>(sample_rate));
                        v.mod_env1.release(tp.env1.release, static_cast<float>(sample_rate));
                        v.mod_env2.release(tp.env2.release, static_cast<float>(sample_rate));
                    }
                }

                if (tp.engine == EngineType::kMidiOut) continue;

                float amp = v.amp_adsr.process();
                float f_env = v.filter_adsr.process();
                if (!v.amp_adsr.isActive() && amp < 0.0005f) {
                    v.active = false;
                    continue;
                }

                float raw = 0.0f;

                if (tp.engine == EngineType::kSampleOneShot) {
                    const auto& sb = sampler().trackSample(t);
                    if (sb.loaded && sb.lengthFrames() > 0) {
                        float total_f = static_cast<float>(sb.lengthFrames());
                        float start_f = std::clamp(tp.sample.start, 0.0f, 0.999f) * total_f;
                        float loop_f = std::clamp(tp.sample.loop_point, 0.0f, 0.999f) * total_f;
                        float end_f = std::clamp(tp.sample.end, 0.001f, 1.0f) * total_f;
                        if (end_f <= start_f) end_f = std::min(total_f, start_f + 10.0f);
                        if (loop_f >= end_f) loop_f = start_f;

                        float note_pitch = static_cast<float>(v.note_num) - 48.0f + tp.sample.tune + (tp.p[1] - 0.5f) * 48.0f;
                        float pitch_ratio = std::pow(2.0f, note_pitch / 12.0f);
                        float step = pitch_ratio * (static_cast<float>(sb.sample_rate) * inv_sr);

                        uint32_t f0 = static_cast<uint32_t>(std::clamp(v.phase1, 0.0f, total_f - 1.0f));
                        uint32_t f1 = std::min(f0 + 1, sb.lengthFrames() - 1);
                        float frac = v.phase1 - static_cast<float>(f0);
                        raw = sb.data_l[f0] + frac * (sb.data_l[f1] - sb.data_l[f0]);

                        if (tp.sample.loop_mode == SampleLoopMode::kOff) {
                            v.phase1 += step;
                            if (v.phase1 >= end_f || v.phase1 >= total_f - 1.0f) {
                                raw = 0.0f;
                                v.active = false;
                            }
                        } else if (tp.sample.loop_mode == SampleLoopMode::kForward) {
                            v.phase1 += step;
                            if (v.phase1 >= end_f) {
                                float loop_len = std::max(10.0f, end_f - loop_f);
                                v.phase1 = loop_f + std::fmod(v.phase1 - loop_f, loop_len);
                            }
                        } else if (tp.sample.loop_mode == SampleLoopMode::kReverse) {
                            v.phase1 -= step;
                            if (v.phase1 <= loop_f) {
                                float loop_len = std::max(10.0f, end_f - loop_f);
                                v.phase1 = end_f - std::fmod(loop_f - v.phase1, loop_len);
                            }
                        } else if (tp.sample.loop_mode == SampleLoopMode::kPingPong) {
                            if (v.loop_forward) {
                                v.phase1 += step;
                                if (v.phase1 >= end_f) {
                                    v.phase1 = std::max(loop_f, end_f - (v.phase1 - end_f));
                                    v.loop_forward = false;
                                }
                            } else {
                                v.phase1 -= step;
                                if (v.phase1 <= loop_f) {
                                    v.phase1 = std::min(end_f, loop_f + (loop_f - v.phase1));
                                    v.loop_forward = true;
                                }
                            }
                        }
                        track_playhead_[t].store(v.phase1 / total_f, std::memory_order_relaxed);
                    } else {
                        raw = 0.0f;
                    }
                } else if (tp.engine == EngineType::kSliceSampler) {
                    const auto& sb = sampler().recordedSample();
                    int s_idx = std::clamp(static_cast<int>(tp.p[1] * (sampler().sliceCount() - 1) + 0.5f), 0, sampler().sliceCount() - 1);
                    SlicePoint pt = sampler().getSlice(s_idx);
                    if (sb.loaded && pt.length_frames > 0) {
                        float pitch_ratio = std::pow(2.0f, (tp.sample.tune / 12.0f) + (tp.p[2] - 0.5f) * 4.0f);
                        float step = pitch_ratio * (static_cast<float>(sb.sample_rate) * inv_sr);
                        uint32_t cur_f = pt.start_frame + static_cast<uint32_t>(v.phase1);
                        if (v.phase1 < static_cast<float>(pt.length_frames) && cur_f + 1 < sb.lengthFrames()) {
                            float frac = v.phase1 - static_cast<float>(static_cast<uint32_t>(v.phase1));
                            raw = sb.data_l[cur_f] + frac * (sb.data_l[cur_f + 1] - sb.data_l[cur_f]);
                            v.phase1 += step;
                            if (sb.lengthFrames() > 0) {
                                track_playhead_[t].store(static_cast<float>(cur_f) / sb.lengthFrames(), std::memory_order_relaxed);
                            }
                        } else {
                            raw = 0.0f;
                            v.active = false;
                        }
                    } else {
                        raw = 0.0f;
                    }
                } else if (tp.engine == EngineType::kDx7) {
                    int p_idx = std::clamp(static_cast<int>(tp.p[0] * (dx7_engine_.patchCount() - 1) + 0.5f), 0, dx7_engine_.patchCount() - 1);
                    const auto& patch = dx7_engine_.getPatch(p_idx);
                    float bright = std::clamp(tp.p[2] * 2.0f, 0.0f, 3.0f);
                    float dec = std::clamp(tp.p[3] * 2.0f + 0.1f, 0.1f, 4.0f);
                    int algo_idx = std::clamp(static_cast<int>(tp.p[4] * 31.99f), 0, 31);
                    int fb_amt = std::clamp(static_cast<int>(tp.p[5] * 7.99f), 0, 7);
                    float detune = std::clamp(tp.p[1] * 4.0f, 0.0f, 6.0f);
                    raw = v.dx7_voice.process(patch, bright, dec, static_cast<float>(sample_rate), algo_idx, fb_amt, detune);
                    if (!v.dx7_voice.isActive()) {
                        v.active = false;
                    }
                } else {
                    float cur_freq = v.freq;
                    if (tp.engine == EngineType::kAcid303 && v.is_legato) {
                        v.current_pitch = (1.0f - v.glide_coef) * v.target_pitch + v.glide_coef * v.current_pitch;
                        cur_freq = 440.0f * std::pow(2.0f, (v.current_pitch - 69.0f) / 12.0f);
                    }

                    const float dt = std::clamp(cur_freq * inv_sr, 0.0001f, 0.45f);
                    v.phase1 += dt;
                    if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;

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
                            float mod_idx = std::clamp(tp.p[1] + fm_macro, 0.0f, 1.0f) * 6.5f * f_env;
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
                        default:
                            raw = 0.0f;
                            break;
                    }
                }

                // Оновлюємо огинаючі фільтрів для поточного семплу
                v.lpf_env_val *= v.lpf_decay_coef;
                v.hpf_env_val *= v.hpf_decay_coef;

                // Dual filter 1: 2-pole High Pass Filter (SVF) with Bipolar Decay Envelope
                float eff_hpf = std::clamp(tp.hpf_cutoff + hpf_offset + (tp.hpf_env + hpf_env_offset) * v.hpf_env_val, 0.0f, 0.98f);
                if (eff_hpf > 0.005f) {
                    float f_hpf = std::clamp(2.0f * std::sin(3.14159265f * (20.0f * std::pow(800.0f, eff_hpf)) * inv_sr), 0.005f, 0.88f);
                    v.hpf_low += f_hpf * v.hpf_band;
                    float hpf_high = raw - v.hpf_low - hpf_q * v.hpf_band;
                    v.hpf_band += f_hpf * hpf_high;
                    v.hpf_band = std::clamp(v.hpf_band, -1.85f, 1.85f);
                    raw = hpf_high;
                }

                // Dual filter 2: 2-pole Low Pass Filter (SVF) with Bipolar Decay Envelope
                float base_cut = (v.plock_cutoff >= 0.0f) ? v.plock_cutoff : tp.lpf_cutoff;
                float eff_lpf = std::clamp(base_cut + cut_offset + (tp.lpf_env + env_offset) * v.lpf_env_val, 0.02f, 0.98f);
                float f_lpf = std::clamp(2.0f * std::sin(3.14159265f * (20.0f * std::pow(1000.0f, eff_lpf)) * inv_sr), 0.02f, 0.88f);
                v.svf_low += f_lpf * v.svf_band;
                float high = raw - v.svf_low - lpf_q * v.svf_band;
                v.svf_band += f_lpf * high;
                v.svf_band = std::clamp(v.svf_band, -1.85f, 1.85f);

                trk_sum += v.svf_low * amp * v.velocity;
            } // end vi loop

            // Per-track Model:Cycles / Model:Samples GAIN / OVERDRIVE curve
            float g = std::clamp(tp.gain_over + getTrackMod(t, ModTarget::kGainOver), -1.0f, 1.0f);
            float out_s = trk_sum;
            if (g <= 0.0f) {
                // Чисте плавне затухання до 0: 0.0 = 100% (0 dB), -1.0 = тиша
                float atten = std::clamp(1.0f + g, 0.0f, 1.0f);
                out_s = trk_sum * (atten * atten);
            } else {
                // Вище нуля: теплий синусоїдальний/гіперболічний овердрайв (guitar crunch)
                float drive = 1.0f + g * 5.0f;
                float sat = std::tanh(trk_sum * drive);
                float comp = 1.0f / std::sqrt(1.0f + g * 3.5f);
                out_s = sat * comp;
            }
            out_s *= trk_level;

            float s_l = out_s * pan_l;
            float s_r = out_s * pan_r;

            auto& fx_state = track_fx_[t];
            const auto& fx_p = tp.fx;

            // 1. Bitcrusher (Elektron style: bit reduction 16 down to 1 bit)
            if (fx_p.bit_depth < 16) {
                int b = std::clamp(fx_p.bit_depth, 1, 16);
                float steps = std::pow(2.0f, static_cast<float>(b)) - 1.0f;
                float half = steps * 0.5f;
                s_l = std::round(s_l * half) / half;
                s_r = std::round(s_r * half) / half;
            }

            // 2. SRR (Sample Rate Reduction: 1x down to 64x factor)
            if (fx_p.srr_factor > 1) {
                int factor = std::clamp(fx_p.srr_factor, 1, 64);
                if (fx_state.srr_step % factor == 0) {
                    fx_state.srr_l = s_l;
                    fx_state.srr_r = s_r;
                } else {
                    s_l = fx_state.srr_l;
                    s_r = fx_state.srr_r;
                }
                fx_state.srr_step++;
            }

            // 3. Distortion (Boss Metal Zone style: mid-frequency bite + hard square clip)
            if (fx_p.dist_mix > 0.001f && fx_p.dist_drive > 0.001f) {
                // Boss Metal Zone pre-emphasis mid-EQ (peaking around 1.0 - 1.6 kHz)
                float mid_f = 800.0f + fx_p.dist_tone * 1200.0f;
                float alpha_mid = std::clamp(2.0f * 3.14159265f * mid_f * inv_sr, 0.02f, 0.85f);
                fx_state.dist_bp_l += alpha_mid * (s_l - fx_state.dist_bp_l);
                fx_state.dist_bp_r += alpha_mid * (s_r - fx_state.dist_bp_r);
                float pre_l = s_l + fx_state.dist_bp_l * 2.8f;
                float pre_r = s_r + fx_state.dist_bp_r * 2.8f;

                // High-gain hard-clipping waveshaping
                float gain = 1.0f + fx_p.dist_drive * 19.0f;
                float hard_l = std::clamp(pre_l * gain, -1.0f, 1.0f);
                float hard_r = std::clamp(pre_r * gain, -1.0f, 1.0f);

                // Post-clip low-pass smoothing (guitar cabinet roll-off ~4.5 kHz)
                float alpha_post = 0.40f;
                fx_state.dist_lp_l += alpha_post * (hard_l - fx_state.dist_lp_l);
                fx_state.dist_lp_r += alpha_post * (hard_r - fx_state.dist_lp_r);

                float norm = 1.0f / std::sqrt(1.0f + fx_p.dist_drive * 5.0f);
                float wet_l = fx_state.dist_lp_l * norm;
                float wet_r = fx_state.dist_lp_r * norm;

                s_l = s_l * (1.0f - fx_p.dist_mix) + wet_l * fx_p.dist_mix;
                s_r = s_r * (1.0f - fx_p.dist_mix) + wet_r * fx_p.dist_mix;
            }

            // 4. Per-track Chorus
            if (fx_p.chorus_mix > 0.005f) {
                if (!fx_state.chorus_l.empty()) {
                    fx_state.chorus_l[fx_state.chorus_idx] = s_l;
                    fx_state.chorus_r[fx_state.chorus_idx] = s_r;
                    float lfo_l = (std::sin(fx_state.chorus_phase) + 1.0f) * 0.5f;
                    float lfo_r = (std::cos(fx_state.chorus_phase) + 1.0f) * 0.5f;
                    fx_state.chorus_phase += fx_p.chorus_rate * 6.2831853f * inv_sr;
                    if (fx_state.chorus_phase >= 6.2831853f) fx_state.chorus_phase -= 6.2831853f;

                    float mod_l = 80.0f + lfo_l * fx_p.chorus_depth * 140.0f;
                    float mod_r = 80.0f + lfo_r * fx_p.chorus_depth * 140.0f;
                    size_t c_idx_l = (fx_state.chorus_idx + fx_state.chorus_l.size() - static_cast<size_t>(mod_l)) % fx_state.chorus_l.size();
                    size_t c_idx_r = (fx_state.chorus_idx + fx_state.chorus_r.size() - static_cast<size_t>(mod_r)) % fx_state.chorus_r.size();

                    s_l = s_l * (1.0f - fx_p.chorus_mix * 0.5f) + fx_state.chorus_l[c_idx_l] * (fx_p.chorus_mix * 0.70f);
                    s_r = s_r * (1.0f - fx_p.chorus_mix * 0.5f) + fx_state.chorus_r[c_idx_r] * (fx_p.chorus_mix * 0.70f);
                    fx_state.chorus_idx = (fx_state.chorus_idx + 1) % fx_state.chorus_l.size();
                }
            }

            // Sidechain compressor lambda
            auto applySidechain = [&](float& sig_l, float& sig_r) {
                if (fx_p.sc_source > 0 && fx_p.sc_source <= kMaxTracks) {
                    int src_trk = fx_p.sc_source - 1;
                    float sc_sig = last_track_sample_[src_trk];
                    float sc_level = std::fabs(sc_sig);
                    float atk_c = std::exp(-1.0f / (std::clamp(fx_p.sc_attack, 0.001f, 0.2f) * static_cast<float>(sample_rate)));
                    float rel_c = std::exp(-1.0f / (std::clamp(fx_p.sc_release, 0.010f, 1.5f) * static_cast<float>(sample_rate)));
                    if (sc_level > fx_state.sc_env) {
                        fx_state.sc_env = sc_level + atk_c * (fx_state.sc_env - sc_level);
                    } else {
                        fx_state.sc_env = sc_level + rel_c * (fx_state.sc_env - sc_level);
                    }
                    float thresh_lin = std::pow(10.0f, fx_p.sc_thresh / 20.0f);
                    if (fx_state.sc_env > thresh_lin && thresh_lin > 0.0001f) {
                        float over_db = 20.0f * std::log10(fx_state.sc_env / thresh_lin);
                        float gr_db = over_db * (1.0f - 1.0f / std::max(1.0f, fx_p.sc_ratio));
                        fx_state.sc_gain = std::pow(10.0f, -gr_db / 20.0f);
                    } else {
                        fx_state.sc_gain = 1.0f;
                    }
                    sig_l *= fx_state.sc_gain;
                    sig_r *= fx_state.sc_gain;
                }
            };

            // Якщо компресор стоїть перед Del/Rev (PRE):
            if (fx_p.sc_pos == SidechainPos::kPreDelayRev) {
                applySidechain(s_l, s_r);
            }

            // 5. Parallel Delay & Reverb
            float wet_dly_l = 0.0f, wet_dly_r = 0.0f;
            float wet_rev_l = 0.0f, wet_rev_r = 0.0f;

            // Parallel Delay
            if (fx_p.delay_mix > 0.005f && !fx_state.delay_l.empty()) {
                float div_mult = kDelayDivMultiplier[std::clamp(fx_p.delay_div, 0, 7)];
                float cur_bpm = std::clamp(bpm_.load(std::memory_order_relaxed), 30.0f, 300.0f);
                uint32_t d_samples = static_cast<uint32_t>((60.0f / cur_bpm) * div_mult * sample_rate);
                d_samples = std::clamp(d_samples, 16u, static_cast<uint32_t>(fx_state.delay_l.size() - 1));

                size_t r_idx = (fx_state.delay_idx + fx_state.delay_l.size() - d_samples) % fx_state.delay_l.size();
                wet_dly_l = fx_state.delay_l[r_idx];
                wet_dly_r = fx_state.delay_r[r_idx];

                float fbk = std::clamp(fx_p.delay_feedback, 0.0f, 0.95f);
                float w = std::clamp(fx_p.delay_width, -1.0f, 1.0f);
                float abs_w = std::abs(w);
                float fb_l = (1.0f - abs_w) * wet_dly_l + abs_w * wet_dly_r;
                float fb_r = (1.0f - abs_w) * wet_dly_r + abs_w * wet_dly_l;
                float in_l = (w >= 0.0f) ? s_l : s_l * (1.0f - abs_w);
                float in_r = (w <= 0.0f) ? s_r : s_r * (1.0f - abs_w);

                fx_state.delay_l[fx_state.delay_idx] = in_l + fb_l * fbk;
                fx_state.delay_r[fx_state.delay_idx] = in_r + fb_r * fbk;
                fx_state.delay_idx = (fx_state.delay_idx + 1) % fx_state.delay_l.size();

                float mono_tap = 0.5f * (wet_dly_l + wet_dly_r);
                if (w >= 0.0f) {
                    wet_dly_l = (1.0f - abs_w) * mono_tap + abs_w * wet_dly_l;
                    wet_dly_r = (1.0f - abs_w) * mono_tap + abs_w * wet_dly_r;
                } else {
                    wet_dly_l = (1.0f - abs_w) * mono_tap + abs_w * wet_dly_r;
                    wet_dly_r = (1.0f - abs_w) * mono_tap + abs_w * wet_dly_l;
                }
            }

            // Parallel Reverb
            if (fx_p.rev_mix > 0.005f && !fx_state.rev_pre_l.empty()) {
                // Pre-delay
                uint32_t pre_samples = static_cast<uint32_t>(std::clamp(fx_p.rev_predelay, 0.0f, 0.15f) * sample_rate);
                pre_samples = std::clamp(pre_samples, 1u, static_cast<uint32_t>(fx_state.rev_pre_l.size() - 1));
                size_t p_r_idx = (fx_state.rev_pre_idx + fx_state.rev_pre_l.size() - pre_samples) % fx_state.rev_pre_l.size();
                float pre_in_l = fx_state.rev_pre_l[p_r_idx];
                float pre_in_r = fx_state.rev_pre_r[p_r_idx];
                fx_state.rev_pre_l[fx_state.rev_pre_idx] = s_l;
                fx_state.rev_pre_r[fx_state.rev_pre_idx] = s_r;
                fx_state.rev_pre_idx = (fx_state.rev_pre_idx + 1) % fx_state.rev_pre_l.size();

                float rev_in = (pre_in_l + pre_in_r) * 0.5f;

                // Engine-specific tuning
                float fbk_scale = 1.0f;
                float damp_bias = 0.0f;
                switch (fx_p.rev_engine) {
                    case ReverbEngine::kPlate: fbk_scale = 0.90f; damp_bias = 0.15f; break;
                    case ReverbEngine::kHall:  fbk_scale = 0.95f; damp_bias = 0.35f; break;
                    case ReverbEngine::kRoom:  fbk_scale = 0.75f; damp_bias = 0.50f; break;
                    case ReverbEngine::kDark:  fbk_scale = 0.92f; damp_bias = 0.70f; break;
                    default: break;
                }

                float fbk = std::clamp(fx_p.rev_decay * fbk_scale, 0.1f, 0.96f);
                float damp = std::clamp((1.0f - fx_p.rev_hicut) + damp_bias, 0.05f, 0.95f);

                float c_out_l = 0.0f, c_out_r = 0.0f;
                for (int ci = 0; ci < 4; ++ci) {
                    c_out_l += fx_state.comb_l[ci].process(rev_in, fbk, damp);
                    c_out_r += fx_state.comb_r[ci].process(rev_in, fbk, damp);
                }
                c_out_l *= 0.25f;
                c_out_r *= 0.25f;

                for (int ai = 0; ai < 2; ++ai) {
                    c_out_l = fx_state.allpass_l[ai].process(c_out_l);
                    c_out_r = fx_state.allpass_r[ai].process(c_out_r);
                }

                // Low-cut filter on reverb output
                float alpha_locut = std::clamp(2.0f * 3.14159265f * (30.0f + fx_p.rev_locut * 500.0f) * inv_sr, 0.005f, 0.40f);
                fx_state.rev_hp_l += alpha_locut * (c_out_l - fx_state.rev_hp_l);
                fx_state.rev_hp_r += alpha_locut * (c_out_r - fx_state.rev_hp_r);
                wet_rev_l = c_out_l - fx_state.rev_hp_l;
                wet_rev_r = c_out_r - fx_state.rev_hp_r;
            }

            // Sum parallel wet outputs
            s_l += wet_dly_l * fx_p.delay_mix + wet_rev_l * fx_p.rev_mix;
            s_r += wet_dly_r * fx_p.delay_mix + wet_rev_r * fx_p.rev_mix;

            // Якщо компресор стоїть після Del/Rev (POST):
            if (fx_p.sc_pos == SidechainPos::kPostDelayRev) {
                applySidechain(s_l, s_r);
            }

            last_track_sample_[t] = (s_l + s_r) * 0.5f;

            float abs_s = std::max(std::fabs(s_l), std::fabs(s_r));
            if (abs_s > trk_peak[t]) trk_peak[t] = abs_s;

            mix_l += s_l;
            mix_r += s_r;
            send_l += s_l * d_send;
            send_r += s_r * d_send;
            rev_l  += s_l * r_send;
            rev_r  += s_r * r_send;
        }

        // Стерео Хорус (LFO-модульована затримка 7-18мс)
        if (chr_mix > 0.01f) {
            chorus_buf_l_[chorus_idx_] = mix_l;
            chorus_buf_r_[chorus_idx_] = mix_r;
            float lfo_l = (std::sin(chorus_phase_) + 1.0f) * 0.5f;
            float lfo_r = (std::cos(chorus_phase_) + 1.0f) * 0.5f;
            chorus_phase_ += chr_rate * 6.2831853f * inv_sr;
            if (chorus_phase_ >= 6.2831853f) chorus_phase_ -= 6.2831853f;

            float delay_samples_l = 160.0f + lfo_l * chr_depth * 220.0f;
            float delay_samples_r = 160.0f + lfo_r * chr_depth * 220.0f;
            uint32_t c_idx_l = (chorus_idx_ + chorus_buf_l_.size() - static_cast<uint32_t>(delay_samples_l)) % chorus_buf_l_.size();
            uint32_t c_idx_r = (chorus_idx_ + chorus_buf_r_.size() - static_cast<uint32_t>(delay_samples_r)) % chorus_buf_r_.size();

            mix_l = mix_l * (1.0f - chr_mix * 0.5f) + chorus_buf_l_[c_idx_l] * chr_mix * 0.70f;
            mix_r = mix_r * (1.0f - chr_mix * 0.5f) + chorus_buf_r_[c_idx_r] * chr_mix * 0.70f;
            chorus_idx_ = (chorus_idx_ + 1) % chorus_buf_l_.size();
        }

        // Tanh Overdrive / Сатуратор із фільтром тембру
        if (drv_amt > 0.01f) {
            float drive_gain = 1.0f + drv_amt * 7.0f;
            float sat_l = std::tanh(mix_l * drive_gain);
            float sat_r = std::tanh(mix_r * drive_gain);
            float alpha = std::clamp(drv_tone * 0.85f + 0.10f, 0.05f, 0.95f);
            drive_lp_l_ += alpha * (sat_l - drive_lp_l_);
            drive_lp_r_ += alpha * (sat_r - drive_lp_r_);
            float norm_gain = 1.0f / std::sqrt(drive_gain);
            mix_l = (drive_lp_l_ * norm_gain) * drv_amt + mix_l * (1.0f - drv_amt);
            mix_r = (drive_lp_r_ * norm_gain) * drv_amt + mix_r * (1.0f - drv_amt);
        }

        // Майстер стерео пінг-понг ділей з регулюванням ширини стерео
        uint32_t read_idx = (delay_idx_ + delay_buf_l_.size() - delay_len) % delay_buf_l_.size();
        float d_l = delay_buf_l_[read_idx];
        float d_r = delay_buf_r_[read_idx];

        float w = std::clamp(params_.delay_width.load(std::memory_order_relaxed), -1.0f, 1.0f);
        float abs_w = std::abs(w);
        float fb_l = (1.0f - abs_w) * d_l + abs_w * d_r;
        float fb_r = (1.0f - abs_w) * d_r + abs_w * d_l;
        float in_l = (w >= 0.0f) ? send_l : send_l * (1.0f - abs_w);
        float in_r = (w <= 0.0f) ? send_r : send_r * (1.0f - abs_w);

        delay_buf_l_[delay_idx_] = std::clamp(in_l + fb_l * dly_fb, -0.95f, 0.95f);
        delay_buf_r_[delay_idx_] = std::clamp(in_r + fb_r * dly_fb, -0.95f, 0.95f);
        delay_idx_ = (delay_idx_ + 1) % delay_buf_l_.size();

        float mono_tap = 0.5f * (d_l + d_r);
        float out_d_l = (w >= 0.0f) ? ((1.0f - abs_w) * mono_tap + abs_w * d_l) : ((1.0f - abs_w) * mono_tap + abs_w * d_r);
        float out_d_r = (w >= 0.0f) ? ((1.0f - abs_w) * mono_tap + abs_w * d_r) : ((1.0f - abs_w) * mono_tap + abs_w * d_l);

        // Справжня алгоритмічна стерео реверберація Freeverb (8 Comb + 4 Allpass)
        float room_size = std::clamp(rev_dec * 0.28f + 0.70f, 0.0f, 0.98f);
        float damp = 0.40f;
        float out_comb_l = 0.0f;
        float out_comb_r = 0.0f;
        for (int k = 0; k < kNumCombs; ++k) {
            out_comb_l += comb_l_[k].process(rev_l, room_size, damp);
            out_comb_r += comb_r_[k].process(rev_r, room_size, damp);
        }
        for (int k = 0; k < kNumAllpasses; ++k) {
            out_comb_l = allpass_l_[k].process(out_comb_l);
            out_comb_r = allpass_r_[k].process(out_comb_r);
        }
        float rv_l = out_comb_l * 0.18f;
        float rv_r = out_comb_r * 0.18f;

        float m_l = mix_l + out_d_l * dly_mix + rv_l * rev_mix;
        float m_r = mix_r + out_d_r * dly_mix + rv_r * rev_mix;

        // 1. Master Output GAIN / OVER
        float mg = std::clamp(local_mfx.gain_over, -1.0f, 1.0f);
        if (mg <= 0.0f) {
            float atten = std::clamp(1.0f + mg, 0.0f, 1.0f);
            m_l *= (atten * atten);
            m_r *= (atten * atten);
        } else {
            float drive = 1.0f + mg * 4.0f;
            float comp = 1.0f / std::sqrt(1.0f + mg * 3.0f);
            m_l = std::tanh(m_l * drive) * comp;
            m_r = std::tanh(m_r * drive) * comp;
        }

        // 2. Master Sidechain Compressor
        float sc_in = (local_mfx.sc_source == 0)
                          ? (std::fabs(m_l) + std::fabs(m_r)) * 0.5f
                          : std::fabs(last_track_sample_[std::clamp(local_mfx.sc_source - 1, 0, kMaxTracks - 1)]);
        float m_atk = std::exp(-1.0f / (std::clamp(local_mfx.sc_attack, 0.001f, 0.100f) * static_cast<float>(sample_rate)));
        float m_rel = std::exp(-1.0f / (std::clamp(local_mfx.sc_release, 0.020f, 1.000f) * static_cast<float>(sample_rate)));
        if (sc_in > mst_sc_env_) {
            mst_sc_env_ = sc_in + m_atk * (mst_sc_env_ - sc_in);
        } else {
            mst_sc_env_ = sc_in + m_rel * (mst_sc_env_ - sc_in);
        }
        float m_thresh_lin = std::pow(10.0f, local_mfx.sc_thresh / 20.0f);
        if (local_mfx.sc_thresh < -0.1f && mst_sc_env_ > m_thresh_lin && m_thresh_lin > 0.0001f) {
            float over_db = 20.0f * std::log10(mst_sc_env_ / m_thresh_lin);
            float gr_db = over_db * (1.0f - 1.0f / std::max(1.0f, local_mfx.sc_ratio));
            mst_sc_gain_ = std::pow(10.0f, -gr_db / 20.0f);
        } else {
            mst_sc_gain_ = 1.0f;
        }
        m_l *= mst_sc_gain_;
        m_r *= mst_sc_gain_;

        // 3. Master HPF (12 dB 2-pole or 24 dB 4-pole)
        if (local_mfx.hpf_cutoff > 0.005f) {
            float f_hpf = std::clamp(2.0f * std::sin(3.14159265f * (20.0f * std::pow(800.0f, local_mfx.hpf_cutoff)) * inv_sr), 0.005f, 0.88f);
            // Stage 1 (12 dB/oct)
            mst_hpf1_low_l_ += f_hpf * mst_hpf1_band_l_;
            float high1_l = m_l - mst_hpf1_low_l_ - 1.414f * mst_hpf1_band_l_;
            mst_hpf1_band_l_ += f_hpf * high1_l;
            m_l = high1_l;

            mst_hpf1_low_r_ += f_hpf * mst_hpf1_band_r_;
            float high1_r = m_r - mst_hpf1_low_r_ - 1.414f * mst_hpf1_band_r_;
            mst_hpf1_band_r_ += f_hpf * high1_r;
            m_r = high1_r;

            // Stage 2 (24 dB/oct if enabled)
            if (local_mfx.hpf_slope_24) {
                mst_hpf2_low_l_ += f_hpf * mst_hpf2_band_l_;
                float high2_l = m_l - mst_hpf2_low_l_ - 1.414f * mst_hpf2_band_l_;
                mst_hpf2_band_l_ += f_hpf * high2_l;
                m_l = high2_l;

                mst_hpf2_low_r_ += f_hpf * mst_hpf2_band_r_;
                float high2_r = m_r - mst_hpf2_low_r_ - 1.414f * mst_hpf2_band_r_;
                mst_hpf2_band_r_ += f_hpf * high2_r;
                m_r = high2_r;
            }
        }

        // 4. Master Parametric Bell EQ Filter
        if (std::fabs(local_mfx.bell_gain) > 0.05f) {
            float f0 = std::clamp(40.0f * std::pow(400.0f, local_mfx.bell_freq), 20.0f, 18000.0f);
            float gain_lin = std::pow(10.0f, local_mfx.bell_gain / 40.0f);
            float w0 = 2.0f * 3.14159265f * f0 * inv_sr;
            float alpha = std::sin(w0) / (2.0f * std::clamp(local_mfx.bell_q, 0.2f, 10.0f));
            float cosw0 = std::cos(w0);

            float b0 = 1.0f + alpha * gain_lin;
            float b1 = -2.0f * cosw0;
            float b2 = 1.0f - alpha * gain_lin;
            float a0 = 1.0f + alpha / gain_lin;
            float a1 = -2.0f * cosw0;
            float a2 = 1.0f - alpha / gain_lin;

            float inv_a0 = 1.0f / a0;
            float b0_n = b0 * inv_a0, b1_n = b1 * inv_a0, b2_n = b2 * inv_a0;
            float a1_n = a1 * inv_a0, a2_n = a2 * inv_a0;

            float y_l = b0_n * m_l + mst_bell_x1_l_;
            mst_bell_x1_l_ = b1_n * m_l - a1_n * y_l + mst_bell_x2_l_;
            mst_bell_x2_l_ = b2_n * m_l - a2_n * y_l;
            m_l = y_l;

            float y_r = b0_n * m_r + mst_bell_x1_r_;
            mst_bell_x1_r_ = b1_n * m_r - a1_n * y_r + mst_bell_x2_r_;
            mst_bell_x2_r_ = b2_n * m_r - a2_n * y_r;
            m_r = y_r;
        }

        // 5. Master LPF (12 dB 2-pole or 24 dB 4-pole)
        if (local_mfx.lpf_cutoff < 0.995f) {
            float f_lpf = std::clamp(2.0f * std::sin(3.14159265f * (20.0f * std::pow(1000.0f, local_mfx.lpf_cutoff)) * inv_sr), 0.02f, 0.88f);
            // Stage 1 (12 dB/oct)
            mst_lpf1_low_l_ += f_lpf * mst_lpf1_band_l_;
            float high1_l = m_l - mst_lpf1_low_l_ - 1.414f * mst_lpf1_band_l_;
            mst_lpf1_band_l_ += f_lpf * high1_l;
            m_l = mst_lpf1_low_l_;

            mst_lpf1_low_r_ += f_lpf * mst_lpf1_band_r_;
            float high1_r = m_r - mst_lpf1_low_r_ - 1.414f * mst_lpf1_band_r_;
            mst_lpf1_band_r_ += f_lpf * high1_r;
            m_r = mst_lpf1_low_r_;

            // Stage 2 (24 dB/oct if enabled)
            if (local_mfx.lpf_slope_24) {
                mst_lpf2_low_l_ += f_lpf * mst_lpf2_band_l_;
                float high2_l = m_l - mst_lpf2_low_l_ - 1.414f * mst_lpf2_band_l_;
                mst_lpf2_band_l_ += f_lpf * high2_l;
                m_l = mst_lpf2_low_l_;

                mst_lpf2_low_r_ += f_lpf * mst_lpf2_band_r_;
                float high2_r = m_r - mst_lpf2_low_r_ - 1.414f * mst_lpf2_band_r_;
                mst_lpf2_band_r_ += f_lpf * high2_r;
                m_r = mst_lpf2_low_r_;
            }
        }

        mix_l = std::clamp(m_l * master_vol, -0.98f, 0.98f);
        mix_r = std::clamp(m_r * master_vol, -0.98f, 0.98f);

        max_l = std::max(max_l, std::fabs(mix_l));
        max_r = std::max(max_r, std::fabs(mix_r));
        max_dly_send = std::max(max_dly_send, (std::fabs(send_l) + std::fabs(send_r)) * 0.5f);
        max_rev_send = std::max(max_rev_send, (std::fabs(rev_l) + std::fabs(rev_r)) * 0.5f);
        max_chr_send = std::max(max_chr_send, chr_mix * (std::fabs(mix_l) + std::fabs(mix_r)) * 0.5f);

        if ((i & 1u) == 0u) {
            scope_ring_[sc_idx & 255u].store((mix_l + mix_r) * 0.5f, std::memory_order_relaxed);
            ++sc_idx;
        }

        out_interleaved[i * 2]     = static_cast<int16_t>(mix_l * 32000.0f);
        out_interleaved[i * 2 + 1] = static_cast<int16_t>(mix_r * 32000.0f);
    }

    net_streamer_.pushAudioBlock(out_interleaved, frames);

    scope_write_idx_.store(sc_idx & 255u, std::memory_order_relaxed);

    float prev_l = vu_l_.load(std::memory_order_relaxed);
    float prev_r = vu_r_.load(std::memory_order_relaxed);
    float next_l = std::max(max_l, prev_l * 0.88f);
    float next_r = std::max(max_r, prev_r * 0.88f);
    vu_l_.store(next_l, std::memory_order_relaxed);
    vu_r_.store(next_r, std::memory_order_relaxed);
    peak_hold_.store(std::max({next_l, next_r, peak_hold_.load(std::memory_order_relaxed) * 0.96f}), std::memory_order_relaxed);

    send_delay_peak_.store(std::max(max_dly_send, send_delay_peak_.load(std::memory_order_relaxed) * 0.88f), std::memory_order_relaxed);
    send_reverb_peak_.store(std::max(max_rev_send, send_reverb_peak_.load(std::memory_order_relaxed) * 0.88f), std::memory_order_relaxed);
    send_chorus_peak_.store(std::max(max_chr_send, send_chorus_peak_.load(std::memory_order_relaxed) * 0.88f), std::memory_order_relaxed);

    for (int t = 0; t < kMaxTracks; ++t) {
        float prev_t = track_vu_[t].load(std::memory_order_relaxed);
        track_vu_[t].store(std::max(trk_peak[t], prev_t * 0.86f), std::memory_order_relaxed);
    }
}

#if defined(__linux__)
void AudioEngine::alsaThreadLoop() {
    // 1. Жорстко ізолюємо аудіопотік на окремому ядрі CPU 3 (усуває конкуренцію з перериваннями dwc2 на CPU 0)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // 2. Піднімаємо пріоритет до найвищого Real-Time (SCHED_FIFO 85),
    //    щоб аудіотракт гарантовано не переривався жодними іншими процесами!
    {
        struct sched_param sp{};
        sp.sched_priority = 85;
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    }

    snd_pcm_t* pcm = nullptr;
    snd_pcm_format_t active_fmt = SND_PCM_FORMAT_S16_LE;

    int card = current_card_.load();
    const bool use_usb = (card > 0) && !force_internal_.load(std::memory_order_relaxed);
    const uint32_t rate = active_sample_rate_.load();
    const uint32_t block_frames = target_period_frames_.load(std::memory_order_relaxed);
    active_period_frames_.store(block_frames, std::memory_order_relaxed);

    // Буфер ALSA: щонайменше 6 періодів для USB (для компенсації джитеру dwc2) або 4 для внутрішнього ЦАП
    const unsigned int latency_us = use_usb
        ? std::max(64000u, static_cast<unsigned int>((block_frames * 6 * 1000000ULL) / rate))
        : std::max(16000u, static_cast<unsigned int>((block_frames * 4 * 1000000ULL) / rate));

    // 1. Якщо підключено зовнішній USB Audio інтерфейс і не ввімкнено оверрайд FORCE INTERNAL,
    //    відкриваємо ВИКЛЮЧНО його (щоб не було конфлікту двох різних кварцових генераторів USB vs I2S RK817!)
    if (use_usb) {
        char dev_names[2][32];
        std::snprintf(dev_names[0], sizeof(dev_names[0]), "hw:%d,0", card);
        std::snprintf(dev_names[1], sizeof(dev_names[1]), "plughw:%d,0", card);

        for (const auto& dev_str : dev_names) {
            int err = snd_pcm_open(&pcm, dev_str, SND_PCM_STREAM_PLAYBACK, 0);
            if (err >= 0) {
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
                            0,
                            latency_us
                        ) >= 0) {
                        active_fmt = fmt;
                        ok = true;
                        std::printf("[ALSA] Opened EXCLUSIVE USB %s at %u Hz stereo (fmt=%d, latency=%u us, soft_resample=0)\n",
                                    dev_str, rate, static_cast<int>(fmt), latency_us);
                        break;
                    }
                }
                if (ok) break;
                snd_pcm_close(pcm);
                pcm = nullptr;
            }
        }
        if (!pcm) {
            std::fprintf(stderr, "[ALSA] Failed to open USB card %d (%u Hz)\n", card, rate);
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

    std::vector<int16_t> buf16(block_frames * 2, 0);
    std::vector<int32_t> buf32(block_frames * 2, 0);
    std::vector<uint8_t> buf24_3(block_frames * 2 * 3, 0);

    auto writeBlock = [&](const int16_t* in_samples) -> snd_pcm_sframes_t {
        if (!pcm) return 0;
        if (active_fmt == SND_PCM_FORMAT_S32_LE) {
            for (size_t i = 0; i < buf16.size(); ++i) {
                buf32[i] = static_cast<int32_t>(in_samples[i]) << 16;
            }
            return snd_pcm_writei(pcm, buf32.data(), block_frames);
        } else if (active_fmt == SND_PCM_FORMAT_S24_LE) {
            for (size_t i = 0; i < buf16.size(); ++i) {
                buf32[i] = static_cast<int32_t>(in_samples[i]) << 8;
            }
            return snd_pcm_writei(pcm, buf32.data(), block_frames);
        } else if (active_fmt == SND_PCM_FORMAT_S24_3LE) {
            for (size_t i = 0; i < buf16.size(); ++i) {
                int32_t v = static_cast<int32_t>(in_samples[i]) << 8;
                buf24_3[i * 3 + 0] = static_cast<uint8_t>(v & 0xFF);
                buf24_3[i * 3 + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
                buf24_3[i * 3 + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
            }
            return snd_pcm_writei(pcm, buf24_3.data(), block_frames);
        } else {
            return snd_pcm_writei(pcm, in_samples, block_frames);
        }
    };

    // Pre-roll: заповнюємо апаратний буфер ALSA 4 періодами
    if (pcm) {
        for (int p = 0; p < 4; ++p) {
            renderBlock(buf16.data(), block_frames, rate);
            writeBlock(buf16.data());
        }
    }

    while (running_.load(std::memory_order_relaxed)) {
        renderBlock(buf16.data(), block_frames, rate);

        if (pcm) {
            snd_pcm_sframes_t written = writeBlock(buf16.data());
            if (written < 0) {
                xrun_count_.fetch_add(1, std::memory_order_relaxed);
                std::fprintf(stderr, "[ALSA] Write error %ld: %s, dropping DMA and recovering...\n",
                             written, snd_strerror(static_cast<int>(written)));
                snd_pcm_drop(pcm);
                int prep_err = snd_pcm_prepare(pcm);
                if (prep_err < 0) {
                    snd_pcm_recover(pcm, prep_err, 1);
                }
                // Pre-roll після відновлення, щоб запобігти повторному спустошенню
                for (int p = 0; p < 2; ++p) {
                    renderBlock(buf16.data(), block_frames, rate);
                    writeBlock(buf16.data());
                }
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
    t.xrun_count = xrun_count_.load(std::memory_order_relaxed);
    const bool forced_int = force_internal_.load(std::memory_order_relaxed);
    if (!forced_int && current_usb_48k_.load(std::memory_order_relaxed)) {
        t.device_label = "OUT: USB (" + std::to_string(t.sample_rate) + " Hz)";
    } else {
        const auto& prof = activeProfile();
        std::string tag = forced_int ? "OUT: [INT-FORCE] " : "OUT: ";
        t.device_label = tag + prof.name + (current_hp_.load(std::memory_order_relaxed) ? " HP (" : " SPK (")
                         + std::to_string(t.sample_rate) + " Hz)";
    }
    t.vu_left = vu_l_.load(std::memory_order_relaxed);
    t.vu_right = vu_r_.load(std::memory_order_relaxed);
    t.peak_hold = peak_hold_.load(std::memory_order_relaxed);

    t.active_tracks = active_rows_count_;
    for (int i = 0; i < kMaxTracks; ++i) {
        t.track_levels[i] = track_vu_[i].load(std::memory_order_relaxed);
        t.track_names[i] = tracks_[i].name[0] ? std::string(tracks_[i].name) : ("TRK " + std::to_string(i + 1));
    }

    uint32_t r_idx = scope_write_idx_.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < 256; ++i) {
        t.scope_samples[i] = scope_ring_[(r_idx + i) & 255u].load(std::memory_order_relaxed);
    }
    t.send_delay = send_delay_peak_.load(std::memory_order_relaxed);
    t.send_reverb = send_reverb_peak_.load(std::memory_order_relaxed);
    t.send_chorus = send_chorus_peak_.load(std::memory_order_relaxed);
    return t;
}

float AudioEngine::getLiveModOffset(int track, ModTarget target) const {
    if (track < 0 || track >= kMaxTracks) return 0.0f;
    size_t idx = static_cast<size_t>(target);
    if (idx >= static_cast<size_t>(ModTarget::kCount)) return 0.0f;
    return live_mod_offsets_[track][idx];
}

float AudioEngine::getLiveModValue(int track, ModTarget target, float base_val) const {
    return std::clamp(base_val + getLiveModOffset(track, target), 0.0f, 1.0f);
}

} // namespace groovebox
