# Linux Portable Groovebox

Lightweight multi-timbral groovebox / synth for cheap **Linux handheld consoles** (RK3326 / RK3566 / H700 class, ROCKNIX / ArkOS / Knulli style firmwares).

Reference device: **MagicX XU10** (RK3326S, 1 GB RAM, 640×480) paired over USB with **Elektron Digitakt II** (USB Audio 48 kHz + MIDI Clock). Any class-compliant USB audio / MIDI device is a target.

## Design goals
* **Audio first:** minimal CPU on UI (320×240 logical, dirty-flag 0/15/24 FPS, 8-colour palette) so DSP gets the headroom.
* **Direct ALSA**, no PipeWire in the audio path. 32 kHz on the internal codec, 48 kHz on USB audio.
* **Class-compliant USB host:** USB audio (S32/S24/S16 auto-format) + up to 8 RawMIDI ports, MIDI Clock PLL, transport, notes, CC.
* **Modular:** synth engines self-describe their parameters; UI never hardcodes engine params.
* **Portable:** device-specific details (input codes, USB role switch, VBUS control, mixer controls) are meant to live in per-device profiles — see *Porting*.

## Layout
| Path | Purpose |
|---|---|
| `src/` | C++20 app: `audio_engine`, `synth_engines`, `midi_engine`, `hw_monitor`, `input_manager`, `ui_renderer` |
| `cmake/` | Zig-based aarch64 cross toolchain (no Docker) |
| `scripts/` | Deploy, DTB patchers, SD flashing, USB-SSH setup |
| `agent_context/` | Project memory: `CONTEXT.md`, `development_log.md` |

## Build
```bash
# macOS desktop preview (SDL2)
cmake -S . -B build-macos && cmake --build build-macos -j4

# Cross-compile + deploy to device over USB-SSH
bash scripts/deploy_xu10.sh --run
```
`sysroot/` (device `libSDL2`, `libasound`, headers) is not committed — pull it from your device.

## Controls (default profile)
* `L1 / L2 / R2 / R1` — tabs HW/SYS, SYNTH, MIX/FX, SCOPE/SEQ
* `START` — play/stop · `FN` or `SELECT+B` — USB role HOST ⇄ DEVICE · `SELECT+Y` — 320×240 ⇄ 160×120 (dev)

## Porting
Currently hardcoded for XU10 (to be moved into device profiles):
* evdev nodes / key codes (`input_manager.cpp`)
* USB role switch path `ff300000.usb-role-switch`, RK817 VBUS register `0xb4` (`hw_monitor.cpp`)
* `Playback Mux` / `Master Playback Volume` mixer controls (`audio_engine.cpp`)

## Status
Pre-alpha. See `agent_context/development_log.md`.
