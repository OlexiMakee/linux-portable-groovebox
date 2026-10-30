# Portable Linux Groovebox DAW — Root Index

> **Mandatory Ingestion Path:** `INDEX.md` $\rightarrow$ `README.md` $\rightarrow$ `agent_context/CONTEXT.md`.

## Навігація проєкту
* [README.md](README.md) — Архітектура універсального портативного Linux DAW, візуальний тур, опис 7 звукових рушіїв, розділення `SAMPLE` vs `SAMPLE PLAYER`, пресети збірки (`CMakePresets.json`) та інструкція портування на нові портативні консолі (`Trimui Brick Pro`, `R36S`, `MagicX XU10`).
* [CONTEXT.md](agent_context/CONTEXT.md) — Поточний архітектурний стан, мульти-девайсна система профілів (`profiles/*.ini`), універсальна підтримка USB Class-Compliant Audio/MIDI та двосторонній 16-канальний MIDI-роутинг.
* [development_log.md](agent_context/development_log.md) — Хронологічний журнал змін, рефакторингу та апаратних патчів.
* [profiles/](profiles/) — Декларативні `.ini` профілі апаратних платформ (`magicx-xu10.ini`, `trimui-brick-pro.ini`, `r36s.ini`, `anbernic-rg35xx.ini`, `anbernic-rg351.ini`, `generic.ini`).
* [CMakePresets.json](CMakePresets.json) — Стандартні пресети збірки для `desktop`, `rk3326` (MagicX XU10 / R36S) та `a133p` (Trimui Brick / Brick Pro).
* [patch_dtb_usb_peripheral.py](scripts/patch_dtb_usb_peripheral.py) — Скрипт патчингу FDT (`rk3326-magicx-xu10.dtb`) для активації USB Gadget (`peripheral`) та `u2phy_otg`.
