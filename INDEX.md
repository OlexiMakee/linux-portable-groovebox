# Portable Linux Groovebox Framework — Root Index

> **Mandatory Ingestion Path:** `INDEX.md` $\rightarrow$ `README.md` $\rightarrow$ `agent_context/CONTEXT.md`.

## Project Navigation
* [README.md](README.md) — Primary English documentation: Modular C++20 Groovebox Framework overview, engine maturity status, `SAMPLE PLAYER` vs `AUDIO-RECORDING` (`[R1] AUDIO-REC`), Windows/macOS/Linux build workflows, and MagicX XU10 hardware benchmarks.
* [docs/README.uk.md](docs/README.uk.md) — Українська версія основної документації проєкту.
* [docs/PORTING_GUIDE.md](docs/PORTING_GUIDE.md) — Hardware porting & calibration guide (English) for Trimui Brick Pro, R36S, and other Linux handhelds.
* [docs/PORTING_GUIDE.uk.md](docs/PORTING_GUIDE.uk.md) — Посібник із портування та калібрування заліза (Українська).
* [agent_context/CONTEXT.md](agent_context/CONTEXT.md) — Current architectural state, multi-device HAL profiles (`profiles/*.ini`), universal USB Audio/MIDI, and bidirectional 16-channel MIDI routing.
* [agent_context/development_log.md](agent_context/development_log.md) — Chronological development log and hardware patches.
* [profiles/](profiles/) — Declarative `.ini` hardware profiles (`magicx-xu10.ini`, `trimui-brick-pro.ini`, `r36s.ini`, `generic.ini`).
* [CMakePresets.json](CMakePresets.json) — Build presets for `desktop`, `windows-ninja`, `rk3326` / `rk3326-ninja` (MagicX XU10 / R36S), and `a133p` / `a133p-ninja` (Trimui Brick / Brick Pro).
