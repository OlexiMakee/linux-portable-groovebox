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
      reverb_buf_l_(16384, 0.0f),
      reverb_buf_r_(16384, 0.0f),
      chorus_buf_l_(2048, 0.0f),
      chorus_buf_r_(2048, 0.0f) {
    for (auto& s : scope_ring_) s.store(0.0f, std::memory_order_relaxed);
    for (auto& t : track_vu_) t.store(0.0f, std::memory_order_relaxed);
    initDefaultPattern();
    net_streamer_.start(8000, 32000);
}

AudioEngine::~AudioEngine() {
    stop();
    net_streamer_.stop();
}

void AudioEngine::initDefaultPattern() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    // За замовчуванням 1 інструмент = 1 доріжка:
    // T1: SAMPLE ONESHOT (Kick 808)
    // T2: ACID 303 BASSLINE (Legato Slide, Accent)
    // T3: YAMAHA DX7 6-OP (ROM1A E.PIANO 1 / BASS 1 / LEAD)
    // T4: CHORD PAD (4-Voice Polyphonic Detuned Pad)
    tracks_[0] = {EngineType::kSampleOneShot, {0.00f, 0.50f, 0.40f, 0.85f, 0.15f, 0.15f}, 0.002f, 0.35f, 0.00f, 0.05f, 0.90f,  0.00f, 0.10f, false};
    tracks_[1] = {EngineType::kAcid303,       {0.62f, 0.55f, 0.60f, 0.22f, 0.70f, 0.00f}, 0.005f, 0.28f, 0.00f, 0.05f, 0.85f, -0.15f, 0.20f, false};
    tracks_[2] = {EngineType::kDx7,           {0.32f, 0.75f, 0.60f, 0.40f, 0.00f, 0.30f}, 0.005f, 0.45f, 0.00f, 0.05f, 0.85f,  0.15f, 0.25f, false};
    tracks_[3] = {EngineType::kChordPad,      {0.55f, 0.35f, 0.00f, 0.45f, 0.25f, 0.40f}, 0.010f, 0.45f, 0.00f, 0.05f, 0.75f, -0.20f, 0.30f, false};

    pattern_length_.store(64);
    active_rows_count_ = 3; // Рівно 3 доріжки за замовчуванням: Kick, 303, DX7

    // Мапінг 1:1 — кожен рядок відповідає своєму інструменту
    for (int i = 0; i < kTotalRows; ++i) {
        row_track_[i] = std::min(i, 3);
        row_voice_[i] = 0;
    }

    // Дефолтна довжина для всіх рядків: 16 кроків, усі кроки чисті та порожні
    for (int r = 0; r < kTotalRows; ++r) {
        row_lengths_[r].store(16, std::memory_order_relaxed);
        for (uint32_t s = 0; s < kMaxSteps; ++s) {
            uint8_t def_note = 48;
            if (r == 0) def_note = 36;      // Kick C1
            else if (r == 1) def_note = 36; // 303 C1
            else if (r == 2) def_note = 60; // DX7 C3
            else def_note = 48;
            pattern_[r][s] = {false, false, false, def_note, 0.90f, 0.60f, 100, TrigCondition::kNone, false, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
        }
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
        pattern_[r] = pattern_[r + 1];
        row_lengths_[r].store(row_lengths_[r + 1].load(std::memory_order_relaxed), std::memory_order_relaxed);
        row_track_[r] = row_track_[r + 1];
        row_voice_[r] = row_voice_[r + 1];
    }
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
        int t = getRowTrack(r);
        int v = getRowVoice(r);
        if (t < 4 && tracks_[t].engine == EngineType::kDrumSyn) {
            static constexpr uint8_t kDrumNotes[4] = {36, 38, 42, 46};
            note = kDrumNotes[std::clamp(v, 0, 3)];
        } else {
            note = 48 + (r % 4) * 4;
        }
    }
    float vel = st.enabled ? st.velocity : 0.85f;
    float cut = st.has_plock ? st.plock_cutoff : -1.0f;
    triggerVoice(r, note, vel, cut, st.slide, st.gate_len);
}

