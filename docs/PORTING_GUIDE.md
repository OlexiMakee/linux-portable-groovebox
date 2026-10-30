# Hardware Porting & Multi-Device Development Guide

Цей посібник призначений для розробників та контриб'юторів, які запускають або портують **Portable Linux Groovebox DAW** на інші портативні Linux-консолі (**Trimui Brick / Brick Pro**, **R36S / R36H / R35S**, **Anbernic RG35XX / RG351**, **Powkiddy RGB30** тощо) або десктопні системи.

---

## 1. Модульна архітектура абстракції заліза (HAL)

Усі платформо-залежні параметри винесені з C++ коду в декларативні конфігураційні файли [`profiles/*.ini`](../profiles/), які зчитуються на старті в [`src/device_profile.cpp`](../src/device_profile.cpp):

1. **Дисплей (`[display]`):**
   * Внутрішнє логічне полотно рендерера завжди працює у `320×240` (`160×120` у Low-Res режимі).
   * `UiShell::init()` створює фізичне KMSDRM/SDL2 вікно з розмірами `display.width × display.height` (наприклад, `1024×768` на Trimui Brick Pro або `640×480` на MagicX XU10 / R36S) і застосовує `SDL_RenderSetLogicalSize(renderer, 320, 240)` та `SDL_RenderSetIntegerScale`.
2. **Ввід (`[input]`):**
   * `InputManager` відкриває `/dev/input/event*` у неблокуючому режимі (`O_RDONLY | O_NONBLOCK`), зіставляючи назву пристрою зі списком підрядків через `|` (наприклад, `gamepad = trimui-input|Xbox 360|Gamepad|joypad`).
   * Хрестовина (D-pad) підтримує як дискретні коди `EV_KEY` (`key.up`, `key.down`, `key.left`, `key.right`), так і осі `EV_ABS` (`ABS_HAT0X`, `ABS_HAT0Y`).
   * Прапорець `has_analog_sticks = 1|0` керує опитуванням осей `ABS_X/Y/RX/RY` та відображенням індикаторів стіків у нижньому HUD.
3. **Аудіо та ЦАП (`[audio]`):**
   * Визначає індекс вбудованої звукової карти ALSA (`internal_card`), нативну частоту дискретизації ЦАП без програмного ресемплінгу (`internal_rate`, наприклад `32000` для RK817 або `44100` для Allwinner `audiocodec`) та назви контролерів `amixer` для автоматичного перемикання динамік/навушники (`mux_control`, `spk_value`, `hp_value`).
4. **Файлова система (`[storage]`):**
   * Задає шлях до папки даних на SD-картці (`data_dir`), де лежать підпапки `oneshots/`, `loops/`, `dx7/`, `projects/`, `recordings/`.

---

## 2. Готові профілі консолей

| Консоль | SoC | Екран | Профіль | Запуск із примусовим профілем |
|:---|:---|:---|:---|:---|
| **MagicX XU10** | Rockchip RK3326S | `640×480` | [`profiles/magicx-xu10.ini`](../profiles/magicx-xu10.ini) | `GROOVEBOX_PROFILE=magicx-xu10` |
| **Trimui Brick / Brick Pro** | Allwinner A133P | `1024×768` | [`profiles/trimui-brick-pro.ini`](../profiles/trimui-brick-pro.ini) | `GROOVEBOX_PROFILE=trimui-brick-pro` |
| **R36S / R36H / R35S** | Rockchip RK3326 | `640×480` | [`profiles/r36s.ini`](../profiles/r36s.ini) | `GROOVEBOX_PROFILE=r36s` |
| **Desktop / Generic** | x86_64 / ARM64 | `640×480` | [`profiles/generic.ini`](../profiles/generic.ini) | `GROOVEBOX_PROFILE=generic` |

---

## 3. Збірка під різні платформи (`CMakePresets.json`)

Проєкт містить готові пресети CMake для десктопу та ARM64-консолей:

```bash
# 1. Нативна збірка на ПК (macOS / Linux Desktop) + запуск 311 автотестів:
cmake --preset desktop
cmake --build --preset desktop
./build/auto_test

# 2. Крос-компіляція для Rockchip RK3326 (MagicX XU10 / R36S / RG351):
cmake --preset rk3326
cmake --build --preset rk3326

# 3. Крос-компіляція для Allwinner A133P (Trimui Brick / Trimui Brick Pro):
cmake --preset a133p
cmake --build --preset a133p
```

> **Примітка щодо крос-компіляції на Linux / macOS:**
> Тучейн [`cmake/aarch64-rocknix-zig.cmake`](../cmake/aarch64-rocknix-zig.cmake) використовує `zig c++ -target aarch64-linux-gnu.2.38`. Якщо на цільовій прошивці використовується старіша версія `glibc` (наприклад, Ubuntu 20.04 в ArkOS на R36S із `glibc 2.31`), достатньо змінити цільовий таргет у [`cmake/zig-cxx-aarch64.sh`](../cmake/zig-cxx-aarch64.sh) на `aarch64-linux-gnu.2.31`.

---

## 4. Покрокова інструкція калібрування на новому пристрої (Trimui Brick Pro / R36S)

Коли ви вперше запускаєте бінарник на консолі по SSH або через скрипт запуску в `Ports/`:

### Крок 1: Перевірка моделі пристрою
```bash
tr -d '\0' < /proc/device-tree/model ; echo
```
Переконайтесь, що один із токенів у `device.match` вашого `.ini`-профілю збігається з виведеним рядком (регістр не має значення). Або передайте змінну середовища `GROOVEBOX_PROFILE=trimui-brick-pro`.

### Крок 2: Перевірка `evdev` геймпада та кодів кнопок
```bash
cat /proc/bus/input/devices
# Або інтерактивно:
evtest
```
* Знайдіть назву геймпада (`Name="..."`) і додайте її через `|` у `input.gamepad` вашого `.ini`-файлу.
* Натисніть кнопки `A, B, X, Y, L1, R1, L2, R2, SELECT, START` та перевірте їхні шестнадцяткові коди `0x130..0x13b` у секції `[input]`.
* Якщо хрестовина (D-pad) передає події `ABS_HAT0X` (`code 16`) та `ABS_HAT0Y` (`code 17`), вони підхоплюються автоматично.

### Крок 3: Перевірка ALSA карт та мікшера
```bash
cat /proc/asound/cards
amixer -c 0 scontrols
```
* Вкажіть номер вбудованої карти в `audio.internal_card` (зазвичай `0`) та її нативну частоту дискретизації в `audio.internal_rate` (`32000` для RK817, `44100` або `48000` для Allwinner `audiocodec`).

### Крок 4: Шаблон стартового скрипту для SD-картки (`Groovebox.sh` для Ports)
Для запуску з меню `Ports` на ArkOS (R36S), CrossMix / TrimUI OS (Trimui Brick Pro) або ROCKNIX:

```bash
#!/bin/bash
DIR="$(cd "$(dirname "$0")/groovebox" && pwd)"
cd "$DIR"

# Опціонально: вказати профіль та папку даних на SD-картці
export GROOVEBOX_PROFILE="trimui-brick-pro"
export GROOVEBOX_DATA_DIR="$DIR"
export SDL_VIDEODRIVER=kmsdrm

./xu10_groovebox 2>&1 | tee /tmp/groovebox.log
```
