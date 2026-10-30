# PROJECT CONTEXT: Portable Linux Groovebox DAW (Multi-Device Modular Architecture)

## 1. Підтримувані апаратні платформи та профілі (`profiles/*.ini`)
Система повністю абстрагована від конкретного одноплатника чи вендора через декларативні `.ini`-профілі (`DeviceProfile`):
* **MagicX XU10 (`profiles/magicx-xu10.ini`):** Rockchip RK3326S (`4x Cortex-A35`), 1 GB DDR4, IPS `640×480` (2× integer scale від `320×240`), ЦАП `RK817` (`hw:0`, нативні `32 000 Hz`), ROCKNIX (`Linux 7.1.2 aarch64`).
* **Trimui Brick / Trimui Brick Pro (`profiles/trimui-brick-pro.ini`):** Allwinner A133P (`4x Cortex-A53 @ 1.8 GHz`), 1–2 GB LPDDR4X, IPS **`1024×768`** (`4:3` crisp scaling від `320×240`), `audiocodec` (`44 100 Hz`), підтримка `ABS_HAT0X/Y` хрестовини та подвійних Hall-effect стіків, SD-картка `/mnt/SDCARD/Roms/groovebox`.
* **R36S / R36H / R35S (`profiles/r36s.ini`):** Rockchip RK3326 (`4x Cortex-A35`), IPS `640×480`, ЦАП `RK817`, геймпад `retrogame_joypad / OpenSimHardware`, ArkOS / AmberELEC / ROCKNIX, SD-картка `/roms/groovebox`.
* **Anbernic RG35XX / RG351 (`profiles/anbernic-*.ini`) та Desktop (`profiles/generic.ini`):** macOS / Linux x86_64 / ARM64.
* **Перевизначення через змінні середовища:**
  * `GROOVEBOX_PROFILE=<name>` — примусовий вибір профілю (наприклад, `trimui-brick-pro` або `r36s`).
  * `GROOVEBOX_DATA_DIR=<path>` — примусовий шлях до кореневої теки `oneshots/`, `loops/`, `dx7/`, `projects/`, `recordings/`.

## 2. Архітектура звукового та міді-ядра
1. **7 вибіркових звукових та MIDI-рушіїв (`kSelectableEngines` у `src/synth_engines.hpp`):**
   * `SAMPLE PLAYER` (`kSampleOneShot` / `kSlicer` із монофонічним чоком та сітками `8/16/32/64/128`),
   * `303 BASSLINE` (`kAcid303`),
   * `DX7 6-OP` (`kDx7`),
   * `CHORD PAD` (`kChord`),
   * `VIRTUAL ANALOG` (`kVaMono`),
   * `2-OP FM` (`kFm2Op`),
   * `MIDI OUT` (`kMidiOut`).
2. **Розмежування `SamplerEngine` (`[R1] SAMPLE`) та `Sample Player` (`[L2] SYNTH`):**
   * `[R1] SAMPLE` — глобальна робоча станція живого запису, ресемплінгу майстер-шини (`RecSource::kMasterResample`) або зовнішнього входу (`RecSource::kLineIn`), овердабу, нарізки транзієнтів, масштабування хвилі (`1X..8X`), призначення фрагментів на будь-яку з 16 доріжок (`assignTrimmedToTrack`) та експорту `.wav` на SD-картку (`saveTrimmedToWav`).
   * `[L2] SAMPLE PLAYER` — індивідуальний інструментальний рушій доріжки, що відтворює семпли з `SamplerEngine::trackSample(t)`.
3. **Універсальний USB Audio / MIDI та двосторонній 16-канальний MIDI (`src/hw_monitor.cpp`, `src/midi_engine.cpp`):**
   * Автоматичне виявлення будь-яких Class-Compliant USB Audio карт та USB MIDI портів без прив'язки до конкретного бренду.
   * Двосторонній ALSA RawMIDI (`O_RDWR`): вхід на всі 16 доріжок (`Note On/Off`, `CC 74/71`, `Program Change`, `24 PPQN Clock PLL`) та вихід з доріжок `MIDI OUT` (`sendNote`, `sendControlChange`, `sendProgramChange`).
4. **Автоматизоване тестування та збірка (`CMakePresets.json`, `tools/auto_test.cpp`):**
   * Пресети `desktop`, `rk3326`, `a133p`.
   * **311 автоматизованих тестів** (`./build/auto_test`) покривають DSP, секвенсор, P-Locks, семплер, ресемплінг, експорт WAV, UI-рендеринг та завантаження профілів консолей.
