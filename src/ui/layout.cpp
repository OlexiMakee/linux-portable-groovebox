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
            char b[80];
            const auto tp = c.audio.getTrackParams(c.app.selected_track);
            std::string nm = c.audio.getTrackName(c.app.selected_track);
            std::snprintf(b, sizeof(b), "SYNTH [T%02d/%02d: %s - %s] | < SEL+X/Y >",
                          c.app.selected_track + 1, c.audio.getActiveRowCount(), nm.c_str(), getEngineTypeName(tp.engine));
            return std::string(b);
        };
        synth_spec.dynamic_rows = [](const UiContext& c) -> std::vector<GridRow> {
            const auto tp = c.audio.getTrackParams(c.app.selected_track);
            switch (tp.engine) {
                case EngineType::kDx7:
                    return {
                        {"INST",  {"TRK.ENGINE", "TRK.P1", "TRK.RND"}},
                        {"DX7",   {"TRK.P5", "TRK.P6", "TRK.P3", "TRK.P4"}},
                        {"TIMBRE",{"TRK.P2", "TRK.GLIDE", "FX.CHR_MIX", "FX.DRIVE"}},
                        {"LPF",   {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",   {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV",   {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}},
                        {"LFO1",  {"LFO1.WAVE", "LFO1.RATE", "LFO1.MULT", "LFO1.DEPTH", "LFO1.DEST", "LFO1.MODE"}},
                        {"LFO2",  {"LFO2.WAVE", "LFO2.RATE", "LFO2.MULT", "LFO2.DEPTH", "LFO2.DEST", "LFO2.MODE"}},
                        {"SEND",  {"TRK.DLY", "TRK.REV", "TRK.PAN", "TRK.LVL"}}
                    };
                case EngineType::kAcid303:
                    return {
                        {"INST",  {"TRK.ENGINE", "TRK.P6", "TRK.RND"}},
                        {"303",   {"TRK.P1", "TRK.P2", "TRK.P3", "TRK.P4"}},
                        {"ACID",  {"TRK.P5", "TRK.GLIDE", "FX.DRIVE", "FX.DRV_TONE"}},
                        {"LPF",   {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",   {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"LFO1",  {"LFO1.WAVE", "LFO1.RATE", "LFO1.MULT", "LFO1.DEPTH", "LFO1.DEST", "LFO1.MODE"}},
                        {"LFO2",  {"LFO2.WAVE", "LFO2.RATE", "LFO2.MULT", "LFO2.DEPTH", "LFO2.DEST", "LFO2.MODE"}},
                        {"SEND",  {"TRK.DLY", "TRK.REV", "TRK.PAN", "TRK.LVL"}}
                    };
                case EngineType::kFm2Op:
                    return {
                        {"INST",  {"TRK.ENGINE", "TRK.GLIDE", "TRK.RND"}},
                        {"2-OP",  {"TRK.P1", "TRK.P2", "TRK.P3", "TRK.P5"}},
                        {"MOD",   {"TRK.P4", "TRK.P6", "FX.CHR_MIX", "FX.DRIVE"}},
                        {"LPF",   {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",   {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV",   {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}},
                        {"LFO1",  {"LFO1.WAVE", "LFO1.RATE", "LFO1.MULT", "LFO1.DEPTH", "LFO1.DEST", "LFO1.MODE"}},
                        {"LFO2",  {"LFO2.WAVE", "LFO2.RATE", "LFO2.MULT", "LFO2.DEPTH", "LFO2.DEST", "LFO2.MODE"}},
                        {"SEND",  {"TRK.DLY", "TRK.REV", "TRK.PAN", "TRK.LVL"}}
                    };
                case EngineType::kChordPad:
                    return {
                        {"INST",  {"TRK.ENGINE", "TRK.GLIDE", "TRK.RND"}},
                        {"CHORD", {"TRK.P3", "TRK.P5", "TRK.P1", "TRK.P2"}},
                        {"COLOR", {"FX.CHR_MIX", "FX.CHR_RATE", "FX.DRIVE", "TRK.P6"}},
                        {"LPF",   {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",   {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV",   {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}},
                        {"LFO1",  {"LFO1.WAVE", "LFO1.RATE", "LFO1.MULT", "LFO1.DEPTH", "LFO1.DEST", "LFO1.MODE"}},
                        {"LFO2",  {"LFO2.WAVE", "LFO2.RATE", "LFO2.MULT", "LFO2.DEPTH", "LFO2.DEST", "LFO2.MODE"}},
                        {"SEND",  {"TRK.DLY", "TRK.REV", "TRK.PAN", "TRK.LVL"}}
                    };
                case EngineType::kSampleOneShot:
                    return {
                        {"INST",  {"TRK.ENGINE", "TRK.WAVE_SAMPLE", "TRK.P2", "TRK.RND"}},
                        {"SMPL",  {"TRK.P3", "TRK.P4", "TRK.P5", "TRK.P6"}},
                        {"COLOR", {"FX.DRIVE", "FX.DRV_TONE", "FX.CHR_MIX", "TRK.GLIDE"}},
                        {"LPF",   {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",   {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV",   {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}},
                        {"LFO1",  {"LFO1.WAVE", "LFO1.RATE", "LFO1.MULT", "LFO1.DEPTH", "LFO1.DEST", "LFO1.MODE"}},
                        {"LFO2",  {"LFO2.WAVE", "LFO2.RATE", "LFO2.MULT", "LFO2.DEPTH", "LFO2.DEST", "LFO2.MODE"}},
                        {"SEND",  {"TRK.DLY", "TRK.REV", "TRK.PAN", "TRK.LVL"}}
                    };
                case EngineType::kSliceSampler:
                    return {
                        {"INST",  {"TRK.ENGINE", "TRK.WAVE_SAMPLE", "TRK.P3", "TRK.RND"}},
                        {"SMPL",  {"TRK.P2", "TRK.P4", "TRK.P5", "TRK.P6"}},
                        {"COLOR", {"FX.DRIVE", "FX.DRV_TONE", "FX.CHR_MIX", "TRK.GLIDE"}},
                        {"LPF",   {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",   {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV",   {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}},
                        {"LFO1",  {"LFO1.WAVE", "LFO1.RATE", "LFO1.MULT", "LFO1.DEPTH", "LFO1.DEST", "LFO1.MODE"}},
                        {"LFO2",  {"LFO2.WAVE", "LFO2.RATE", "LFO2.MULT", "LFO2.DEPTH", "LFO2.DEST", "LFO2.MODE"}},
                        {"SEND",  {"TRK.DLY", "TRK.REV", "TRK.PAN", "TRK.LVL"}}
                    };
                case EngineType::kMidiOut:
                    return {
                        {"INST",  {"TRK.ENGINE", "TRK.P1", "TRK.P6"}},
                        {"CC_A",  {"TRK.P2", "TRK.P3"}},
                        {"CC_B",  {"TRK.P4", "TRK.P5"}},
                        {"SEND",  {"TRK.PAN", "TRK.LVL"}}
                    };
                case EngineType::kVaMono:
                default:
                    return {
                        {"INST",  {"TRK.ENGINE", "TRK.GLIDE", "TRK.RND"}},
                        {"OSC",   {"TRK.P5", "TRK.P6", "TRK.P1", "TRK.P2"}},
                        {"COLOR", {"FX.DRIVE", "FX.DRV_TONE", "FX.CHR_MIX", "TRK.P3"}},
                        {"LPF",   {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",   {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV",   {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}},
                        {"LFO1",  {"LFO1.WAVE", "LFO1.RATE", "LFO1.MULT", "LFO1.DEPTH", "LFO1.DEST", "LFO1.MODE"}},
                        {"LFO2",  {"LFO2.WAVE", "LFO2.RATE", "LFO2.MULT", "LFO2.DEPTH", "LFO2.DEST", "LFO2.MODE"}},
                        {"SEND",  {"TRK.DLY", "TRK.REV", "TRK.PAN", "TRK.LVL"}}
                    };
            }
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
        fx_spec.accent = palette::kAccentPurple;
        fx_spec.tab_title = [](const UiContext&) { return "FX/MIX"; };
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
        mix_spec.tab_title = [](const UiContext&) { return "MIX/FX"; };
        mix_spec.header = [](const UiContext& c) {
            char b[64];
            std::snprintf(b, sizeof(b), "MIXER [%02d CHANNELS ACTIVE] & BUS/MST", c.audio.getActiveRowCount());
            return std::string(b);
        };
        mix_spec.on_focus = [](UiContext& c, int r) {
            if (r >= 0 && r < c.audio.getActiveRowCount()) c.app.selected_track = r;
        };
        mix_spec.dynamic_rows = [](const UiContext& c) -> std::vector<GridRow> {
            std::vector<GridRow> rows;
            const int count = c.audio.getActiveRowCount();
            for (int t = 0; t < count; ++t) {
                std::string nm = c.audio.getTrackName(t);
                if (nm.length() > 5) nm = nm.substr(0, 5);
                char title[16];
                std::snprintf(title, sizeof(title), "%02d:%s", t + 1, nm.c_str());
                std::string pre = "T" + std::to_string(t + 1) + ".";
                rows.push_back({title, {pre + "HPF", pre + "LVL", pre + "PAN", pre + "DLY", pre + "REV", pre + "MUTE"}});
            }
            rows.push_back({"BUS", {"FX.DRIVE", "FX.DRV_TONE", "FX.CHR_MIX", "FX.CHR_RATE"}});
            rows.push_back({"MST", {"MST.VOL", "DLY.FBK", "REV.RET", "MST.BPM"}});
            return rows;
        };
        shell.registerView(std::make_unique<GridView>(std::move(mix_spec)));
    }

    // =========================================================================
    // 5. РОБОЧА СТАНЦІЯ СЕМПЛЮВАННЯ (SAMPLE)
    // =========================================================================
    shell.registerView(std::make_unique<SampleView>());

    // =========================================================================
    // 6. СИСТЕМНІ НАЛАШТУВАННЯ (SYS) — ТАБЛИЧНЕ МЕНЮ 4x2 ТА СТАТУС-ОВЕРЛЕЙ
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
            std::snprintf(b, sizeof(b), "STEP EDITOR [T%d:V%d ST%03d]", t + 1, v + 1, c.app.cursor_step + 1);
            return std::string(b);
        };
        ins_spec.rows = {
            {"", {"STEP.TYPE"}},
            {"", {"STEP.NOTE"}},
            {"", {"STEP.CHORD1"}},
            {"", {"STEP.CHORD2"}},
            {"", {"STEP.CHORD3"}},
            {"", {"STEP.VEL"}},
            {"", {"STEP.GATE"}},
            {"", {"STEP.SLIDE"}},
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
    // Порядок зліва направо: L1 (SEQ), L2 (SYNTH), R2 (MIX <-> FX), R1 (SAMPLE), G (SYS)
    // 4 однакові вкладки під пальці по 70px + вузька SYS 40px = 320px
    // Вкладка R1: Робоча станція семплювання (SAMPLE). Живий PERF викликається через SELECT + R1!
    // =========================================================================
    shell.setTabSlots({
        TabSlot{"[L1]", {ViewId::kSeq}, 70},
        TabSlot{"[L2]", {ViewId::kSynth}, 70},
        TabSlot{"[R2]", {ViewId::kMix, ViewId::kFx}, 70},
        TabSlot{"[R1]", {ViewId::kSample}, 70},
        TabSlot{"[G]",  {ViewId::kSys}, 40}
    });
}

} // namespace groovebox
