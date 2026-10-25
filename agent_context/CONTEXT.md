# PROJECT CONTEXT: Custom Linux Groovebox / Synth Appliance (MagicX XU10 + Elektron Digitakt II)

## 1. Апаратна платформа та цільова конфігурація
* **Консоль:** MagicX XU10 (Rockchip RK3326S, 4x Cortex-A35 `aarch64`, 1 GB DDR4, Mali-G31 MP2 `Panfrost`, IPS 640x480 @ 60Hz, PMIC/DAC `RK817` `hw:0` `rk817int`, Gamepad `/dev/input/event3` `XU10 Gamepad`, Volume `/dev/input/event2` `gpio-keys-vol`).
* **ОС:** ROCKNIX (`20261001-a`, Linux `7.1.2 #1 SMP PREEMPT aarch64`, `/` SquashFS Read-Only, `/storage` ext4 27.4 GB Read-Write на Apacer 32 GB).
* **Зовнішній секвенсор/семплер:** Elektron Digitakt II (`USB AUDIO/MIDI` Class Compliant, **48 000 Hz** fixed, буфер 64–128 семплів, синхронізація по `0xF8` 24 PPQN із програмним PLL, транспорт `0xFA`/`0xFC`/`0xFB`).
* **Робоча директорія на консолі:** `/storage/dev/groovebox/`.
* **USB-SSH канал:** Інтерфейс `en9` (`10.1.1.1/24` на MacBook $\leftrightarrow$ `10.1.1.2/24` на XU10 `gadget`), безпарольний SSH-ключ `backups/xu10_id_ed25519`, динамічний Role Switch через `/sys/class/usb_role/ff300000.usb-role-switch/role` (`device` $\leftrightarrow$ `host`).

## 2. Поточний статус: Step 1 + Milestone 1..5 — ЗАВЕРШЕНО ТА ЗАПУЩЕНО НА XU10
1. **Step 1 (USB-SSH Development Mode):**
   * Встановлено та пропатчено свіжий образ `ROCKNIX-RK3326.aarch64-20261001-a.img` на карту Apacer 32 GB (`rk3326-magicx-xu10.dtb`: `/usb@ff300000 dr_mode = "otg"`, `role-switch-default-mode = "peripheral"`, `extlinux.conf`: додано `ssh`).
   * Переведено USB Gadget CDC-NCM (`/storage/.cache/usbgadget/network.conf`) на підмережу `10.1.1.2/24` (`HOSTIP=10.1.1.1`), безпарольний SSH-ключ `backups/xu10_id_ed25519` прописаний у `/storage/.ssh/authorized_keys` (root-пароль консолі: `1111`).
2. **Milestone 1 (Project Skeleton & Build System):**
   * Встановлено `memory_os` у локальний `venv_auto/` та `cmake`, `sdl2`, `zig` (v0.17.0) через Homebrew.
   * Стягнуто `sysroot/usr/lib` (`libSDL2-2.0.so.0.3200.10`, `libasound.so.2.0.0`, `glibc 2.41`) із консолі та налаштовано крос-компіляцію `cmake/aarch64-rocknix-zig.cmake` (`zig c++ -target aarch64-linux-gnu.2.38`) і деплой `scripts/deploy_xu10.sh`.
3. **Milestone 2 (Hardware Monitor & Input Test, Elektron/M8 Minimalist UI):**
   * Реалізовано [config.hpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/config.hpp), [hw_monitor.cpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/hw_monitor.cpp), [input_manager.cpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/input_manager.cpp), [ui_renderer.cpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/ui_renderer.cpp), [main.cpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/main.cpp).
   * Графічне ядро: основний режим **`320×240`** з компактним піксельним шрифтом `1×` (80 символів у рядку) + гаряче перемикання у **`160×120`** за комбінацією `SELECT + Y`, адаптивний `0 / 15 / 24 FPS` (Dirty-Flag Redraw) та 8-кольорова палітра без альфа-блендингу.
4. **Milestone 3 (Real-Time ALSA Audio Engine @ 32k/48k & Auto Speaker/HP/USB Routing):**
   * Реалізовано [audio_engine.hpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/audio_engine.hpp) та [audio_engine.cpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/audio_engine.cpp) напряму поверх `libasound.so.2` (`RW_INTERLEAVED`, `S16_LE`, `period_size = 128`, `buffer_size = 512`).
   * **Апаратна знахідка тактового генератора I2S `RK817` (`hw:0,0`):** при запиті `24 000 Hz` драйвер `rk817_int` видає попередження і примусово виставляє апаратний дільник **`32 000 Hz`** (`16000 / 32000 / 44100 / 48000 Hz`). Тому для локального режиму (`RK817`) використано точний нативний режим **`32 000 Hz`** (на 33% легше за 48 кГц без програмного ресемплінгу ALSA!), а при підключенні USB Audio (`Digitakt II`) — автоматичне перемикання на **`48 000 Hz`**.
   * Автоматичне перемикання **Динамік (`SPK`) $\leftrightarrow$ Навушники (`HP`)** через `/dev/input/event1` (`SW_HEADPHONE_INSERT`) та `amixer -c 0 cset name='Playback Mux' 'SPK'/'HP'`.