void AudioEngine::randomizeRow(int row) {
    int r = std::clamp(row, 0, kTotalRows - 1);
    int t = getRowTrack(r);
    int v = getRowVoice(r);
    uint32_t len = getRowLength(r);
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto randByte = [this]() -> uint32_t {
        rng_state_ = rng_state_ * 1664525u + 1013904223u;
        return (rng_state_ >> 16) & 0xFF;
    };

    bool is_drum = (t < 4 && tracks_[t].engine == EngineType::kDrumSyn);

    for (uint32_t s = 0; s < len; ++s) {
        uint32_t rnd = randByte();
        bool trig = false;
        uint8_t note = 48;
        float vel = 0.80f + (randByte() % 20) * 0.01f;
        float gate = 0.50f + (randByte() % 30) * 0.01f;

        if (is_drum) {
            static constexpr uint8_t kDrumNotes[4] = {36, 38, 42, 46};
            note = kDrumNotes[std::clamp(v, 0, 3)];
            if (v == 0) { // BD
                trig = (s % 4 == 0) || ((s % 16 == 10 || s % 16 == 14) && (rnd < 90));
            } else if (v == 1) { // SD
                trig = (s % 8 == 4) || ((s % 8 == 7 || s % 8 == 2) && (rnd < 70));
            } else if (v == 2) { // CH
                trig = (s % 2 == 0) || (rnd < 90);
                vel = (s % 4 == 0) ? 0.90f : 0.65f;
            } else { // OH
                trig = (s % 4 == 2) && (rnd < 150);
            }
        } else {
            // Мелодійний синт: пентатоніка
            static constexpr uint8_t kScale[8] = {36, 39, 41, 43, 46, 48, 51, 53};
            trig = (rnd < 110);
            note = kScale[rnd % 8];
        }

        pattern_[r][s].enabled = trig;
        pattern_[r][s].is_param_trig = false;
        pattern_[r][s].slide = !is_drum && (randByte() < 40);
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
    st.enabled = true;
    st.is_param_trig = false;
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
        v.gate_samples_left = -1;
        v.is_legato = false;
    }
    std::fill(delay_buf_l_.begin(), delay_buf_l_.end(), 0.0f);
    std::fill(delay_buf_r_.begin(), delay_buf_r_.end(), 0.0f);
    vu_l_.store(0.0f, std::memory_order_relaxed);
    vu_r_.store(0.0f, std::memory_order_relaxed);
    peak_hold_.store(0.0f, std::memory_order_relaxed);
    for (auto& t : track_vu_) t.store(0.0f, std::memory_order_relaxed);
}

uint32_t AudioEngine::getRowLength(int row) const {
    int r = std::clamp(row, 0, kTotalRows - 1);
    return row_lengths_[r].load(std::memory_order_relaxed);
}

void AudioEngine::setRowLength(int row, uint32_t len) {
    int r = std::clamp(row, 0, kTotalRows - 1);
    row_lengths_[r].store(std::clamp(len, 1u, 128u), std::memory_order_relaxed);
}

