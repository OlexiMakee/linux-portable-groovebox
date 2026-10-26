#!/bin/bash
set -e

KEY="backups/xu10_id_ed25519"
HOST="root@10.1.1.2"
LOCAL_DIR="/tmp/xu10_tour"
mkdir -p "${LOCAL_DIR}"

echo "=== Running remote capture tour on MagicX XU10 ==="

ssh -i "${KEY}" "${HOST}" 'python3 - << "EOF"
import struct, time, os

def send_key(code, duration=0.06):
    with open("/dev/input/event3", "wb") as f:
        ev_down = struct.pack("llHHI", 0, 0, 1, code, 1)
        ev_syn = struct.pack("llHHI", 0, 0, 0, 0, 0)
        f.write(ev_down + ev_syn)
        f.flush()
        time.sleep(duration)
        ev_up = struct.pack("llHHI", 0, 0, 1, code, 0)
        f.write(ev_up + ev_syn)
        f.flush()
        time.sleep(0.10)

def grab_fb(filename):
    time.sleep(0.15)
    with open("/dev/fb0", "rb") as f:
        raw = f.read(640 * 480 * 4)
    filesize = 54 + len(raw)
    bmp_header = struct.pack("<2sIHHI", b"BM", filesize, 0, 0, 54)
    dib_header = struct.pack("<IiiHHIIIIII", 40, 640, -480, 1, 32, 0, len(raw), 2835, 2835, 0, 0)
    with open(filename, "wb") as out:
        out.write(bmp_header + dib_header + raw)
    print(f"Captured: {filename}")

# Key codes:
L1 = 0x136
L2 = 0x139
R1 = 0x137
R2 = 0x138
FN = 0x2c0
UP = 0x220
DOWN = 0x221
LEFT = 0x222
RIGHT = 0x223
BTN_A = 0x131
BTN_B = 0x133

# 1. Capture current screen (ensure in SEQ first by pressing L1 if in SYNTH)
# Send L1 twice to settle
send_key(L1)
time.sleep(0.2)
grab_fb("/tmp/xu10_01_seq.bmp")

# 2. Press L1 -> SYNTH tab
send_key(L1)
grab_fb("/tmp/xu10_02_synth.bmp")

# 3. Press L2 -> FX tab
send_key(L2)
grab_fb("/tmp/xu10_03_fx.bmp")

# 4. Press R1 -> MOD tab (Dual LFO & Mini-Scope)
send_key(R1)
grab_fb("/tmp/xu10_04_mod.bmp")

# 5. Press R2 -> MIX tab
send_key(R2)
grab_fb("/tmp/xu10_05_mix.bmp")

# 6. Press FN -> SYS tab
send_key(FN)
grab_fb("/tmp/xu10_06_sys.bmp")

# 7. In SYS: Press Down -> select USB Role
send_key(DOWN)
send_key(BTN_A)
grab_fb("/tmp/xu10_07_sys_toggled.bmp")

# Return to SEQ
send_key(FN)
send_key(L1)
print("Tour finished!")
EOF'

echo "=== Downloading screenshots to ${LOCAL_DIR} ==="
scp -i "${KEY}" "${HOST}:/tmp/xu10_0*.bmp" "${LOCAL_DIR}/"

for f in "${LOCAL_DIR}"/*.bmp; do
    sips -s format png "${f}" --out "${f%.bmp}.png" >/dev/null
done

echo "=== Tour Complete! Captured screenshots in ${LOCAL_DIR}: ==="
ls -la "${LOCAL_DIR}"/*.png
