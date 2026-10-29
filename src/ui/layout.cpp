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
            char b[96];
            const auto tp = c.audio.getTrackParams(c.app.selected_track);
            std::string nm = c.audio.getTrackName(c.app.selected_track);
            if (c.app.synth_submode == SynthSubmode::kMod) {
                std::snprintf(b, sizeof(b), "MOD MATRIX [T%02d/%02d: %s] | 4 LFOs * 2 ENVs * STICKS",
                              c.app.selected_track + 1, c.audio.getActiveRowCount(), nm.c_str());
            } else if (c.app.synth_submode == SynthSubmode::kFx) {
                std::snprintf(b, sizeof(b), "TRACK FX [T%02d/%02d: %s] | INSERT CHAIN",
                              c.app.selected_track + 1, c.audio.getActiveRowCount(), nm.c_str());
            } else {
                std::snprintf(b, sizeof(b), "SYNTH [T%02d/%02d: %s - %s] | < SEL+X/Y >",
                              c.app.selected_track + 1, c.audio.getActiveRowCount(), nm.c_str(), getEngineTypeName(tp.engine));
            }
            return std::string(b);
        };
        synth_spec.dynamic_rows = [](const UiContext& c) -> std::vector<GridRow> {
            if (c.app.synth_submode == SynthSubmode::kMod) {
                return {
                    {"LFO1",   {"LFO1.WAVE", "LFO1.RATE", "LFO1.MULT", "LFO1.DEPTH", "LFO1.DEST", "LFO1.MODE"}},
                    {"LFO2",   {"LFO2.WAVE", "LFO2.RATE", "LFO2.MULT", "LFO2.DEPTH", "LFO2.DEST", "LFO2.MODE"}},
                    {"LFO3",   {"LFO3.WAVE", "LFO3.RATE", "LFO3.MULT", "LFO3.DEPTH", "LFO3.DEST", "LFO3.MODE"}},
                    {"LFO4",   {"LFO4.WAVE", "LFO4.RATE", "LFO4.MULT", "LFO4.DEPTH", "LFO4.DEST", "LFO4.MODE"}},
                    {"ENV2",   {"ENV2.ATK", "ENV2.DEC", "ENV2.SUS", "ENV2.REL", "ENV2.AMT", "ENV2.DEST"}},
                    {"L-STK1", {"STK.L1_X", "STK.L1_XAMT", "STK.L1_Y", "STK.L1_YAMT"}},
                    {"L-STK2", {"STK.L2_X", "STK.L2_XAMT", "STK.L2_Y", "STK.L2_YAMT"}},
                    {"R-STK1", {"STK.R1_X", "STK.R1_XAMT", "STK.R1_Y", "STK.R1_YAMT"}},
                    {"R-STK2", {"STK.R2_X", "STK.R2_XAMT", "STK.R2_Y", "STK.R2_YAMT"}}
                };
            }
            if (c.app.synth_submode == SynthSubmode::kFx) {
                const auto tp = c.audio.getTrackParams(c.app.selected_track);
                GridRow r_crush = {"CRUSH",  {"FX.BC_BITS", "FX.SRR"}};
                GridRow r_dist  = {"DIST",   {"FX.DST_DRV", "FX.DST_TONE", "FX.DST_MIX"}};
                GridRow r_chor  = {"CHORUS", {"FX.CHR_MIX", "FX.CHR_RATE", "FX.CHR_DEP"}};
                GridRow r_scomp = {"S-COMP", {"FX.SC_SRC", "FX.SC_THR", "FX.SC_RAT", "FX.SC_ATK", "FX.SC_REL", "FX.SC_POS"}};
                GridRow r_dly   = {"DELAY",  {"FX.DLY_DIV", "FX.DLY_FBK", "FX.DLY_WID", "FX.DLY_MIX"}};
                GridRow r_rev   = {"REVERB", {"FX.REV_ENG", "FX.REV_DEC", "FX.REV_HICUT", "FX.REV_LOCUT", "FX.REV_PRE", "FX.REV_MIX"}};
                GridRow r_snds  = {"SENDS",  {"TRK.DLY", "TRK.REV", "TRK.CHR"}};

                if (tp.fx.sc_pos == SidechainPos::kPreDelayRev) {
                    return { r_crush, r_dist, r_chor, r_scomp, r_dly, r_rev, r_snds };
                } else {
                    return { r_crush, r_dist, r_chor, r_dly, r_rev, r_scomp, r_snds };
                }
            }
            const auto tp = c.audio.getTrackParams(c.app.selected_track);
            switch (tp.engine) {
                case EngineType::kDx7:
                    return {
                        {"INST",   {"TRK.ENGINE", "TRK.PRESET", "TRK.P1"}},
                        {"DX7",    {"TRK.P5", "TRK.P6", "TRK.P3", "TRK.P4", "TRK.P2"}},
                        {"LPF",    {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_POLE", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",    {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_POLE", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV 1 (AMP)", {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}}
                    };
                case EngineType::kAcid303:
                    return {
                        {"INST",   {"TRK.ENGINE", "TRK.PRESET", "TRK.P6"}},
                        {"303",    {"TRK.P1", "TRK.P2", "TRK.P3", "TRK.P4", "TRK.P5"}},
                        {"LPF",    {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_POLE", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",    {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_POLE", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV 1 (AMP)", {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}}
                    };
                case EngineType::kFm2Op:
                    return {
                        {"INST",   {"TRK.ENGINE", "TRK.PRESET", "TRK.P1"}},
                        {"2-OP",   {"TRK.P2", "TRK.P3", "TRK.P4", "TRK.P5", "TRK.P6"}},
                        {"LPF",    {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_POLE", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",    {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_POLE", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV 1 (AMP)", {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}}
                    };
                case EngineType::kChordPad:
                    return {
                        {"INST",   {"TRK.ENGINE", "TRK.PRESET", "TRK.P1"}},
                        {"CHORD",  {"TRK.P2", "TRK.P3", "TRK.P4", "TRK.P5", "TRK.P6"}},
                        {"LPF",    {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_POLE", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",    {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_POLE", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV 1 (AMP)", {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}}
                    };
                case EngineType::kSampleOneShot:
                case EngineType::kSliceSampler:
                    return {
                        {"INST",   {"TRK.ENGINE", "TRK.PRESET", "TRK.WAVE_SAMPLE", "SMP.PLAY_MODE"}},
                        {"PLAY",   {"SMP.LOOP", "SMP.START", "SMP.LOOP_PT", "SMP.END"}},
                        {"TUNING", {"SMP.TUNE", "SMP.SLICE"}},
                        {"LPF",    {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_POLE", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",    {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_POLE", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV 1 (AMP)", {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}}
                    };
                case EngineType::kMidiOut:
                    return {
                        {"INST",   {"TRK.ENGINE", "TRK.PRESET", "TRK.P1", "TRK.P6"}},
                        {"CC_A",   {"TRK.P2", "TRK.P3"}},
                        {"CC_B",   {"TRK.P4", "TRK.P5"}}
                    };
                case EngineType::kVaMono:
                default:
                    return {
                        {"INST",   {"TRK.ENGINE", "TRK.PRESET", "TRK.P1"}},
                        {"OSC",    {"TRK.P2", "TRK.P3", "TRK.P4", "TRK.P5", "TRK.P6"}},
                        {"LPF",    {"TRK.LPF_CUT", "TRK.LPF_RES", "TRK.LPF_POLE", "TRK.LPF_ENV", "TRK.LPF_DEC"}},
                        {"HPF",    {"TRK.HPF_CUT", "TRK.HPF_RES", "TRK.HPF_POLE", "TRK.HPF_ENV", "TRK.HPF_DEC"}},
                        {"ENV 1 (AMP)", {"TRK.ATK", "TRK.DEC", "TRK.SUS", "TRK.REL"}}
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
            {"CHR", {"MST.CHR_MIX", "MST.CHR_RATE", "MST.CHR_DEP", "TRK.CHR"}},
            {"MST", {"MST.VOL", "MST.PEAK"}}
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
            if (c.app.mix_submode == MixSubmode::kMaster) {
                std::snprintf(b, sizeof(b), "MASTERING BUS | GAIN/OVER * COMP * HPF/LPF * BELL EQ");
            } else if (c.app.mix_submode == MixSubmode::kSend) {
                std::snprintf(b, sizeof(b), "SEND FX BUS | DELAY, REVERB & CHORUS");
            } else {
                std::snprintf(b, sizeof(b), "MIXER [%02d CHANNELS ACTIVE] & BUS/MST", c.audio.getActiveRowCount());
            }
            return std::string(b);
        };
        mix_spec.on_focus = [](UiContext& c, int r) {
            if (c.app.mix_submode != MixSubmode::kMaster && r >= 0 && r < c.audio.getActiveRowCount()) {
                c.app.selected_track = r;
            }
        };
        mix_spec.dynamic_rows = [](const UiContext& c) -> std::vector<GridRow> {
            if (c.app.mix_submode == MixSubmode::kMaster) {
                return {
                    {"OUT",  {"MST.GAIN", "MST.VOL", "MST.PAN", "MST.BPM"}},
                    {"COMP", {"MST.SC_SRC", "MST.SC_THR", "MST.SC_RAT", "MST.SC_ATK", "MST.SC_REL"}},
                    {"FILT", {"MST.HPF_CUT", "MST.HPF_RES", "MST.HPF_SLP", "MST.LPF_CUT", "MST.LPF_RES", "MST.LPF_SLP"}},
                    {"EQ1",  {"MST.EQ1_FRQ", "MST.EQ1_GAIN", "MST.EQ1_Q"}},
                    {"EQ2",  {"MST.EQ2_FRQ", "MST.EQ2_GAIN", "MST.EQ2_Q"}},
                    {"EQ3",  {"MST.EQ3_FRQ", "MST.EQ3_GAIN", "MST.EQ3_Q"}}
                };
            }
            std::vector<GridRow> rows;
            const int count = c.audio.getActiveRowCount();
            for (int t = 0; t < count; ++t) {
                std::string nm = c.audio.getTrackName(t);
                if (nm.length() > 5) nm = nm.substr(0, 5);
                char title[16];
                std::snprintf(title, sizeof(title), "%02d:%s", t + 1, nm.c_str());
                std::string pre = "T" + std::to_string(t + 1) + ".";
                rows.push_back({title, {pre + "LVL", pre + "PAN", pre + "DLY", pre + "REV", pre + "CHR", pre + "MUTE"}});
            }
            rows.push_back({"BUS", {"FX.CHR_MIX", "FX.CHR_RATE"}});
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
            {"PROJ", {"SYS.BANK", "SYS.PAT", "SYS.SAVE", "SYS.SAVE_AS"}},
            {"AUD",  {"SYS.LOAD", "SYS.BUF", "SYS.OUT", "SYS.PANIC"}}
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
            switch (c.app.inspect_page) {
                case AppModel::InspectPage::kSyn:
                    std::snprintf(b, sizeof(b), "midi-<SYN>-mod-fx");
                    break;
                case AppModel::InspectPage::kMod:
                    std::snprintf(b, sizeof(b), "midi-syn-<MOD>-fx");
                    break;
                case AppModel::InspectPage::kFx:
                    std::snprintf(b, sizeof(b), "midi-syn-mod-<FX>");
                    break;
                case AppModel::InspectPage::kMidi:
                default:
                    std::snprintf(b, sizeof(b), "<MIDI>-syn-mod-fx");
                    break;
            }
            return std::string(b);
        };
        ins_spec.dynamic_rows = [](const UiContext& c) -> std::vector<GridRow> {
            switch (c.app.inspect_page) {
                case AppModel::InspectPage::kSyn:
                    return {
                        {"", {"STEP.CUT"}},
                        {"", {"STEP.P1"}},
                        {"", {"STEP.P2"}},
                        {"", {"STEP.P3"}},
                        {"", {"STEP.P4"}},
                        {"", {"STEP.P5"}},
                        {"", {"STEP.P6"}},
                        {"", {"STEP.ATK"}},
                        {"", {"STEP.DEC"}},
                        {"", {"STEP.SUS"}},
                        {"", {"STEP.REL"}},
                        {"", {"STEP.SMP_START"}},
                        {"", {"STEP.SMP_LOOP"}},
                        {"", {"STEP.SMP_END"}}
                    };
                case AppModel::InspectPage::kMod:
                    return {
                        {"", {"LFO1.DEPTH"}},
                        {"", {"LFO1.RATE"}},
                        {"", {"LFO1.DEST"}},
                        {"", {"LFO2.DEPTH"}},
                        {"", {"LFO2.RATE"}},
                        {"", {"LFO2.DEST"}},
                        {"", {"ENV2.AMT"}},
                        {"", {"ENV2.DEST"}},
                        {"", {"ENV2.ATK"}},
                        {"", {"ENV2.DEC"}}
                    };
                case AppModel::InspectPage::kFx:
                    return {
                        {"", {"STEP.DLY_SEND"}},
                        {"", {"STEP.REV_SEND"}},
                        {"", {"FX.BC_BITS"}},
                        {"", {"FX.DST_DRV"}},
                        {"", {"FX.CHR_MIX"}}
                    };
                case AppModel::InspectPage::kMidi:
                default:
                    return {
                        {"", {"STEP.TYPE"}},
                        {"", {"STEP.NOTE"}},
                        {"", {"STEP.CHORD1"}},
                        {"", {"STEP.CHORD2"}},
                        {"", {"STEP.CHORD3"}},
                        {"", {"STEP.VEL"}},
                        {"", {"STEP.GATE"}},
                        {"", {"STEP.SLIDE"}},
                        {"", {"STEP.PROB"}},
                        {"", {"STEP.COND"}}
                    };
            }
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
            {"SCALE", {"ARP.SCALE", "ARP.ROOT", "TRK.TRANSP"}},
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
        TabSlot{"[L1]", {ViewId::kSeq}, 72},
        TabSlot{"[L2]", {ViewId::kSynth}, 72},
        TabSlot{"[R2]", {ViewId::kMix, ViewId::kFx}, 72},
        TabSlot{"[R1]", {ViewId::kSample}, 72},
        TabSlot{"[G]",  {ViewId::kSys}, 32}
    });
}

} // namespace groovebox