uint32_t AudioEngine::cycleRowLength(int row) {
    int r = std::clamp(row, 0, kTotalRows - 1);
    uint32_t cur = row_lengths_[r].load(std::memory_order_relaxed);
    uint32_t next = (cur >= 128u) ? 4u : std::min(128u, ((cur / 4u) + 1u) * 4u);
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
    if (row < 0 || row >= kTotalRows) return;
    std::lock_guard<std::mutex> lock(state_mutex_);
    int t = row_track_[row];
    int v_idx = row_voice_[row];
    if (tracks_[t].muted) return;

    if (tracks_[t].engine == EngineType::kMidiOut) {
        int ch = std::clamp(static_cast<int>(tracks_[t].p[0] * 15.99f), 0, 15);
        if (midi_out_cb_) {
            midi_out_cb_(ch, midi_note, std::clamp(velocity, 0.05f, 1.0f), true);
        }
        voices_[row].active = true;
        voices_[row].note_num = static_cast<uint8_t>(midi_note);
        voices_[row].gate_samples_left = static_cast<int32_t>(0.125f * std::clamp(gate_len, 0.1f, 2.0f) * static_cast<float>(active_sample_rate_.load()));
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

    // Тривалість 1 кроку секвенсора (~125мс на 120 BPM)
    float step_sec = 0.125f;
    v.gate_samples_left = static_cast<int32_t>(step_sec * std::clamp(gate_len, 0.1f, 2.0f) * sr);

    if (tracks_[t].engine == EngineType::kAcid303 && slide && v.amp_adsr.isActive()) {
        // Roland TB-303 Legato Slide: нова цільова нота, без скидання фази чи огинаючої!
        v.target_pitch = static_cast<float>(midi_note);
        v.is_legato = true;
    } else {
        v.target_pitch = static_cast<float>(midi_note);
        v.current_pitch = v.target_pitch;
        v.is_legato = false;
        v.freq = midiToFreq(midi_note);

        if (tracks_[t].engine == EngineType::kDrumRack) {
            static constexpr int kDrumNotes[4] = {36, 38, 42, 46};
            int d_note = (midi_note > 0) ? midi_note : kDrumNotes[v_idx];
            v.freq = midiToFreq(d_note);
            // Визначаємо слот барабанів (0=BD, 1=SD, 2=CH, 3=OH, 4=CP, 5=Rim, 6=Tom, 7=Perc)
            if (d_note == 36) v.drum_slot = 0;
            else if (d_note == 38 || d_note == 40) v.drum_slot = 1;
            else if (d_note == 42 || d_note == 44) v.drum_slot = 2;
            else if (d_note == 46) v.drum_slot = 3;
            else if (d_note == 39) v.drum_slot = 4;
            else if (d_note == 37) v.drum_slot = 5;
            else if (d_note == 41 || d_note == 45) v.drum_slot = 6;
            else v.drum_slot = static_cast<uint8_t>(v_idx % kDrumSlots);

            v.phase1 = 0.0f; // скидання позиції відтворення семпла
            float att = 0.001f;
            float dec = 0.05f + tracks_[t].p[2] * 0.85f;
            v.amp_adsr.trigger(att, dec, 0.0f, sr);
            v.filter_adsr.trigger(0.001f, dec * 0.7f, 0.0f, sr);
        } else if (tracks_[t].engine == EngineType::kDx7) {
            int p_idx = std::clamp(static_cast<int>(tracks_[t].p[0] * (dx7_engine_.patchCount() - 1) + 0.5f), 0, dx7_engine_.patchCount() - 1);
            v.dx7_voice.trigger(dx7_engine_.getPatch(p_idx), static_cast<float>(midi_note), velocity, sr);
            v.amp_adsr.trigger(0.001f, 0.99f, 1.0f, sr);
            v.filter_adsr.trigger(0.001f, 0.99f, 1.0f, sr);
        } else if (tracks_[t].engine == EngineType::kSampleOneShot || tracks_[t].engine == EngineType::kSliceSampler) {
            v.phase1 = 0.0f;
            v.amp_adsr.trigger(tracks_[t].attack, tracks_[t].decay, tracks_[t].sustain, sr);
            v.filter_adsr.trigger(tracks_[t].attack * 0.6f, tracks_[t].decay, tracks_[t].sustain, sr);
        } else {
            v.amp_adsr.trigger(tracks_[t].attack, tracks_[t].decay, tracks_[t].sustain, sr);
            v.filter_adsr.trigger(tracks_[t].attack * 0.6f, tracks_[t].decay, tracks_[t].sustain, sr);
        }
    }
}

int AudioEngine::allocateVoice(int track, int midi_note) {
    if (track < 0 || track >= 4) return 0;
    const int base = track * kVoicesPerTrack;

    if (tracks_[track].engine == EngineType::kDrumRack) {
        if (midi_note == 36) return base + 0;
        if (midi_note == 38 || midi_note == 40) return base + 1;
        if (midi_note == 42 || midi_note == 44) return base + 2;
        if (midi_note == 46 || midi_note == 39) return base + 3;
        return base + (std::abs(midi_note - 36) % kVoicesPerTrack);
    }

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
    if (track < 0 || track >= 4) return;
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
    if (track < 0 || track >= 4) return;
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
    for (int r = 0; r < kTotalRows; ++r) {
        int t = row_track_[r];
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

        if (steps[r].is_param_trig) {
            // Жовтий Elektron Trig (Param Trig / P-Lock): лише оновлюємо параметри, без нової ноти!
            std::lock_guard<std::mutex> lock(state_mutex_);
            voices_[r].plock_cutoff = steps[r].has_plock ? steps[r].plock_cutoff : -1.0f;
        } else {
            // Звичайний Note Trig
            triggerVoice(
                r,
                steps[r].note,
                steps[r].velocity,
                steps[r].has_plock ? steps[r].plock_cutoff : -1.0f,
                steps[r].slide,
                steps[r].gate_len
            );
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

    std::array<TrackParameters, 4> local_tracks;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        local_tracks = tracks_;
    }

    auto computeLfo = [](LfoParameters& lfo, float dt) -> float {
        if (lfo.depth <= 0.001f) return 0.0f;
        lfo.phase += lfo.rate * dt;
        if (lfo.phase >= 1.0f) lfo.phase -= std::floor(lfo.phase);
        float p = lfo.phase;
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
        return raw * lfo.depth * 0.40f;
    };

    float dt_block = inv_sr * static_cast<float>(frames);
    std::array<float, 4> lfo1_val{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> lfo2_val{0.0f, 0.0f, 0.0f, 0.0f};
    for (int t = 0; t < 4; ++t) {
        lfo1_val[t] = computeLfo(local_tracks[t].lfo1, dt_block);
        lfo2_val[t] = computeLfo(local_tracks[t].lfo2, dt_block);
    }

    auto getTrackMod = [&](int trk, StickDest dest) -> float {
        const auto& tp = local_tracks[trk];
        float val = 0.0f;
        if (tp.stick_left_x == dest) val += raw_lx;
        if (tp.stick_left_y == dest) val += raw_ly;
        if (tp.stick_right_x == dest) val += raw_rx;
        if (tp.stick_right_y == dest) val += raw_ry;
        if (tp.lfo1.dest == dest) val += lfo1_val[trk];
        if (tp.lfo2.dest == dest) val += lfo2_val[trk];
        return val;
    };

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
        float rev_l = 0.0f;
        float rev_r = 0.0f;

        for (int r = 0; r < active_rows_count_; ++r) {
            auto& v = voices_[r];
            int t = std::clamp(static_cast<int>(row_track_[r]), 0, 3);
            const auto& tp = local_tracks[t];
            if (tp.muted || !v.active) continue;

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

            if (tp.engine == EngineType::kDrumRack) {
                const auto& sb = sampler().drumSlot(v.drum_slot);
                if (sb.loaded && sb.lengthFrames() > 0) {
                    float pitch_ratio = std::pow(2.0f, (tp.p[1] - 0.5f) * 2.0f);
                    float step = pitch_ratio * (static_cast<float>(sb.sample_rate) * inv_sr);
                    uint32_t f0 = static_cast<uint32_t>(v.phase1);
                    if (f0 + 1 < sb.lengthFrames()) {
                        float frac = v.phase1 - static_cast<float>(f0);
                        raw = sb.data_l[f0] + frac * (sb.data_l[f0 + 1] - sb.data_l[f0]);
                        v.phase1 += step;
                    } else {
                        raw = 0.0f;
                        v.active = false;
                    }
                } else {
                    raw = 0.0f;
                }
            } else if (tp.engine == EngineType::kSampleOneShot) {
                const auto& sb = sampler().recordedSample();
                if (sb.loaded && sb.lengthFrames() > 0) {
                    float note_pitch = static_cast<float>(v.note_num) - 60.0f + (tp.p[1] - 0.5f) * 24.0f;
                    float pitch_ratio = std::pow(2.0f, note_pitch / 12.0f);
                    float step = pitch_ratio * (static_cast<float>(sb.sample_rate) * inv_sr);
                    uint32_t f0 = static_cast<uint32_t>(v.phase1);
                    if (f0 + 1 < sb.lengthFrames()) {
                        float frac = v.phase1 - static_cast<float>(f0);
                        raw = sb.data_l[f0] + frac * (sb.data_l[f0 + 1] - sb.data_l[f0]);
                        v.phase1 += step;
                    } else {
                        raw = 0.0f;
                        v.active = false;
                    }
                } else {
                    raw = 0.0f;
                }
            } else if (tp.engine == EngineType::kSliceSampler) {
                const auto& sb = sampler().recordedSample();
                int s_idx = std::clamp(static_cast<int>(tp.p[1] * (sampler().sliceCount() - 1) + 0.5f), 0, sampler().sliceCount() - 1);
                SlicePoint pt = sampler().getSlice(s_idx);
                if (sb.loaded && pt.length_frames > 0) {
                    float pitch_ratio = std::pow(2.0f, (tp.p[2] - 0.5f) * 2.0f);
                    float step = pitch_ratio * (static_cast<float>(sb.sample_rate) * inv_sr);
                    uint32_t cur_f = pt.start_frame + static_cast<uint32_t>(v.phase1);
                    if (v.phase1 < static_cast<float>(pt.length_frames) && cur_f + 1 < sb.lengthFrames()) {
                        float frac = v.phase1 - static_cast<float>(static_cast<uint32_t>(v.phase1));
                        raw = sb.data_l[cur_f] + frac * (sb.data_l[cur_f + 1] - sb.data_l[cur_f]);
                        v.phase1 += step;
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
                raw = v.dx7_voice.process(patch, bright, dec, static_cast<float>(sample_rate));
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

            // Резонансний SVF-фільтр з урахуванням P-Lock кроку, стіків та LFO
            float base_cut = (v.plock_cutoff >= 0.0f) ? v.plock_cutoff : tp.p[0];
            float cut_offset = cut_macro + getTrackMod(t, StickDest::kCutoff);
            float env_offset = env_macro + getTrackMod(t, StickDest::kEnvMod);
            float res_offset = res_macro + getTrackMod(t, StickDest::kResonance);

            float f_cut = std::clamp(
                base_cut + cut_offset + f_env * std::clamp(tp.p[2] + env_offset, 0.0f, 1.0f) * 0.58f,
                0.04f,
                0.88f
            );
            float reso = std::clamp(tp.p[1] + res_offset, 0.0f, 0.90f);
            float q_damp = std::clamp(1.0f - reso * 0.88f, 0.10f, 1.0f);

            v.svf_low += f_cut * v.svf_band;
            float high = raw - v.svf_low - q_damp * v.svf_band;
            v.svf_band += f_cut * high;
            v.svf_band = std::clamp(v.svf_band, -1.85f, 1.85f);

            float trk_level = std::clamp(tp.level + getTrackMod(t, StickDest::kLevel), 0.0f, 1.0f);
            float out_s = v.svf_low * amp * v.velocity * trk_level;

            float abs_s = std::fabs(out_s);
            if (abs_s > trk_peak[t]) trk_peak[t] = abs_s;

            float trk_pan = std::clamp(tp.pan + getTrackMod(t, StickDest::kPan), -1.0f, 1.0f);
            float pan_l = std::clamp(1.0f - trk_pan, 0.0f, 1.0f) * 0.5f;
            float pan_r = std::clamp(1.0f + trk_pan, 0.0f, 1.0f) * 0.5f;

            float d_send = std::clamp(tp.delay_send + getTrackMod(t, StickDest::kDelaySend), 0.0f, 1.0f);
            float r_send = std::clamp(tp.reverb_send + getTrackMod(t, StickDest::kReverbSend), 0.0f, 1.0f);

            mix_l += out_s * pan_l;
            mix_r += out_s * pan_r;
            send_l += out_s * d_send * pan_l;
            send_r += out_s * d_send * pan_r;
            rev_l  += out_s * r_send * pan_l;
            rev_r  += out_s * r_send * pan_r;
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

        // Майстер стерео пінг-понг ділей
        uint32_t read_idx = (delay_idx_ + delay_buf_l_.size() - delay_len) % delay_buf_l_.size();
        float d_l = delay_buf_l_[read_idx];
        float d_r = delay_buf_r_[read_idx];
        delay_buf_l_[delay_idx_] = std::clamp(send_l + d_r * dly_fb, -0.95f, 0.95f);
        delay_buf_r_[delay_idx_] = std::clamp(send_r + d_l * dly_fb, -0.95f, 0.95f);
        delay_idx_ = (delay_idx_ + 1) % delay_buf_l_.size();

        // Майстер стерео ревербератор (дифузний Schroeder танк)
        uint32_t rev_len = (sample_rate == 48000u) ? 14400u : 9600u;
        uint32_t r_read = (reverb_idx_ + reverb_buf_l_.size() - rev_len) % reverb_buf_l_.size();
        float rv_l = reverb_buf_l_[r_read];
        float rv_r = reverb_buf_r_[r_read];
        reverb_buf_l_[reverb_idx_] = std::clamp(rev_l + (rv_r * 0.60f + rv_l * 0.30f) * rev_dec, -0.95f, 0.95f);
        reverb_buf_r_[reverb_idx_] = std::clamp(rev_r + (rv_l * 0.60f + rv_r * 0.30f) * rev_dec, -0.95f, 0.95f);
        reverb_idx_ = (reverb_idx_ + 1) % reverb_buf_l_.size();

        mix_l = std::clamp((mix_l + d_l * dly_mix + rv_l * rev_mix) * master_vol, -0.98f, 0.98f);
        mix_r = std::clamp((mix_r + d_r * dly_mix + rv_r * rev_mix) * master_vol, -0.98f, 0.98f);

        max_l = std::max(max_l, std::fabs(mix_l));
        max_r = std::max(max_r, std::fabs(mix_r));

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

    for (int t = 0; t < 4; ++t) {
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
