#include "ui/ui_shell.hpp"

#include <cstdio>

namespace groovebox {

void setupAppLayout(UiShell& shell) {
    // =========================================================================
    // 1. СЕКВЕНСОР (SEQ)
    // =========================================================================
    shell.registerView(std::make_unique<SeqView>());

    // =========================================================================
    // 2. СИНТЕЗАТОР (SYNTH)
    // =========================================================================
    {
        GridSpec synth_spec;
        synth_spec.id = ViewId::kSynth;
        synth_spec.style = GridStyle::kTable;
        synth_spec.accent = palette::kAccentCyan;
        synth_spec.tab_title = [](const UiContext&) { return "SYNTH"; };
        synth_spec.header = [](const UiContext& c) {
            char b[64];
            const auto tp = c.audio.getTrackParams(c.app.selected_track);
            std::snprintf(b, sizeof(b), "SYNTH TRK T%d [%s]", c.app.selected_track + 1, getEngineTypeName(tp.engine));
            return std::string(b);
        };
        synth_spec.rows = {
            // Row 0: Інструмент, вибір форми хвилі / семпла, рандомайзер
            {"INST", {"TRK.ENGINE", "TRK.WAVE_SAMPLE", "TRK.RND"}},
            // Row 1: Фільтр та тембр (динамічні назви P1..P4)
            {"FLT",  {"TRK.P1", "TRK.P2", "TRK.P3", "TRK.P4"}},
            // Row 2: Осцилятори
            {"OSC",  {"TRK.P5", "TRK.P6"}},
            // Row 3: ADSR огинаюча
            {"ENV",  {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}},
            // Row 4: Модуляція LFO 1 та LFO 2 з призначенням DEST
            {"LFO1", {"LFO1.WAVE", "LFO1.RATE", "LFO1.DEPTH", "LFO1.DEST"}},
            {"LFO2", {"LFO2.WAVE", "LFO2.RATE", "LFO2.DEPTH", "LFO2.DEST"}},
            // Row 5: Посили та рівні
            {"SEND", {"TRK.DLY", "TRK.REV", "TRK.PAN", "TRK.LVL"}}
        };
        shell.registerView(std::make_unique<GridView>(std::move(synth_spec)));
    }

    // =========================================================================
    // 3. ЕФЕКТИ (FX)
    // =========================================================================
    {
        GridSpec fx_spec;
        fx_spec.id = ViewId::kFx;
        fx_spec.style = GridStyle::kTable;
        fx_spec.accent = palette::kAccentOrange;
        fx_spec.tab_title = [](const UiContext&) { return "FX"; };
        fx_spec.header = [](const UiContext&) { return "MASTER SEND EFFECTS & OUTPUT"; };
        fx_spec.rows = {
            {"DLY", {"DLY.TIME", "DLY.FBK", "DLY.RET", "MST.BPM"}},
            {"REV", {"REV.DECAY", "REV.RET", "TRK.DLY", "TRK.REV"}},
            {"CHR", {"FX.CHR_MIX", "FX.CHR_RATE", "FX.CHR_DEP", "TRK.PAN"}},
            {"DRV", {"FX.DRIVE", "FX.DRV_TONE", "MST.VOL", "MST.PEAK"}}
        };
        shell.registerView(std::make_unique<GridView>(std::move(fx_spec)));
    }

    // =========================================================================
    // 4. МІКШЕР (MIX)
    // =========================================================================
    {
        GridSpec mix_spec;
        mix_spec.id = ViewId::kMix;
        mix_spec.style = GridStyle::kTable;
        mix_spec.accent = palette::kAccentOrange;
        mix_spec.tab_title = [](const UiContext&) { return "MIX"; };
        mix_spec.header = [](const UiContext&) { return "MIXER CHANNELS & MASTER"; };
        mix_spec.on_focus = [](UiContext& c, int r) {
            if (r >= 0 && r < 4) c.app.selected_track = r;
        };
        mix_spec.rows = {
            {"T1",  {"T1.LVL", "T1.PAN", "T1.DLY", "T1.REV", "T1.MUTE"}},
            {"T2",  {"T2.LVL", "T2.PAN", "T2.DLY", "T2.REV", "T2.MUTE"}},
            {"T3",  {"T3.LVL", "T3.PAN", "T3.DLY", "T3.REV", "T3.MUTE"}},
            {"T4",  {"T4.LVL", "T4.PAN", "T4.DLY", "T4.REV", "T4.MUTE"}},
            {"MST", {"MST.VOL", "DLY.FBK", "REV.RET", "MST.BPM"}}
        };
        shell.registerView(std::make_unique<GridView>(std::move(mix_spec)));
    }

    // =========================================================================
    // 5. ОСЦИЛОГРАФ (SCOPE)
    // =========================================================================
    shell.registerView(std::make_unique<ScopeView>());

    // =========================================================================
    // 6. СИСТЕМНІ НАЛАШТУВАННЯ (SYS) — ТАБЛИЧНЕ МЕНЮ 4x3 ТА СТАТУС-ОВЕРЛЕЙ
    // =========================================================================
    {
        GridSpec sys_spec;
        sys_spec.id = ViewId::kSys;
        sys_spec.style = GridStyle::kTable;
        sys_spec.accent = palette::kAccentOrange;
        sys_spec.tab_title = [](const UiContext&) { return "SYS"; };
        sys_spec.header = [](const UiContext& c) {
            char b[96];
            std::snprintf(b, sizeof(b), "SYS STATS: CPU %3.1f%% | %2dC | RAM %d/%d MB | SR %u",
                          c.hw.cpu_load_percent, c.hw.cpu_temp_c, c.hw.ram_used_mb, c.hw.ram_total_mb, c.tel.sample_rate);
            return std::string(b);
        };
        sys_spec.rows = {
            {"PROJ", {"SYS.BANK", "SYS.PAT", "SYS.SAVE", "SYS.LOAD"}},
            {"SMPL", {"SYS.SAMPLER", "SYS.SLICES", "SYS.REC_SRC", "SYS.REC_ARM"}},
            {"AUD",  {"SYS.BUF", "SYS.USB", "SYS.OUT", "SYS.PANIC"}}
        };
        shell.registerView(std::make_unique<GridView>(std::move(sys_spec)));
    }

    // =========================================================================
    // 7. STEP INSPECTOR POPUP (INSPECT)
    // =========================================================================
    {
        GridSpec ins_spec;
        ins_spec.id = ViewId::kInspect;
        ins_spec.style = GridStyle::kPopup;
        ins_spec.accent = palette::kParamTrig;
        ins_spec.header = [](const UiContext& c) {
            char b[64];
            const int t = c.audio.getRowTrack(c.app.cursor_row);
            const int v = c.audio.getRowVoice(c.app.cursor_row);
            std::snprintf(b, sizeof(b), "STEP INSPECTOR [T%d:V%d ST%03d]", t + 1, v + 1, c.app.cursor_step + 1);
            return std::string(b);
        };
        ins_spec.rows = {
            {"", {"STEP.TYPE"}},
            {"", {"STEP.NOTE"}},
            {"", {"STEP.VEL"}},
            {"", {"STEP.GATE"}},
            {"", {"STEP.SLIDE"}},
            {"", {"STEP.ATK"}},
            {"", {"STEP.DEC"}},
            {"", {"STEP.SUS"}},
            {"", {"STEP.REL"}},
            {"", {"STEP.CUT"}},
            {"", {"STEP.PROB"}},
            {"", {"STEP.COND"}}
        };
        shell.registerView(std::make_unique<GridView>(std::move(ins_spec)));
    }

    // =========================================================================
    // 8. ЖИВИЙ ПЕРФОРМАНС (PERF)
    // =========================================================================
    shell.registerView(std::make_unique<PerfView>());

    // =========================================================================
    // 9. ДВОКАСКАДНИЙ АРПЕДЖИРАТОР ТА ГЕНЕРАТОР (ARP)
    // =========================================================================
    {
        GridSpec arp_spec;
        arp_spec.id = ViewId::kArp;
        arp_spec.style = GridStyle::kTable;
        arp_spec.accent = palette::kAccentOrange;
        arp_spec.tab_title = [](const UiContext&) { return "ARP"; };
        arp_spec.header = [](const UiContext&) { return "DUAL ARPEGGIATOR & PROCEDURAL ENGINE"; };
        arp_spec.rows = {
            {"SCALE", {"ARP.SCALE", "ARP.ROOT", "TRK.TRANSP", "TRK.GLIDE"}},
            {"ARP1",  {"ARP.DIV1", "ARP.CHORD", "ARP.PROG", "ARP.OCT"}},
            {"ARP2",  {"ARP.DIV2", "ARP.PAT", "ARP.GATE", "STEP.SLIDE"}},
            {"ALGO",  {"ARP.EUCLID", "ARP.MUTATE", "ARP.RND", "ARP.COMMIT"}}
        };
        shell.registerView(std::make_unique<GridView>(std::move(arp_spec)));
    }

    // =========================================================================
    // 10. ВКЛАДКИ ВЕРХНЬОГО РЯДУ
    // Порядок зліва направо: L1 (SEQ), L2 (SYNTH), R2 (MIX <-> FX), R1 (PERF), G (SYS)
    // 4 однакові вкладки під пальці по 70px + вузька SYS 40px = 320px
    // Вкладка R1 закріплена виключно за живим Performance Mode!
    // =========================================================================
    shell.setTabSlots({
        TabSlot{"[L1]", {ViewId::kSeq}, 70},
        TabSlot{"[L2]", {ViewId::kSynth}, 70},
        TabSlot{"[R2]", {ViewId::kMix, ViewId::kFx}, 70},
        TabSlot{"[R1]", {ViewId::kPerf}, 70},
        TabSlot{"[G]",  {ViewId::kSys}, 40}
    });
}

} // namespace groovebox
