# Hardware Porting & Customization Guide

**[🇬🇧 English](PORTING_GUIDE.md)** | **[🇺🇦 Українська](PORTING_GUIDE.uk.md)**

This guide explains how contributors can port the **Portable Linux Groovebox Framework** to new Linux-based handheld consoles (such as **Trimui Brick / Brick Pro**, **R36S / R36H**, **Anbernic RG35XX / RG351**, **Powkiddy RGB30**, etc.) and how to develop from **Windows**, **macOS**, or **Linux**.

---

## 1. Declarative Hardware Abstraction Layer (`profiles/*.ini`)

All device-specific details are decoupled from C++ source code into `.ini` files in [`profiles/`](../profiles/), parsed at startup by [`src/device_profile.cpp`](../src/device_profile.cpp):

1. **Device Detection (`[device]`):**
   * Reads `/proc/device-tree/model` on Linux and matches any `|`-separated token in `device.match` (case-insensitive, e.g., `match = trimui brick|tg3040|trimui|a133p`).
   * Can be overridden at runtime via the environment variable `GROOVEBOX_PROFILE=<name>`.
2. **Display Scaling (`[display]`):**
   * The internal UI canvas renders at a logical resolution of `320×240` (`4:3` aspect ratio).
   * `UiShell::init()` creates the physical KMSDRM/SDL2 window at `display.width × display.height` (e.g., `1024×768` on Trimui Brick / Brick Pro, `640×480` on MagicX XU10 / R36S) and applies `SDL_RenderSetLogicalSize(renderer, 320, 240)` and `SDL_RenderSetIntegerScale`.
3. **Input Mapping (`[input]`):**
   * Opens `/dev/input/event*` devices matching any `|`-separated token in `input.gamepad`, `input.volume`, and `input.headphones`.
   * Supports both discrete GPIO D-pad buttons (`key.up`, `key.down`, `key.left`, `key.right`) and `EV_ABS` hat axes (`ABS_HAT0X`, `ABS_HAT0Y`).
   * `has_analog_sticks = 1|0` controls analog stick polling (`ABS_X/Y/RX/RY`) and HUD stick indicators.
4. **Audio Routing (`[audio]`):**
   * Configures the internal ALSA card index (`internal_card`), native hardware sample rate (`internal_rate`, e.g., `32000` for Rockchip RK817 or `44100` for Allwinner `audiocodec`), and `amixer` controls for speaker/headphone switching.
5. **SD Card Storage (`[storage]`):**
   * Sets the default base path (`data_dir`) for `oneshots/`, `loops/`, `dx7/`, `projects/`, and `recordings/` (overridable via `GROOVEBOX_DATA_DIR`).

---

## 2. Current Handheld Profiles

| Handheld | SoC | Native Screen | Profile File | Env Override |
|:---|:---|:---|:---|:---|
| **MagicX XU10** | Rockchip RK3326S | `640×480` | [`profiles/magicx-xu10.ini`](../profiles/magicx-xu10.ini) | `GROOVEBOX_PROFILE=magicx-xu10` |
| **Trimui Brick / Brick Pro** | Allwinner A133P | `1024×768` | [`profiles/trimui-brick-pro.ini`](../profiles/trimui-brick-pro.ini) | `GROOVEBOX_PROFILE=trimui-brick-pro` |
| **R36S / R36H / R35S** | Rockchip RK3326 | `640×480` | [`profiles/r36s.ini`](../profiles/r36s.ini) | `GROOVEBOX_PROFILE=r36s` |

---

## 3. Cross-Compiling from Windows, macOS, or Linux

The repository uses **Zig (`zig c++`)** as a zero-Docker cross-compiler targeting `aarch64-linux-gnu`.

### Windows (PowerShell / Command Prompt with Ninja + Zig)
```powershell
# Cross-compile for Rockchip RK3326 (MagicX XU10 / R36S):
cmake --preset rk3326-ninja
cmake --build --preset rk3326-ninja

# Cross-compile for Allwinner A133P (Trimui Brick / Brick Pro):
cmake --preset a133p-ninja
cmake --build --preset a133p-ninja
```

### macOS / Linux / WSL2
```bash
# Cross-compile for Rockchip RK3326 (MagicX XU10 / R36S):
cmake --preset rk3326 && cmake --build --preset rk3326

# Cross-compile for Allwinner A133P (Trimui Brick / Brick Pro):
cmake --preset a133p && cmake --build --preset a133p
```

> **Note on Target `glibc` Version:**
> By default, [`cmake/zig-cxx-aarch64.sh`](../cmake/zig-cxx-aarch64.sh) and [`cmake/zig-cxx-aarch64.bat`](../cmake/zig-cxx-aarch64.bat) target `aarch64-linux-gnu.2.38` (ROCKNIX / mainline Linux). If your target OS uses an older `glibc` (for example, Ubuntu 20.04 on ArkOS for R36S with `glibc 2.31`), change the target suffix in `zig-cxx-aarch64.sh` / `.bat` to `aarch64-linux-gnu.2.31`.

---

## 4. Step-by-Step Calibration on a New Handheld

When testing on a new handheld over SSH or ADB:

### Step 1: Check the Device Tree Model String
```bash
tr -d '\0' < /proc/device-tree/model ; echo
```
Add a matching substring to `device.match` in your `profiles/<device>.ini`.

### Step 2: Verify `evdev` Input Devices & Keycodes
```bash
cat /proc/bus/input/devices
evtest
```
* Add the gamepad device name substring to `input.gamepad` (separated by `|`).
* Press `A, B, X, Y, L1, R1, L2, R2, SELECT, START` in `evtest` and update the hex keycodes (`0x130..0x13b`) in `[input]`.
* D-pads emitting `ABS_HAT0X` (`code 16`) and `ABS_HAT0Y` (`code 17`) work automatically without keycode mapping.

### Step 3: Check ALSA Audio Card & Mixer Controls
```bash
cat /proc/asound/cards
amixer -c 0 scontrols
```
Update `audio.internal_card`, `audio.internal_rate`, and `audio.mux_control` in your profile.

### Step 4: Launch Script Template (`Groovebox.sh` for SD Card `Ports/`)
```bash
#!/bin/bash
DIR="$(cd "$(dirname "$0")/groovebox" && pwd)"
cd "$DIR"

export GROOVEBOX_PROFILE="trimui-brick-pro"
export GROOVEBOX_DATA_DIR="$DIR"
export SDL_VIDEODRIVER=kmsdrm

./xu10_groovebox 2>&1 | tee /tmp/groovebox.log
```
