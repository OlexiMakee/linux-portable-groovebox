# Посібник із портування та кастомізації заліза (Українська)

**[🇬🇧 English](PORTING_GUIDE.md)** | **[🇺🇦 Українська](PORTING_GUIDE.uk.md)**

Цей документ описує процес адаптації **Portable Linux Groovebox Framework** під нові портативні Linux-консолі (**Trimui Brick / Brick Pro**, **R36S / R36H / R35S**, **Anbernic RG35XX / RG351** тощо) та налаштування середовища розробки на **Windows**, **macOS** і **Linux**.

---

## 1. Декларативний шар абстракції заліза (`profiles/*.ini`)

Усі апаратні особливості консолей винесені з C++ коду в `.ini` файли в каталозі [`profiles/`](../profiles/), які зчитуються на старті в [`src/device_profile.cpp`](../src/device_profile.cpp):

1. **Автовизначення пристрою (`[device]`):**
   * Читає `/proc/device-tree/model` та шукає збіг із будь-яким токеном у `device.match`, розділеним символом `|` (наприклад, `match = trimui brick|tg3040|trimui|a133p`).
   * Може бути примусово перевизначено через змінну середовища `GROOVEBOX_PROFILE=<name>`.
2. **Масштабування дисплея (`[display]`):**
   * Внутрішнє логічне полотно рендерера має розмір `320×240` (`4:3`).
   * `UiShell::init()` відкриває KMSDRM/SDL2 вікно з фізичною роздільною здатністю `display.width × display.height` (`1024×768` на Trimui Brick / Brick Pro, `640×480` на MagicX XU10 / R36S) і застосовує `SDL_RenderSetLogicalSize(renderer, 320, 240)` та `SDL_RenderSetIntegerScale`.
3. **Ввід та контролери (`[input]`):**
   * Відкриває `/dev/input/event*` за списком підрядків через `|` у полях `input.gamepad`, `input.volume`, `input.headphones`.
   * Підтримує як цифрові кнопки D-pad (`EV_KEY`), так і `EV_ABS` хрестовини (`ABS_HAT0X`, `ABS_HAT0Y`).
   * `has_analog_sticks = 1|0` вмикає/вимикає опитування аналогових осей та індикатори стіків у HUD.
4. **Аудіо та ALSA (`[audio]`):**
   * Задає номер вбудованої карти (`internal_card`), її нативну частоту дискретизації (`internal_rate`, наприклад `32000` для RK817 або `44100` для Allwinner `audiocodec`) та контролери `amixer` для перемикання динамік/навушники.
5. **Шляхи на SD-картці (`[storage]`):**
   * Визначає базову теку даних (`data_dir`) для `oneshots/`, `loops/`, `dx7/`, `projects/`, `recordings/` (або перевизначається через `GROOVEBOX_DATA_DIR`).

---

## 2. Крос-компіляція з Windows, macOS та Linux

Для крос-компіляції під `aarch64-linux-gnu` використовується **Zig (`zig c++`)** без потреби в Docker.

### Windows (PowerShell / CMD + Ninja + Zig)
```powershell
# Крос-компіляція для Rockchip RK3326 (MagicX XU10 / R36S):
cmake --preset rk3326-ninja
cmake --build --preset rk3326-ninja

# Крос-компіляція для Allwinner A133P (Trimui Brick / Brick Pro):
cmake --preset a133p-ninja
cmake --build --preset a133p-ninja
```

### macOS / Linux / WSL2
```bash
cmake --preset rk3326 && cmake --build --preset rk3326
cmake --preset a133p  && cmake --build --preset a133p
```

---

## 3. Покрокове калібрування на новій консолі

1. **Перевірка моделі пристрою:** `tr -d '\0' < /proc/device-tree/model ; echo`
2. **Перевірка `evdev` геймпада та кодів кнопок:** запустіть `evtest`, перевірте назву геймпада та шестнадцяткові коди кнопок `0x130..0x13b` у секції `[input]`.
3. **Перевірка ALSA карт:** `cat /proc/asound/cards` та `amixer -c 0 scontrols`.
4. **Стартовий скрипт `Groovebox.sh` для теки `Ports/` на SD-картці:**
   ```bash
   #!/bin/bash
   DIR="$(cd "$(dirname "$0")/groovebox" && pwd)"
   cd "$DIR"

   export GROOVEBOX_PROFILE="trimui-brick-pro"
   export GROOVEBOX_DATA_DIR="$DIR"
   export SDL_VIDEODRIVER=kmsdrm

   ./xu10_groovebox 2>&1 | tee /tmp/groovebox.log
   ```
