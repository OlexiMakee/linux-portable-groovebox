#!/usr/bin/env python3
"""
Curate samples from Desktop directories into structured project assets
and generate embedded C++ ROM header (src/rom_samples.hpp).
"""
import os
import shutil
import wave
import struct

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRUM_SRC = "/Users/oleksii/Desktop/Drum Machines"
WAVE_SRC = "/Users/oleksii/Desktop/Cycle Wave Forms"

RES_DIR = os.path.join(PROJECT_ROOT, "res")
DRUMS_OUT = os.path.join(RES_DIR, "samples", "drums")
WAVES_OUT = os.path.join(RES_DIR, "samples", "waves")
DX7_OUT = os.path.join(RES_DIR, "dx7")

def ensure_dirs():
    os.makedirs(DRUMS_OUT, exist_ok=True)
    os.makedirs(WAVES_OUT, exist_ok=True)
    os.makedirs(DX7_OUT, exist_ok=True)

def find_file(base_dir, pattern_sub, keywords):
    matches = []
    for root, dirs, files in os.walk(base_dir):
        for f in files:
            if not f.endswith(".wav"):
                continue
            f_lower = f.lower()
            root_lower = root.lower()
            if pattern_sub.lower() in f_lower or pattern_sub.lower() in root_lower:
                all_match = True
                for kw in keywords:
                    if kw.lower() not in f_lower and kw.lower() not in root_lower:
                        all_match = False
                        break
                if all_match:
                    matches.append(os.path.join(root, f))
    
    clean = [m for m in matches if "clean" in os.path.basename(m).lower()]
    if clean:
        return clean[0]
    return matches[0] if matches else None

def curate_drum_kits():
    print("[*] Curating Drum Kits...")
    kit_defs = {
        "01_tr808": {
            "bd": ("808", ["bass drum", "clean"]),
            "sd": ("808", ["snare drum", "clean"]),
            "ch": ("808", ["hi hat", "ch", "clean"]),
            "oh": ("808", ["hi hat", "oh", "clean"]),
            "cp": ("808", ["clap", "clean"]),
            "rim": ("808", ["rim", "clean"]),
            "tom": ("808", ["tom", "clean"]),
            "perc": ("808", ["cowbell", "clean"]),
        },
        "02_tr909": {
            "bd": ("909", ["bass drum", "clean"]),
            "sd": ("909", ["snare drum", "clean"]),
            "ch": ("909", ["hi hat", "ch", "clean"]),
            "oh": ("909", ["hi hat", "oh", "clean"]),
            "cp": ("909", ["clap", "clean"]),
            "rim": ("909", ["rim", "clean"]),
            "tom": ("909", ["tom", "clean"]),
            "perc": ("909", ["crash", "clean"]),
        },
        "03_tr707": {
            "bd": ("707", ["bass drum", "clean"]),
            "sd": ("707", ["snare drum", "clean"]),
            "ch": ("707", ["hi hat", "ch", "clean"]),
            "oh": ("707", ["hi hat", "oh", "clean"]),
            "cp": ("707", ["clap", "clean"]),
            "rim": ("707", ["rim", "clean"]),
            "tom": ("707", ["tom", "clean"]),
            "perc": ("707", ["cowbell", "clean"]),
        },
        "04_linndrum": {
            "bd": ("linn", ["bass drum", "clean"]),
            "sd": ("linn", ["snare drum", "clean"]),
            "ch": ("linn", ["hi hat", "ch", "clean"]),
            "oh": ("linn", ["hi hat", "oh", "clean"]),
            "cp": ("linn", ["clap", "clean"]),
            "rim": ("linn", ["rim", "clean"]),
            "tom": ("linn", ["tom", "clean"]),
            "perc": ("linn", ["cowbell", "clean"]),
        },
        "05_oberheim_dmx": {
            "bd": ("dmx", ["bass drum", "clean"]),
            "sd": ("dmx", ["snare drum", "clean"]),
            "ch": ("dmx", ["hi hat", "ch", "clean"]),
            "oh": ("dmx", ["hi hat", "oh", "clean"]),
            "cp": ("dmx", ["clap", "clean"]),
            "rim": ("dmx", ["rim", "clean"]),
            "tom": ("dmx", ["tom", "clean"]),
            "perc": ("dmx", ["ride", "clean"]),
        },
        "06_cr78": {
            "bd": ("cr78", ["bass drum", "clean"]),
            "sd": ("cr78", ["snare drum", "clean"]),
            "ch": ("cr78", ["hi hat", "ch", "clean"]),
            "oh": ("cr78", ["hi hat", "oh", "clean"]),
            "cp": ("808", ["clap", "clean"]), # CR-78 has no clap; borrow 808
            "rim": ("cr78", ["rim", "clean"]),
            "tom": ("cr78", ["bongo", "clean"]),
            "perc": ("cr78", ["cowbell", "clean"]),
        },
        "07_sp1200": {
            "bd": ("sp1200", ["bass drum"]),
            "sd": ("sp1200", ["snare drum", "clean"]),
            "ch": ("sp1200", ["hi hat", "ch", "clean"]),
            "oh": ("sp1200", ["hi hat", "oh"]),
            "cp": ("sp1200", ["clap"]),
            "rim": ("909", ["rim", "sp1200"]),
            "tom": ("sp1200", ["tom", "clean"]),
            "perc": ("sp1200", ["cowbell"]),
        },
        "08_tr606": {
            "bd": ("606", ["bass drum", "clean"]),
            "sd": ("606", ["snare drum", "clean"]),
            "ch": ("606", ["hi hat", "ch", "clean"]),
            "oh": ("606", ["hi hat", "oh", "clean"]),
            "cp": ("909", ["clap", "clean"]), # 606 has no clap; borrow 909
            "rim": ("808", ["rim", "clean"]),
            "tom": ("606", ["tom", "clean"]),
            "perc": ("606", ["cymbal", "clean"]),
        }
    }

    curated = {}
    for kit_name, slots in kit_defs.items():
        kdir = os.path.join(DRUMS_OUT, kit_name)
        os.makedirs(kdir, exist_ok=True)
        curated[kit_name] = {}
        print(f"  Kit: {kit_name}")
        for sname, (pat, kws) in slots.items():
            src_file = find_file(DRUM_SRC, pat, kws)
            if src_file:
                dst_file = os.path.join(kdir, f"{sname}.wav")
                shutil.copyfile(src_file, dst_file)
                curated[kit_name][sname] = dst_file
                print(f"    {sname:4s} -> {os.path.basename(src_file)}")
            else:
                print(f"    {sname:4s} -> [!] NOT FOUND ({pat}, {kws})")
    return curated