5. **Milestone 4 & 5 (Modular Synth Engine Registry, 16-Step Sequencer with Parameter Locks, Stereo Ping-Pong Delay & MIDI Clock PLL):**
   * Реалізовано модульний реєстр движків у [synth_engines.hpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/synth_engines.hpp): 5 взаємозамінних синтезаторних движків (`VA SAW+SUB`, `2-OP FM`, `ACID 303`, `CHORD PAD`, `ANALOG DRUM`), кожен з яких може бути призначений на будь-яку з 4 доріжок на вкладці `[L2] SYNTH` та автоматично експортує 6 іменованих параметрів `p[0..5]` у UI.
   * Додано майстер-шину **Stereo Ping-Pong Delay** та індивідуальні `Level`, `Mute`, `Delay Send` на кожну доріжку на вкладці `[R2] MIX/FX`.
   * Реалізовано інтерактивний 16-кроковий секвенсор із підтримкою **Parameter Locks (P-Lock фільтра на крок)**, транспонуванням нот (`D-Pad Up/Down`) та живим осцилографом на вкладці `[R1] SCOPE`.
   * Реалізовано [midi_engine.hpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/midi_engine.hpp) та [midi_engine.cpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/midi_engine.cpp): неблокуючий ALSA RawMIDI рідер із програмним **PLL-фільтром 24 PPQN (`0xF8`)** для стабільного вимірювання BPM та синхронізації транспорту (`0xFA`/`0xFB`/`0xFC`) і нот по MIDI-каналах `1..4` від **Elektron Digitakt II**.
   * Загальне навантаження CPU на MagicX XU10 становить лише **~4.5%** (`91.7% idle`).

## 3. Збережені архітектурні пропозиції (Очікують затвердження після перевірки SSH)

```text
Problem A: Стек мови, збірки та крос-компіляції (macOS arm64 -> ROCKNIX aarch64)
  A1. Rust (cargo + cross/Docker) — висока безпека пам'яті / складніший лінкінг із системним KMSDRM SDL2 на SquashFS ROCKNIX, потреба у Docker VM, немає нативного DaisySP.
  A2. C++20 + CMake + Zig C++ (`zig c++ -target aarch64-linux-gnu`) — один легкий бінарник крос-компілятора без Docker, нативна сумісність із miniaudio/DaisySP, можливість задати точну версію glibc ROCKNIX (Recommended)
  A3. C++20 + CMake у Docker-контейнері (linux/arm64) — повне Linux-оточення збірки / накладні витрати на запуск Docker та повільніший цикл ітерацій.

Problem B: Підсистема графіки (640x480 @ 60Hz) та читання фізичних контролерів XU10
  B1. Чистий SDL2 (SDL_Renderer + SDL_GameController на обох платформах) — менше коду / ризик втрати мапінгу кнопок і стіків XU10 після зупинки `emustation`.
  B2. SDL2 для рендеру (Cocoa на macOS / KMSDRM на ROCKNIX) + нативний неблокуючий читач Linux `evdev` (`/dev/input/event*`) на консолі з клавіатурним фолбеком на macOS — гарантований доступ до всіх кнопок і raw-осей АЦП стіків з мінімальним джитером (Recommended)

Problem C: Реалізація Hardware & Hot-Plug Monitor (Milestone 2 -> Milestone 3)
  C1. Періодичний виклик CLI-утиліт (`aplay -l`, `amidi -l`) через `popen()` — швидка реалізація / блокування та спайки CPU від створення підпроцесів.
  C2. Нативний C++ поллінг віртуальних ФС ядра (`/proc/asound/cards`, `/proc/asound/pcm`, `/proc/asound/seq/clients`, `/sys/class/udc/*`) в окремому фоновому потоці + симуляція станів (Mock Hot-Plug за хоткеєм) на Desktop — нульовий вплив на RT-аудіопотік і можливість тестувати UI перемикання `RK817 <-> Digitakt II` прямо на Mac (Recommended)

Problem D: Інтеграція Memory OS (`OlexiMakee/memory_os`) та дозвіл на пакети
  D1. Розгорнути `memory_os` у локальному `venv_auto/`, створити `INDEX.md`, `PROJECT_CONTEXT.md`, `agent_context/`, ініціалізувати специфікації майлстоунів через `memory_os spec init`, та встановити через Homebrew відсутні утиліти збірки (`brew install cmake sdl2 zig`) — повна відповідність контракту проєкту (Recommended)
  D2. Розгорнути лише файлову структуру документації (`INDEX.md`, `agent_context/`) без встановлення CLI `memory_os` у `venv_auto/`.
```
