#include "audio_engine.hpp"
#include "synth_engines.hpp"
#include "device_profile.hpp"
#include <chrono>
#include <cstdio>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

#ifdef __linux__
#include <unistd.h>
#include <sys/resource.h>
#else
#include <sys/resource.h>
#endif

using namespace groovebox;

struct BenchResult {
    uint32_t sample_rate;
    uint32_t buffer_size;
    double budget_us;
    double avg_us;
    double max_us;
    double dsp_load_pct;
    double peak_load_pct;
    double headroom_pct;
    double ram_mb;
};

static double getMemoryRssMb() {
#ifdef __linux__
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        unsigned long size = 0, resident = 0;
        statm >> size >> resident;
        long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024;
        return (resident * page_size_kb) / 1024.0;
    }
#endif
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
        return usage.ru_maxrss / (1024.0 * 1024.0);
#else
        return usage.ru_maxrss / 1024.0;
#endif
    }
    return 0.0;
}

static BenchResult runStressTest(uint32_t sr, uint32_t frames, int num_blocks) {
    AudioEngine audio;
    audio.start(0, false, false); // Без захвату фізичного ALSA, чистий реалтайм DSP-рендеринг

    // Налаштовуємо всі 16 активних треків з різними важкими движками
    audio.setActiveRowCount(16);

    for (int t = 0; t < 16; ++t) {
        auto tp = audio.getTrackParams(t);
        tp.muted = false;
        tp.level = 0.85f;
        tp.pan = ((t % 4) - 1.5f) * 0.4f;

        // Розподіл движків для максимального різноманіття навантаження
        switch (t % 6) {
            case 0: tp.engine = EngineType::kVaMono; break;
            case 1: tp.engine = EngineType::kFm2Op; break;
            case 2: tp.engine = EngineType::kAcid303; break;
            case 3: tp.engine = EngineType::kChordPad; break;
            case 4: tp.engine = EngineType::kDx7; break;
            case 5: tp.engine = EngineType::kSampleOneShot; break;
        }

        // Подвійні фільтри (2-pole LPF + 2-pole HPF SVF) на повну потужність
        tp.lpf_cutoff = 0.60f;
        tp.lpf_resonance = 0.70f;
        tp.lpf_env = 0.50f;
        tp.lpf_decay = 0.35f;

        tp.hpf_cutoff = 0.25f;
        tp.hpf_resonance = 0.50f;
        tp.hpf_env = 0.20f;
        tp.hpf_decay = 0.25f;

        // Сенди на майстер-ефекти
        tp.delay_send = 0.75f;
        tp.reverb_send = 0.80f;

        audio.setTrackParams(t, tp);

        // Тригеримо по 4 одночасні ноти на кожному з 16 треків = 64 АКТИВНІ ГОЛОСИ!
        int base_note = 36 + (t % 3) * 12;
        audio.triggerNote(t, base_note + 0, 0.90f);
        audio.triggerNote(t, base_note + 4, 0.85f);
        audio.triggerNote(t, base_note + 7, 0.85f);
        audio.triggerNote(t, base_note + 11, 0.80f);
    }

    std::vector<int16_t> out_buf(frames * 2, 0);

    // Прогрів кешу та ініціалізація буферів (Warm-up)
    for (int i = 0; i < 15; ++i) {
        audio.renderBlock(out_buf.data(), frames, sr);
    }

    std::vector<double> block_times_us;
    block_times_us.reserve(num_blocks);

    // Основний цикл стрес-бенчмарку з живою реалтайм модуляцією ефектів
    for (int b = 0; b < num_blocks; ++b) {
        // Симуляція динамічного руху стіків / LFO / користувацьких крутилок
        float mod_phase = static_cast<float>(b) / static_cast<float>(num_blocks);
        for (int t = 0; t < 16; ++t) {
            auto tp = audio.getTrackParams(t);
            tp.lpf_cutoff = 0.30f + 0.50f * std::sin(mod_phase * 6.283f + t * 0.4f);
            tp.hpf_cutoff = 0.10f + 0.30f * std::cos(mod_phase * 6.283f + t * 0.3f);
            tp.delay_send = 0.50f + 0.45f * std::sin(mod_phase * 3.141f + t);
            audio.setTrackParams(t, tp);
        }

        auto start = std::chrono::high_resolution_clock::now();
        audio.renderBlock(out_buf.data(), frames, sr);
        auto finish = std::chrono::high_resolution_clock::now();

        double elapsed_us = std::chrono::duration<double, std::micro>(finish - start).count();
        block_times_us.push_back(elapsed_us);
    }

    audio.stop();

    double budget_us = (static_cast<double>(frames) / static_cast<double>(sr)) * 1000000.0;
    double sum_us = std::accumulate(block_times_us.begin(), block_times_us.end(), 0.0);
    double avg_us = sum_us / block_times_us.size();
    double max_us = *std::max_element(block_times_us.begin(), block_times_us.end());

    double dsp_load_pct = (avg_us / budget_us) * 100.0;
    double peak_load_pct = (max_us / budget_us) * 100.0;
    double headroom_pct = std::max(0.0, 100.0 - dsp_load_pct);
    double ram_mb = getMemoryRssMb();

    return BenchResult{sr, frames, budget_us, avg_us, max_us, dsp_load_pct, peak_load_pct, headroom_pct, ram_mb};
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    loadActiveProfile();

    std::printf("========================================================================================\n");
    std::printf("       MAGICX XU10 GROOVEBOX: FULL MULTI-BUFFER HARD STRESS BENCHMARK (64 VOICES)       \n");
    std::printf("========================================================================================\n");
    std::printf("Target: 16 Tracks x 4 Polyphonic Voices = 64 Active Voices (VA, FM, 303, Chord, DX7, Sampler)\n");
    std::printf("Master FX: Delay (Ping-Pong) + Reverb (Diffusion) + Chorus + Dual SVF Filters on all 16 Trks\n");
    std::printf("Modulation: Continuous real-time parameter sweeps on every rendered audio block\n");
    std::printf("========================================================================================\n\n");

    const std::vector<uint32_t> sample_rates = {32000, 44100, 48000};
    const std::vector<uint32_t> buffer_sizes = {96, 128, 256, 512, 1024};
    const int num_blocks = 200;

    std::vector<BenchResult> results;

    std::printf("| Sample Rate | Buffer Size | Block Budget |  Avg Render  |  Max Render  | DSP Load | Peak Load | Headroom | Memory RSS |\n");
    std::printf("|:-----------:|:-----------:|:------------:|:------------:|:------------:|:--------:|:---------:|:--------:|:----------:|\n");

    for (uint32_t sr : sample_rates) {
        for (uint32_t buf : buffer_sizes) {
            BenchResult res = runStressTest(sr, buf, num_blocks);
            results.push_back(res);

            std::printf("|  %5u Hz   |  %4u smp   |  %6.2f ms   |  %6.2f ms   |  %6.2f ms   |  %5.1f%%  |  %5.1f%%   |  %5.1f%%  |  %5.1f MB  |\n",
                        res.sample_rate, res.buffer_size,
                        res.budget_us / 1000.0,
                        res.avg_us / 1000.0,
                        res.max_us / 1000.0,
                        res.dsp_load_pct, res.peak_load_pct, res.headroom_pct, res.ram_mb);
            std::fflush(stdout);
        }
    }

    std::printf("========================================================================================\n");
    std::printf("STATUS: MULTI-BUFFER STRESS BENCHMARK COMPLETED SUCCESSFULLY [OK]\n");
    std::printf("========================================================================================\n");

    return 0;
}