def curate_cycle_waves():
    print("[*] Curating Cycle Waveforms...")
    categories = {
        "01_saw": ["AKWF_bw_saw/AKWF_saw_0001.wav", "AKWF_bw_saw/AKWF_saw_0005.wav", "AKWF_bw_sawbright/AKWF_sawbright_0001.wav"],
        "02_square": ["AKWF_bw_squ/AKWF_squ_0001.wav", "AKWF_bw_squ/AKWF_squ_0005.wav", "AKWF_bw_squrounded/AKWF_squrounded_0001.wav"],
        "03_triangle": ["AKWF_bw_tri/AKWF_tri_0001.wav", "AKWF_bw_tri/AKWF_tri_0005.wav"],
        "04_sine": ["AKWF_bw_sin/AKWF_sin_0001.wav", "AKWF_sinharm/AKWF_sinharm_0001.wav"],
        "05_organ": ["AKWF_eorgan/AKWF_eorgan_0001.wav", "AKWF_eorgan/AKWF_eorgan_0025.wav"],
        "06_epiano": ["AKWF_epiano/AKWF_epiano_0001.wav", "AKWF_epiano/AKWF_epiano_0010.wav"],
        "07_bass": ["AKWF_dbass/AKWF_dbass_0001.wav", "AKWF_ebass/AKWF_ebass_0001.wav"],
        "08_chip": ["AKWF_c604/AKWF_c604_0001.wav", "AKWF_vgamebasic/AKWF_vgamebasic_0001.wav"]
    }

    for cat, flist in categories.items():
        cdir = os.path.join(WAVES_OUT, cat)
        os.makedirs(cdir, exist_ok=True)
        for rel in flist:
            sfile = os.path.join(WAVE_SRC, rel)
            if os.path.exists(sfile):
                dfile = os.path.join(cdir, os.path.basename(rel))
                shutil.copyfile(sfile, dfile)
                print(f"    {cat} -> {os.path.basename(rel)}")

def load_wav_pcm16(path):
    with wave.open(path, 'rb') as w:
        nframes = w.getnframes()
        data = w.readframes(nframes)
        samples = struct.unpack(f"<{nframes}h", data)
        return samples

def generate_embedded_header(curated_kits):
    print("[*] Generating src/rom_samples.hpp...")
    out_path = os.path.join(PROJECT_ROOT, "src", "rom_samples.hpp")
    
    # Let's take the default TR-808 kit and embed it compactly
    tr808 = curated_kits.get("01_tr808", {})
    slot_order = ["bd", "sd", "ch", "oh", "cp", "rim", "tom", "perc"]

    lines = []
    lines.append("// Auto-generated ROM factory sample kit and DX7 cartridge data")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("#include <array>")
    lines.append("#include <span>")
    lines.append("")
    lines.append("namespace groovebox::rom {")
    lines.append("")

    # Embed TR-808 samples
    for slot_idx, slot in enumerate(slot_order):
        wav_path = tr808.get(slot)
        if wav_path and os.path.exists(wav_path):
            samples = load_wav_pcm16(wav_path)
            # Limit sample length if kick or cymbal is huge, keep full transient
            max_len = 24000 if slot in ["bd", "oh", "perc"] else 12000
            samples = samples[:max_len]
            lines.append(f"inline constexpr int16_t kDrumSample_{slot}[] = {{")
            # chunk by 16 values
            for i in range(0, len(samples), 16):
                chunk = samples[i:i+16]
                str_chunk = ", ".join(str(s) for s in chunk)
                lines.append(f"    {str_chunk},")
            lines.append("};")
            lines.append(f"inline constexpr uint32_t kDrumSampleLen_{slot} = {len(samples)};")
            lines.append("")

    # Table of default drum slots
    lines.append("struct RomSampleInfo {")
    lines.append("    const int16_t* data;")
    lines.append("    uint32_t length;")
    lines.append("    uint32_t sample_rate;")
    lines.append("    const char* name;")
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr RomSampleInfo kDefaultDrumKit[8] = {")
    for slot in slot_order:
        lines.append(f"    {{kDrumSample_{slot}, kDrumSampleLen_{slot}, 48000, \"TR808_{slot.upper()}\"}},")
    lines.append("};")
    lines.append("")

    # Embed DX7 ROM1A
    rom1a_path = os.path.join(PROJECT_ROOT, "backups", "ROM1A.SYX")
    if os.path.exists(rom1a_path):
        with open(rom1a_path, "rb") as f:
            syx_data = f.read()
        lines.append("// Yamaha DX7 Factory ROM1A (4104 bytes, 32 patches)")
        lines.append(f"inline constexpr uint8_t kDx7Rom1A[{len(syx_data)}] = {{")
        for i in range(0, len(syx_data), 16):
            chunk = syx_data[i:i+16]
            str_chunk = ", ".join(f"0x{b:02X}" for b in chunk)
            lines.append(f"    {str_chunk},")
        lines.append("};")
        lines.append("")

    lines.append("} // namespace groovebox::rom")
    
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"[+] Written {out_path} ({os.path.getsize(out_path)} bytes)")

def download_extra_dx7_banks():
    print("[*] Downloading curated open-source DX7 banks...")
    import urllib.request
    banks = {
        "01_Yamaha_ROM1A.syx": "https://raw.githubusercontent.com/mmontag/dx7-synth-js/master/roms/ROM1A.SYX",
        "02_Brian_Eno.syx": "https://raw.githubusercontent.com/csantinc/DX7-repo/master/ENO.syx",
        "03_Dexed_01.syx": "https://raw.githubusercontent.com/csantinc/DX7-repo/master/Dexed_01.syx",
        "04_Synprez_Basses.syx": "https://raw.githubusercontent.com/csantinc/DX7-repo/master/SynprezFM/SynprezFM_01.syx",
        "05_Synprez_Keys.syx": "https://raw.githubusercontent.com/csantinc/DX7-repo/master/SynprezFM/SynprezFM_02.syx",
    }
    for fname, url in banks.items():
        dst = os.path.join(DX7_OUT, fname)
        if not os.path.exists(dst):
            try:
                req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
                with urllib.request.urlopen(req, timeout=5) as resp:
                    data = resp.read()
                    with open(dst, "wb") as f:
                        f.write(data)
                print(f"  Downloaded {fname} ({len(data)} bytes)")
            except Exception as e:
                print(f"  Failed {fname}: {e}")
        else:
            print(f"  {fname} already exists.")

if __name__ == "__main__":
    ensure_dirs()
    download_extra_dx7_banks()
    curated = curate_drum_kits()
    curate_cycle_waves()
    generate_embedded_header(curated)
    print("[+] All samples and ROM banks curated successfully!")
