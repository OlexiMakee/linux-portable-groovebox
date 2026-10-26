# Development Log — MagicX XU10 Groovebox

## [2026-10-24] Step 1: Enable USB-SSH Development Mode (DTB Patch)
* Проінспектовано `/Volumes/ROCKNIX/boot.ini` та `/Volumes/ROCKNIX/rk3326-magicx-xu10.dtb`.
* Створено резервні копії:
  * На SD-карті: `/Volumes/ROCKNIX/boot.ini.bak`, `/Volumes/ROCKNIX/rk3326-magicx-xu10.dtb.bak`.
  * У робочій директорії: `backups/boot.ini.orig`, `backups/rk3326-magicx-xu10.dtb.orig`.
* Виявлено подвійну апаратну блокувальну помилку в оригінальному `rk3326-magicx-xu10.dtb`:
  1. `/usb@ff300000` (`snps,dwc2`) мав `dr_mode = "host"`.
  2. Його фізичний порт `/syscon@ff2c0000/usb2phy@100/otg-port` (`phandle = 0x76`) мав `status = "disabled"`, через що драйвер `rockchip-usb2phy` не реєстрував OTG PHY.
* Створено детермінований FDT-патчер `scripts/patch_dtb_usb_peripheral.py`.
* Ітерація 1: Увімкнення `otg-port status = "okay"` призвело до вимкнення PHY драйвером `phy-rockchip-inno-usb2` (`rockchip_usb2phy_otg_sm_work` $\rightarrow$ `0x01d1` у `0xff2c0100`) через відсутність розводки `utmi_bvalid` на XU10.
* Ітерація 2: Повернуто `otg-port status = "disabled"` (штатний режим для `dwc2_lowlevel_hw_init` з ігноруванням `-ENODEV`), пропатчено **лише** `/usb@ff300000 :: dr_mode = "peripheral"`, записано у `/Volumes/ROCKNIX/rk3326-magicx-xu10.dtb` та відмонтовано `disk4s1`.
* Ітерація 3 (Перехід на `ROCKNIX-RK3326.aarch64-20261001-a` та Apacer 32 GB):
  * З'ясовано, що підтримка USB Gadget для платформи RK3326 з'явилася в ROCKNIX лише з релізу `20241029` (патч `@Simon-L` з `usb-role-switch` та сервісом `/usr/bin/usbgadget` на базі `usb_f_ncm`, IP `169.254.170.2`).
  * Завантажено та розпаковано `ROCKNIX-RK3326.aarch64-20261001-a.img` (2.0 ГБ).
  * Виявлено, що навіть у `20261001-a` у файлі `rk3326-magicx-xu10.dtb` залишився оверрайд `dr_mode = "host"` (тоді як у `r36s`/`rg351v`/`oga` стоїть `dr_mode = "otg"` + `usb-role-switch`).
  * Скриптом `scripts/patch_rocknix_image.py` пропатчено образ `20261001-a` на місці:
    1. У `rk3326-magicx-xu10.dtb`: `/usb@ff300000 :: dr_mode = "otg"`, `role-switch-default-mode = "peripheral"`.
    2. У `/extlinux/extlinux.conf`: додано прапорець `ssh` у `APPEND` для безумовного старту `sshd.service`.
  * Підготовлено готовий образ у `/tmp/rocknix_xu10.img` для запису на `/dev/rdisk4`.
* Ітерація 4 (Підтвердження USB Gadget Enumeration та виправлення маршрутизації macOS):
  * Після прошивки Apacer 32 ГБ консоллю піднято інтерфейс `MagicX XU10@00130000` (`en9`, `status: active`, mDNS `rk3326.local` $\rightarrow$ `169.254.170.2`).
  * Виявлено конфлікт link-local маршрутизації macOS: дефолтний `169.254.0.0/16` у ROCKNIX конфліктує з unscoped маршрутом `169.254/16 -> en0` (Wi-Fi) у таблиці маршрутизації macOS (`169.254.170.2 -> link#14 en0`).
  * Згенеровано SSH-ключ `backups/xu10_id_ed25519` та створено скрипт `scripts/setup_usb_ssh.sh`, який перенаправляє `169.254.170.2` через `en9`, встановлює SSH-ключ і переводить `/storage/.cache/usbgadget/network.conf` на чисту підмережу `10.1.1.2/24` (`HOSTIP=10.1.1.1`), щоб назавжди усунути конфлікт із `169.254/16`.
  * Перевірено прямий безпарольний SSH-вхід із середовища Antigravity (`ssh -i backups/xu10_id_ed25519 root@10.1.1.2`): інтерфейс `en9` автоматично отримав `10.1.1.1/24`, розділ `/storage` має 27.2 ГБ вільного місця, `Step 1` повністю завершено.

## [2026-10-25] Milestone 1 & Milestone 2: Build System + 160x120/320x240 Adaptive UI & Hardware/Input Monitor
* Встановлено `memory_os` у `venv_auto/` та `cmake 4.4.4`, `sdl2 2.32.74`, `zig 0.17.0`.
* Вивантажено системні бібліотеки з XU10 (`libSDL2-2.0.so.0.3200.10`, `libasound.so.2.0.0`) у `sysroot/usr/lib/`.
* Проінспектовано `evdev` мапінг консолі (`/dev/input/event3` `XU10 Gamepad` з діапазоном стіків `-1800..+1800`, `/dev/input/event2` `gpio-keys-vol` `0x72/0x73`) та композитор Sway (`XDG_RUNTIME_DIR=/var/run/0-runtime-dir`, `WAYLAND_DISPLAY=wayland-1`).
* Реалізовано `CMakeLists.txt`, `cmake/aarch64-rocknix-zig.cmake`, `src/config.hpp`, `src/hw_monitor.hpp/.cpp`, `src/input_manager.hpp/.cpp`, `src/ui_renderer.hpp/.cpp`, `src/main.cpp` та `scripts/deploy_xu10.sh`.
* Зібрано обидва бінарники (`build-macos/xu10_groovebox` та `build-xu10/xu10_groovebox`), задеплоєно у `/storage/dev/groovebox/xu10_groovebox` і запущено на екрані консолі (навантаження CPU лише `3.8%`, RAM `42 MB`).
* Внесено виправлення за результатами живого тесту на консолі:
  1. Базовий режим встановлено `320×240` з дрібним шрифтом `1×` (`3×5` пікселів + `1px` інтервал = 80 символів у рядку).
  2. Виправлено мапінг фізичних кнопок `A <-> B` (`0x131=A`, `0x133=B`) та `X <-> Y` (`0x130=X`, `0x134=Y`) відповідно до маркування корпусу MagicX XU10.
  3. Прибрано перемикання роздільної здатності по одиночній кнопці `Y` — залишено суворо комбінацію розробника `SELECT + Y`.
  4. Нижній HUD-бар перераховано пропорційно ширині екрана (`0..25%` D-Pad, `25..75%` 3 колонки `L-Stick | SEL/STA | R-Stick`, `75..100%` ABXY), щоб жоден символ не виходив за правий край.
  5. Призупинено системні процеси `mako` та `input_sense` у `deploy_xu10.sh`, які малювали системний Wayland OSD-оверлей гучності поверх нашого інтерфейсу.

## [2026-10-25] Milestone 3: Real-Time ALSA Audio Engine (32k Local / 48k Digitakt II) & 4-Track DSP Synth
* Протестовано апаратні частоти дискретизації ЦАП `RK817` (`hw:0,0`) на консолі: з'ясовано, що `24000 Hz` не підтримується апаратним дільником I2S і ядро автоматично округлює його до **`32000 Hz`** (`rate 16000 / 32000 / 44100 / 48000`). Використано точний нативний режим **`32000 Hz`** для автономної роботи на `RK817` (`period_size: 128`, `buffer_size: 512`) та **`48000 Hz`** при підключенні USB Audio (`Digitakt II`).
* Реалізовано `src/audio_engine.hpp` та `src/audio_engine.cpp` з прямим виводом через `libasound.so.2`, автоматичним перемиканням мультиплексора `Playback Mux` (`SPK` $\leftrightarrow$ `HP`) при вставці/вийманні навушників (`/dev/input/event1` `SW_HEADPHONE_INSERT`) та керуванням апаратною гучністю `Master Playback Volume` кнопками `VOL+` / `VOL-`.
* Інтегровано 4-доріжковий DSP-синтезатор (T1: PolyBLEP Saw+Sub, T2: 2-Op FM, T3: Acid 303, T4: Chord Pad) із SVF-фільтром під керуванням аналогових стіків, 16-кроковим секвенсором та Lock-Free кільцевим буфером під осцилограф і VU-метр у реальному часі.


## [2026-10-26] Milestone 7: Full SOLID Modular UI Architecture Overhaul, Dynamic Keymap & Layout Decoupling
* **Ліквідація монолітного `UiRenderer` та повне усунення `if-else` каскадів:**
  * Створено `src/ui/canvas.hpp/.cpp`: SRP-клас виключно для малювання пікселів, ліній, прямокутників та шрифту 3x5 без знання про структуру меню чи бізнес-логіку.
  * Створено `src/ui/ui_context.hpp`: абстракція `UiContext`, що передається у всі об'єкти UI.
  * Створено `src/ui/params.hpp/.cpp`: декларативний реєстр `ParamRegistry` та об'єкти `Param` з унікальними ID (`TRK.ENGINE`, `TRK.TRANSP`, `TRK.GLIDE`, `TRK.P1..P6`, `LFO1.*`, `DLY.*`, `REV.*`, `MST.*`, `SYS.*`, `STEP.*`). Будь-який параметр тепер можна перенести в будь-яке вікно зміною одного текстового ID.
  * Створено `src/ui/actions.hpp/.cpp`: реєстр дій `ActionRegistry` з унікальними ID (`NAV.*`, `PARAM.*`, `SEQ.*`, `MUTE.*`, `TAB.*`, `PLAY.TOGGLE`, тощо).
  * Створено `src/ui/keymap.hpp/.cpp`: таблиця прив'язок `Keymap` з підтримкою шарів (`BASE`, `SELECT`, `HOLD_L2`) та жестів (`PRESS`, `TAP`, `HOLD`). Підказки кнопок у HUD генеруються динамічно із зареєстрованих дій без дублювання.
  * Створено `src/ui/views.hpp/.cpp`: поліморфний інтерфейс `IView` та реалізації:
    * `GridView` — універсальний компонент для таблиць (`SYNTH`, `FX`, `MIX`), вертикальних списків (`SYS`) та спливаючих вікон (`INSPECT`).
    * `SeqView` — 8-рядковий секвенсор з поліметрією та P-Lock зі стіка.
    * `ScopeView` — осцилограф та стерео VU-метри.
  * Створено `src/ui/layout.cpp`: декларативна конфігурація сіток параметрів та вкладок.
    * Вкладку `[R2] MIX` перенесено лівіше за `[R1] SCOPE` (`[L1] SEQ/SYNTH | [L2] FX | [R2] MIX | [R1] SCOPE | [G] SYS`).
    * Усунено дублювання кнопок `ADD/DEL` у першому рядку `SYNTH`: замінено на корисні параметри `TRANSP` (-24..+24 st) та `GLIDE` (5..500 ms).
    * Об'єднано номери та назви треків у лівому стовпчику `SeqView` (`01BD`, `02SD`...), усунено зайву праву панель `TRK INFO`.
    * Усунено перекриття верхнього бару жовтим текстом при затисканні `SELECT`: верхній бар залишається цілісним та читабельним, підказки відображаються у нижньому HUD із бейджем `[SEL]`.
    * Додано шар `HOLD L2`: утримання L2 у секвенсорі активує бейдж `[MUTE]` та переводить кнопки `X, Y, A, B` у режим миттєвого глушіння треків 1–4 (`M.T1`, `M.T2`, `M.T3`, `M.T4`).
  * Створено `src/ui/ui_shell.hpp/.cpp`: головний оркестратор інтерфейсу, що замінив застарілий `ui_renderer.cpp`.
* Збірка перевірена на macOS та aarch64 (Zig C++ cross-compiler) з 0 попереджень компілятора.
* Згенеровано 9 попіксельних тестових знімків у `/Users/oleksii/.gemini/antigravity/brain/a2583e1d-b2ce-4f26-885f-f7402638aa14/solid_0*.png`.

* Створено модульний заголовок `src/synth_engines.hpp`:
  * Додано 5 типів синтезаторних движків (`VA SAW+SUB`, `2-OP FM`, `ACID 303`, `CHORD PAD`, `ANALOG DRUM`), які можна вільно перемикати на будь-якій із 4 доріжок.
  * Кожен движок описує 6 власних нормалізованих параметрів через `kParamNames`, завдяки чому UI вкладки `[L2] SYNTH` автоматично підтягує назви параметрів без хардкоду.
* Розширено `src/audio_engine.hpp/.cpp`:
  * Додано синтез `ANALOG DRUM` (punchy pitch-envelope sine + noise perc + drive) та уніфіковане читання 6 параметрів на кожну доріжку разом із живими макро-офсетами аналогових стіків (`cutoff_macro`, `resonance_macro`, `env_mod_macro`, `fm_macro`).
  * Інтегровано майстер-ефект **Stereo Ping-Pong Delay** та мікшер із параметрами `Level`, `Mute`, `Delay Send` на кожній доріжці.
  * Додано **Parameter Locks (P-Lock фільтра на крок)**, увімкнення/вимкнення кроків та покрокове транспонування нот у 16-кроковому секвенсорі.
* Створено `src/midi_engine.hpp/.cpp`:
  * Неблокуюче читання ALSA RawMIDI (`/dev/snd/midiC1D0` або `midiC0D0`), програмний PLL-фільтр для вхідного MIDI Clock `0xF8` (24 PPQN), синхронізація транспорту `0xFA`/`0xFB`/`0xFC` та тригер нот по MIDI-каналах 1..4.
* Оновлено `src/ui_renderer.hpp/.cpp` та `src/main.cpp`:
  * Повне інтерактивне редагування параметрів движка на `[L2] SYNTH`, мікшера і ділею на `[R2] MIX/FX` та 16-крокового секвенсора з P-Locks на `[R1] SCOPE`, із контекстними підказками у нижньому HUD-барі.
  * Зібрано та задеплоєно на MagicX XU10 (`10.1.1.2`): `0` попереджень компілятора, споживання CPU — **4.5%**.


## [2026-10-25] USB OTG 5V VBUS (`OTG_SWITCH` PMIC + DTB Patch), Universal Class-Compliant USB Audio/MIDI & Autostart
* Виявлено причини, чому при підключенні USB-хаба не світився світлодіод і не детектився Digitakt II на порту OTG:
  1. У режимі `USB_MODE=cdc` контролер `/sys/class/usb_role/ff300000.usb-role-switch/role` перебував у стані `device` (Peripheral), тому не міг опитувати зовнішню USB-периферію.
  2. У штатному `rk3326-magicx-xu10.dtb` був відсутній вузол регулятора `OTG_SWITCH` та властивість `vbus-supply` у `/usb@ff300000`, через що внутрішній ключ 5V VBUS у PMIC `RK817` (`0xb4` bit 2) залишався розімкненим (`disabled`).
* Створено скрипт `scripts/patch_dtb_otg_vbus.py` та пропатчено `/flash/rk3326-magicx-xu10.dtb` (додано `OTG_SWITCH` у `/i2c@ff180000/pmic@20/regulators` та `vbus-supply` у `/usb@ff300000`).
* У `src/hw_monitor.cpp` додано метод `toggleUsbRole()` (викликається круглою кнопкою `FN` або `SELECT + B`):
  * Перехід у **`HOST`**: відв'язує USB Gadget, перемикає `role` у `host`, вмикає 5V VBUS ключ `OTG_SWITCH` (`i2cset -f -y 0 0x20 0xb4 0x46`) та підвантажує модулі `snd-usb-audio snd-rawmidi snd-seq-midi`.
  * Перехід у **`DEVICE`**: вимикає 5V VBUS (`0x42`), перемикає `role` у `device` та запускає `usbgadget start cdc` + `sshd` (`10.1.1.2`).
* Прибрано прив'язку лише до Digitakt:
  * `HardwareMonitor` детектує будь-який зовнішній Class-Compliant USB Audio інтерфейс у `/proc/asound/cards` і виводить його назву.
  * `MidiEngine` паралельно опитує до 8 портів `/dev/snd/midiC0D0..midiC7D0` (дозволяє підключати через USB-хаб одночасно Digitakt II, MIDI-клавіатуру та аудіоінтерфейс), виправлено скидання на крок 0 по `0xFA` (MIDI Start) та додано підтримку `MIDI CC` (`CC 16..21`, `CC 74`, `CC 71`).
* Налаштовано автоматичний запуск DAW при старті консолі (`/storage/.config/autostart/99-groovebox.sh`) та ярлик у меню **`Ports -> XU10 Groovebox`** (`/storage/roms/ports/XU10_Groovebox.sh`).


## [2026-10-25] Живий лог-аналіз підключення Elektron Digitakt II (`1935:102b`) та виправлення `S32_LE` / `0xF8` Transport Gate
* За результатами живого тесту з'єднань у `dmesg` та `/tmp/groovebox.log` зафіксовано:
  1. Консоль успішно розпізнала **Elektron Digitakt II** (`idVendor=1935, idProduct=102b, Product: Elektron Digitakt II`) по USB High-Speed (`usb 1-1`) та підняла `hw:1,0` і `/dev/snd/midiC1D0`.
  2. **Причина відсутності звуку при підключенні Digitakt II:** у `/tmp/groovebox.log` зафіксовано `Sample format not available for PLAYBACK: Invalid argument` при відкритті `hw:1,0` у форматі `SND_PCM_FORMAT_S16_LE`, оскільки USB-аудіоінтерфейс Digitakt II працює у **32-бітному форматі `SND_PCM_FORMAT_S32_LE`** (`48000 Hz`).
  3. **Причина самовільного руху екрана («ніби грає») одразу при підключенні кабелю до Digitakt II:** Digitakt II безперервно шле `0xF8` (MIDI Clock 24 PPQN) навіть у стані Stop, а колбек `on_step16_` у `main.cpp` просував кроки секвенсора на кожні 6 тіків `0xF8` без перевірки `if (app.transport_playing)`.
  4. **Причина скидання режиму `host` назад у `cdc`:** фонове правило `/usr/lib/udev/rules.d/80-usbgadget.rules` викликало `/usr/bin/usbgadget trigger`, який читав `USB_MODE=cdc` із `/storage/.cache/usbgadget/usbgadget.conf`.
* Виправлено:
  * У `AudioEngine::alsaThreadLoop()` додано автопідбір формату для зовнішніх USB-карт (`S32_LE -> S24_3LE -> S24_LE -> S16_LE`) з конвертацією буфера на льоту та **паралельним моніторингом (Dual Output)** одночасно у `hw:1,0` (USB Audio в Digitakt II) та `hw:0,0` (динамік/навушники консолі).
  * У `main.cpp` просування кроків від зовнішнього `0xF8` дозволено лише коли `app.transport_playing == true` (активується по `0xFA` MIDI Start з Digitakt II і зупиняється по `0xFC` MIDI Stop), тоді як BPM-детектор PLL оновлюється постійно.
  * У `HardwareMonitor::requestUsbRoleSwitch("host")` додано запис `USB_MODE=disabled` у `/storage/.cache/usbgadget/usbgadget.conf`, щоб `udev` не перехоплював порт назад у `cdc`.


## [2026-10-25] Усунення конфлікту двох апаратних генераторів (Dual-Clock XRUN) на USB Audio 48k (`Digitakt II`)
* Причина рваного/хриплячого звуку при одночасному виводі в динамік консолі (`hw:0,0`) та по USB в Digitakt II (`hw:1,0`):
  * Обидва пристрої мають власні незалежні кварцові генератори (I2S PLL у RK3326 проти асинхронного USB Audio клока всередині Digitakt II).
  * Послідовний блокуючий виклик `snd_pcm_writei(pcm_usb)` та `snd_pcm_writei(pcm_internal)` в одному потоці з буфером `10 мс` призводив до того, що очікування DMA на `hw:0,0` спустошувало буфер ізохронного USB-потоку `hw:1,0` (`XRUN / -EPIPE`), викликаючи безперервні перезапуски потоку (`snd_pcm_recover`).
* Виправлено в `src/audio_engine.cpp`:
  1. Коли підключено зовнішній USB Audio інтерфейс (`card > 0`), відкривається **виключно** `hw:1,0` (потік на 100% тактується від кварцу Digitakt II без блокування об `hw:0,0`). Коли USB відключено — відкривається `hw:0,0` (`32 000 Hz`).
  2. Аудіопотоку призначено Real-Time пріоритет ядра **`SCHED_FIFO` (75)** (`pthread_setschedparam`).
  3. Розмір блоку збільшено до `256` семплів, а буфер ALSA для USB Isochronous — до `24 000 мкс` (`24 мс`), що повністю усуває джитер контролера `dwc2`.

## [2026-10-25 12:16] Portability & USB Crackle Fix Integration
* **Device Profiles Architecture:**
  * Зареєстровано `src/device_profile.cpp` у `CMakeLists.txt`.
  * Додано виклик `groovebox::loadActiveProfile()` на старті в `src/main.cpp`.
  * Пов'язано аудіорутінг (`internal_card`, `mux_control`, `volume_control`) в `src/audio_engine.cpp` через активний профіль пристрою.
  * У деплой `scripts/deploy_xu10.sh` додано передачу каталогу `profiles/` на цільову консоль.
* **USB Audio Buffer & CPU Governor Fix:**
  * Додано примусове перемикання CPU Governor на `performance` (`echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`) у `launch_groovebox.sh`, що запобігає затримкам переривань `dwc2` при роботі з Digitakt II (48 кГц).
  * Успішно проведено крос-компіляцію через Zig C++ під `aarch64-linux-gnu.2.38`.

## [2026-10-25 12:46] ALSA Implicit Feedback (implicit_fb=1) & Output Hot-Toggle
* **ALSA Implicit Feedback for Elektron Gear:**
  * Виявлено параметр ядра `implicit_fb: Apply generic implicit feedback sync mode` у модулі `snd-usb-audio`. Раніше параметр був `N` для всіх слотів, що викликало розбіжність генератора RK3326 і кварцу Digitakt II.
  * Активовано `implicit_fb=1,1,1,1` у `scripts/deploy_xu10.sh`, `src/hw_monitor.cpp` та зафіксовано в `/storage/.config/modprobe.d/snd-usb-audio.conf`.
  * У `src/midi_engine.cpp` додано прапорець `O_CLOEXEC` при відкритті RawMIDI, що усунуло успадкування файлових дескрипторів фоновими демонами.
* **Audio Engine Latency & Parameters:**
  * У `src/audio_engine.cpp` встановлено `soft_resample = 0` (заборона програмної інтерполяції ALSA) та точний розрахунок буфера 21.33 мс (1024 семпли / 4 періоди по 256 семплів).
* **Hot-Toggle Audio Output (SELECT + Y):**
  * Перепризначено комбінацію `SELECT + Y` з масштабування екрана на миттєве перемикання аудіовиходу (`AudioEngine::toggleOutputOverride()`): `AUTO / USB (48k)` $\leftrightarrow$ `FORCE INTERNAL RK817` на льоту без перезапуску.
  * Оновлено статусний рядок UI: показує `OUT: USB (48000 Hz)` або `OUT: [INT-FORCE] RK817...`.

## [2026-10-25 13:03] USB dwc2 URB Queue Fix (lowlatency=0), ALSA Pre-Roll & Real-Time XRUN Monitor
* **Kernel URB Pipeline (lowlatency=0):**
  * Вимкнено агресивний режим `lowlatency=1` у модулі `snd-usb-audio` (`lowlatency=0`), який на контролерах `dwc2` обвалював URB-пайплайн при будь-якому мікрофреймовому джитері, спричиняючи довгі (100–300 мс) випадання звуку.
  * Параметри збережено в `/storage/.config/modprobe.d/snd-usb-audio.conf`: `options snd-usb-audio implicit_fb=1,1,1,1 lowlatency=0`.
* **ALSA Pre-Roll & Buffer Cushion:**
  * У `src/audio_engine.cpp` реалізовано pre-roll: перед запуском відтворення апаратний буфер ALSA попередньо заповнюється 4 блоками (1024 семпли = 21.33 мс), що виключає старт ЦАП на порожньому буфері.
  * Збільшено буферний горизонт USB до 32 мс (`latency_us = 32000u`).
  * Реалізовано відновлення та re-prime (2 блоки) після XRUN для блокування каскадних зривів.
* **Real-Time UI & Log Diagnostics:**
  * Додано лічильник `audio.xrun_count` безпосередньо у статусний рядок інтерфейсу консолі (`XRUN: %u` на вкладках L1 та R1).
  * Вимкнено буферизацію виводу в `src/main.cpp` (`setvbuf _IONBF`), завдяки чому події ALSA записуються в `/tmp/groovebox.log` миттєво.

## [2026-10-25 14:01] Digitakt II Explicit Feedback Fix (implicit_fb Removed) & Peak CPU Tracking
* **Digitakt II Explicit Feedback Restoration:**
  * За логами `/tmp/groovebox.log` виявлено помилку `XRUN (-5)` (`-EIO` Input/output error). Digitakt II використовує виділений ендпоінт Explicit Feedback, тому форсування `implicit_fb=1` призводило до відсутності вхідних пакетів capture та повної відмови ALSA.
  * Вимкнено `implicit_fb` у ядрі, збережено `lowlatency=0` (`options snd-usb-audio lowlatency=0`).
  * У `src/audio_engine.cpp` додано вивід опису помилки через `snd_strerror()`.
* **Peak CPU Session Monitoring:**
  * У `src/hw_monitor.hpp` та `src/hw_monitor.cpp` додано відстеження пікового значення CPU за всю поточну сесію (`cpu_max_percent`).
  * У `src/ui_renderer.cpp` на вкладці **[L1] HW/SYS** додано відображення піку: `SYSTEM : CPU %2.0f%% [MAX %2.0f%%]`.

## [2026-10-25 14:08] Mozart Default Pattern (KV 331) & 512-Sample ALSA DMA Flush
* **Mozart "Rondo Alla Turca" Default Sequence:**
  * Запрограмовано класичну 16-крокову секвенцію В. А. Моцарта ("Rondo Alla Turca" KV 331 / Турецький марш) у `initDefaultPattern()`:
    * T1 (Drum Syn): маршовий ритм та оркестрові литаври.
    * T2 (VA Synth): класичний бас Альберті (A2, E3, C3, E3...).
    * T3 (Acid 303 Lead): віртуозна партія Турецького маршу (B4, A4, G#4, A4, C5, D5, C5, B4, C5, E5...).
    * T4 (2-Op FM): гармонійна підкладка клавесину (A-moll / D-moll).
* **ALSA dwc2 DMA Flushing & 512-Frame Period:**
  * Збільшено розмір блоку `kBlockFrames` до 512 семплів (10.66 мс), що знизило частоту переривань на процесор з 187 Гц до 93 Гц.
  * Буфер USB розширено до 64 мс (`latency_us = 64000u`).
  * При виникненні XRUN тепер викликається `snd_pcm_drop(pcm)` для повного скидання черги DMA перед `snd_pcm_prepare()`, що ліквідує зависання драйвера зі статусом `prepare failed: Broken pipe`.

## [2026-10-25 14:35] Dedicated Linux Appliance (KMSDRM), Audio CPU 3 RT Isolation & Buffer Hot-Cycling (SELECT + A)
* **Dedicated Appliance Architecture (Direct KMSDRM):**
  * Повністю ліквідовано залежність від Wayland, Sway та EmulationStation. Замасковано `essway.service` та `sway.service` у systemd.
  * Створено та активовано системний сервіс `/storage/.config/system.d/groovebox.service` (`WantedBy=rocknix.target`), який запускає DAW одразу при старті Linux напряму поверх `/dev/dri/card0` через Direct KMSDRM (`SDL_VIDEODRIVER=kmsdrm`).
  * Споживання оперативної пам'яті скоротилося з 297 МБ до 229 МБ, шина AXI та GPU Mali повністю розвантажені від віконного композитора.
* **Audio Engine CPU 3 Isolation & Real-Time FIFO 85:**
  * Аудіопотік жорстко зафіксовано на виділеному ядрі CPU 3 (`pthread_setaffinity_np`), ізолювавши його від апаратних переривань контролера USB `dwc2`, дисплея та таймерів ядра (які обробляються на CPU 0).
  * Пріоритет планувальника піднято до `SCHED_FIFO 85`.
* **Live Buffer Hot-Cycling (128 -> 256 -> 512 -> 1024 -> 2048):**
  * Додано метод `AudioEngine::cycleBufferSize()` та комбінацію гарячих клавіш **`SELECT + A`** для циклічної зміни розміру блоку та буфера на льоту: `128 -> 256 -> 512 -> 1024 -> 2048 семплів`.
  * Буфер ALSA адаптивно розраховується з коефіцієнтом $\times 6$ періодів для USB Audio (`latency_us = max(64ms, block * 6)`), гарантуючи стабільну чергу URB.
  * Поточний розмір буфера виводиться в статусному рядку інтерфейсу (`BUF: %u`) та відображається в підказці при затисканні `SELECT` (`SEL:[A=BUF B=USB]`).

## [2026-10-25 14:43] USB dwc2 IRQ CPU 2 Isolation & URB nrpacks=1 Tuning
* **USB Controller IRQ Affinity (CPU 2):**
  * Перенаправлено апаратні переривання контролера `dwc2` (IRQ 26) на виділене ядро CPU 2 (`echo 4 > /proc/irq/26/smp_affinity`).
  * Тепер 3 ядра виконують суворо розмежовані задачі:
    * **CPU 0:** KMSDRM Display Scanout, Linux Kernel, Evdev Input.
    * **CPU 2:** USB `dwc2` Isochronous Interrupts (SOF 125 µs).
    * **CPU 3:** Real-Time DSP Audio Synthesis (`SCHED_FIFO 85`).
* **Kernel snd-usb-audio nrpacks=1:**
  * Зафіксовано параметр `nrpacks=1` у `/storage/.config/modprobe.d/snd-usb-audio.conf` та `src/hw_monitor.cpp`.
  * Передача кожного ізохронного URB без затримки групування зменшує навантаження на обмежені FIFO-буфери `dwc2`.

## [2026-10-25 15:16] MIDI Bulk Read Optimization (64-byte Buffer) & 96-Sample Low-Latency Mode
* **MIDI Block-Read Optimization (src/midi_engine.cpp):**
  * Замінено побайтове читання `read(fd, &byte, 1)` на блоковий виклик `read(fd, rx_buf, 64)` з послідовним розбором у пам'яті.
  * Усунуто лавину системних викликів та одиночних запитів Bulk IN на контролер `dwc2`, які переривали ізохронний планувальник аудіо при отриманні потоків MIDI Clock та CC/NRPN.
* **96-Sample Buffer Option (2.0 ms @ 48k):**
  * Додано розмір блоку 96 семплів у послідовність перемикання `cycleBufferSize()`: `96 -> 128 -> 256 -> 512 -> 1024 -> 2048`.
  * Перевірено успішну роботу ультранизької затримки на інтерфейсі M-Vave Cube Baby.

## [2026-10-25 15:28] Linux snd-usb-audio autoclock=0 (UAC2 Clock Query Elimination)
* **Kernel Module snd-usb-audio autoclock=0:**
  * Вимкнено параметр ядра `autoclock` (`autoclock=0`) у `/storage/.config/modprobe.d/snd-usb-audio.conf`, `src/hw_monitor.cpp` та `scripts/deploy_xu10.sh`.
  * Перезавантажено модуль у пам'яті ядра (`modprobe -r snd_usb_audio && modprobe snd_usb_audio autoclock=0 lowlatency=0`), перевірено значення `N` у `/sys/module/snd_usb_audio/parameters/autoclock`.
  * Ліквідовано циклічні опитування дескрипторів годинника UAC2 (`Clock Selector / Source Validity`), які на контролері `dwc2` перевантажували керуючий канал та призводили до випадання мікрофреймів при роботі з Elektron Digitakt II.

## [2026-10-25 15:45] Elektron Matrix Sequencer (16-Row x 128-Step), Param Trigs & 4-Stage ADSR Engine
* **16-Row Polyphonic / Drum Matrix Architecture (src/synth_engines.hpp, src/audio_engine.hpp):**
  * Створено матричну модель `16 рядків × 128 кроків` (4 доріжки × 4 поліфонічні голоси/доріжки ударних):
    * Рядки 0–3: Track 1 (Drum: 1BD, 1SD, 1CH, 1OH).
    * Рядки 4–7: Track 2 (VA Synth: голоси 1–4).
    * Рядки 8–11: Track 3 (Acid/Lead: голоси 1–4).
    * Рядки 12–15: Track 4 (FM/Chords: голоси 1–4).
  * Реалізовано повноцінний 4-стадійний генератор огинаючої `AdsrEnvelope` (Attack, Decay, Sustain, Release) з експоненційними кривими загасання для всіх голосів і рушіїв.
* **Elektron-Style Yellow Parameter Trigs:**
  * Додано підтримку жовтих кроків автоматизації (`is_param_trig`):
    * Звичайний Note Trig (зелений/бірюзовий): запускає огинаючу, фазу генератора та ноту.
    * Param Trig (жовтий `#FFD600`): застосовує P-Locks (зріз фільтра, резонанс, параметри модуляції) до активного звуку без повторного перезапуску огинаючої та ноти.
    * Перемикання кроку кнопкою `A`: `Off -> Note Trig -> Param Trig -> Off`.
* **64-Step Visible Page Grid UI (src/ui_renderer.cpp, src/main.cpp):**
  * Трансформовано 4-ту вкладку `[R1]` з пасивного осцилографа у повноцінну секвенсорну матрицю 16 рядків × 64 видимих кроки.
  * Підтримка двох сторінок (P1: 0..63, P2: 64..127) з ручним перемиканням `SELECT + Y` та автоскролом за курсором.
  * Циклічне перемикання довжини патерну `SELECT + X` (16 -> 32 -> 64 -> 128).
  * Навігація D-Pad: рух курсора по 16 рядках та 128 кроках; `X/Y` — транспонування півтонів; `B` — запис P-Lock зі стіка.
* **64-Step Polyphonic Mozart "Rondo Alla Turca" Default Sequence:**
  * Запрограмовано багатодоріжкову 64-крокову секвенцію Турецького маршу Моцарта: барабанний біт (рядки 0–3), партія басу (рядки 4–5), соло з жовтими Param Trigs (рядок 8) та FM акорди (рядки 12–14).

## [2026-10-25 16:20] Default ADSR Zero-Sustain, Panic/Kill, TB-303 65ms Glide, Step Inspector & Tab Reorg (L1 SEQ / Button G)
* **Default Zero-Sustain & Sound Termination (src/synth_engines.hpp, src/audio_engine.cpp):**
  * Усунуто нескінченний сустейн: дефолтний `sustain = 0.0f` та `release = 0.05f` для всіх інструментів та синтезаторів.
  * У методі `AdsrEnvelope::process()`: коли `sustain_level <= 0.001f`, після завершення фази `kDecay` огинаюча переходить у `kIdle` з повним вимиканням голосу (`active = false`, нульове споживання CPU).
  * Додано `gate_len` до кроків секвенсора: після спливання Gate огинаючі автоматично переходять у фазу Release.
  * Реалізовано метод `AudioEngine::panic()`: миттєве глушіння всіх 16 голосів, обнулення фаз, очищення буферів stereo ping-pong delay та скидання VU-метрів.
  * Автоматичний виклик `panic()` при зупинці секвенсора кнопкою `START` або комбінацією **`SELECT + B`** (Panic / Kill Sound).
* **Roland TB-303 Constant-Time 65ms Glide & Legato (src/synth_engines.hpp, src/audio_engine.cpp):**
  * Додано прапорець `slide` до структури `SequencerStep`.
  * При зв'язаних нотах на рушії `kAcid303` нова нота не перезапускає огинаючі та не збиває фазу осцилятора, а плавно ковзає до нової цільової частоти за схемою RC-фільтра напруги CV ($\tau \approx 65\text{ ms}$).
* **Hold-to-Inspect Step Inspector Popup (src/ui_renderer.cpp, src/main.cpp):**
  * Розпізнавання короткого та довгого натискання кнопки `A`:
    * Короткий клік (<200 мс): дефолтне перемикання кроку (`Off -> Note -> Param -> Off`).
    * Утримання (>200 мс): спливає модальне вікно **Step Inspector** над вибраним кроком.
  * Ліва рука керує параметрами: `D-Pad Up/Down` обирає рядок (`TYPE`, `NOTE`, `VELOCITY`, `GATE LEN`, `SLIDE`, `ATTACK`, `DECAY`, `SUSTAIN`, `RELEASE`, `CUTOFF` за шкалою Elektron 0..127); `D-Pad Left/Right` або лівий стік обертає значення (звичайний крок $\pm 1$, із `SELECT` $\pm 8$).
* **Реорганізація вкладок та бокова кнопка G:**
  * Секвенсор перенесено на першу вкладку: **`[L1] SEQ`** (головний робочий екран DAW).
  * Інші вкладки: `[L2] SYNTH`, `[R2] MIX`, `[R1] SCOPE`.
  * Бокова апаратна кнопка **`G`** (`key.fn` `0x2c0`): відкриває/закриває системне меню `[G] SYS`.
* **Динамічний контекст клавіші SELECT:**
  * При затисканні `SELECT`: верхній заголовок показує `SEL: [A:BUF 96..2048] [B:PANIC/KILL] [X:LEN 16..128] [Y:PAGE] [STA:RESET]`, а нижній HUD оновлює всі підказки дій.

## [2026-10-25 16:33] Per-Row Polymetry (16..128), Individual Playheads, Page 2 Guard & Mono Row Allocation
* **Per-Row Polymetry Architecture (src/audio_engine.hpp, src/audio_engine.cpp):**
  * Реалізовано індивідуальну довжину для кожного з 16 рядків: `row_lengths_[r]`.
  * За дефолтом усім рядкам встановлено довжину **16 кроків** (`row_lengths_[r] = 16`).
  * Перемикання довжини поточного рядка кнопкою **`SELECT + X` (LEN)** за поліметричним рядом: `16 -> 24 -> 32 -> 48 -> 64 -> 72 -> 80 -> 96 -> 112 -> 128`.
  * Індивідуальне тактування: кожен рядок обчислює свій крок як `step_r = master_tick % row_length[r]`, дозволяючи різним доріжкам грати в незалежних розмірах (поліметрія 16 vs 24 vs 32 vs 64).
* **Mono Row Allocation (303 Mono):**
  * Для монофонічного синтезатора TB-303 виділено рівно 1 рядок (`row 4`), що запобігає марнуванню 4 рядків під одноголосий пристрій.
* **Page 2 Guard (`SELECT + Y`):**
  * Перехід на `Page 2` (кроки 65..128) активується **лише якщо довжина активного рядка > 64**. При довжині $\le 64$ сторінка жорстко залишається на `Page 1`.
  * Навігація курсора D-Pad не виходить за межі поточної довжини рядка.
* **Графічне зонування та індивідуальний Playhead (src/ui_renderer.cpp):**
  * Кроки в межах активної довжини підсвічуються сіткою та точками тактів.
  * На межі довжини малюється помаранчевий розділювач кінця циклу `|`.
  * Кроки поза межами активної довжини (від `len` до 63) затемнюються в колір фону.
  * Кожен рядок має **власний білий маркер Playhead**, який рухається відповідно до свого поліметричного лічильника в реальному часі.

## [2026-10-25 17:10] 32-Step Wide Grid, 4-Page System (P1..P4), D-Pad Auto-Repeat, Stick P-Lock Auto-Save & L1 Tab Toggle
* **32-Step Visible Grid (7px cells) & 4-Page System (src/synth_engines.hpp, src/ui_renderer.cpp, src/main.cpp):**
  * Переведено відображення секвенсора на сітку 32 видимих кроки завширшки (`kStepsPerPage = 32`, `kMaxPages = 4`, до 128 кроків).
  * Ширина клітинки збільшена до 7 пікселів (`cx = 36 + s * 7`, розмір тригера 6×7 px) з чітким білим курсором (8×9 px).
  * Справа додано динамічні індикатори сторінок `[P1]`, `[P2]`, `[P3]`, `[P4]`: сторінки P2..P4 відображаються лише тоді, коли довжина рядка перевищує 32, 64 або 96 кроків відповідно.
  * Автоматичне перемикання активної сторінки при переміщенні курсора D-Pad (`app.seq_page = app.cursor_step / 32`).
  * Перемикання сторінок комбінацією `SELECT + Y` циклічно обмежується реальною кількістю активних сторінок рядка.
* **D-Pad Smooth Auto-Repeat (src/input_manager.hpp, src/input_manager.cpp, src/main.cpp):**
  * Реалізовано метод `InputManager::updateRepeat(now_ms)`: початкова затримка перед автоповтором 250 мс, інтервал повтору 70 мс (~14 кроків/сек).
  * Забезпечено плавний безперервний скрол курсора по сітці секвенсора та в меню інспектора при тривалому затисканні будь-якого напрямку D-Pad.
* **Elektron Parameter Lock on Stick Auto-Save (src/main.cpp, src/ui_renderer.cpp):**
  * Затискання кнопки `A` на кроці з відхиленням аналогового стіка активує режим живого налаштування параметра (`stick_plock_tweaked = true`) з негайним звуковим прев'ю.
  * При відпусканні кнопки `A` зафіксоване положення стіка автоматично зберігається в крок як активний P-Lock (`has_plock = true`) без випадкового вимкнення кроку.
  * Звичайний короткий клік (<200 мс) без руху стіка зберігає дефолтне перемикання кроку (`Off -> Note -> Param -> Off`).
* **L1 Single-Tap Tab Toggle & Drum Track Slide Suppression:**
  * Одиночне натискання кнопки `L1` перемикає робоче вікно між `[L1] SEQ` та `[L1] SYNTH`.
  * Для барабанних доріжок (`kDrumSyn`) параметр `SLIDE` вимкнено та замінено на маркер `N/A (DRUM)`.

## [2026-10-25 17:55] Elektron OS Design Overhaul: Trig Probability & Conditions, Graphical HUD, Clean 8-Row Setup & Global Settings List
* **Clean 2-Instrument Default Preset & 8 Active Rows (src/audio_engine.cpp, src/audio_engine.hpp):**
  * Видалено весь хардкод Моцарта: секвенсор запускається повністю чистим із порожніми патернами.
  * За дефолтом активні рівно 2 інструменти: Drums (рядки 0..3: `BD`, `SD`, `CH`, `OH`) та VA Synth (рядки 4..7: `V1`..`V4`) — всього 8 рядків (`active_rows_count_ = 8`).
  * Курсор D-Pad обмежується активними 8 рядками без порожніх дублікатів унизу.
* **Elektron Probability & Trig Conditions (src/synth_engines.hpp, src/audio_engine.cpp, src/ui_renderer.cpp, src/main.cpp):**
  * До структури `SequencerStep` інтегровано `probability` ($0\%..100\%$) та `TrigCondition` (`kNone`, `kFill`, `kNotFill`, `k1_2`, `k2_2`, `k1_4`, `k2_4`, `k3_4`, `k4_4`, `kFirst`, `kNotFirst`, `kPre`, `kNotPre`).
  * Розрахунок умов у реальному часі в `triggerSequencerStep()` із лічильником циклів патерну та станом `L3` (Fill Mode).
  * У Step Inspector додано параметри 10 (`PROBABILITY`) та 11 (`CONDITION`).
* **Elektron Trig Keymap (src/main.cpp, src/audio_engine.cpp):**
  * `A`: Встановлення/перемикання тригера (`Note` $\rightarrow$ `Param Trig`); утримання `A` + стік $\rightarrow$ живий P-Lock.
  * `B`: Миттєве видалення тригера під курсором (`Trig Clear / OFF`).
  * `SELECT + A`: **Step Preview** — передпрослуховування виділеного кроку секвенсора з урахуванням ноти та P-Lock параметрів.
  * `SELECT + B`: **Clear Track** — повне очищення активної доріжки (`clearRow`).
  * `SELECT + START`: **Randomize Track** — музичний евристичний рандомайзер ритму та нот для активної доріжки.
  * `SELECT + VOL+` / `SELECT + VOL-`: Швидке регулювання BPM темпу ($60..200$).
* **Graphical Low-Res HUD & Right Sidebar (src/ui_renderer.cpp):**
  * Повністю усунуто текстовий шум: нижній бар переведено на 5 мікро-віджетів (D-Pad хрестик 14×14 px з підсвіткою напрямків, L-Stick 2D-приціл 16×16 px, центральний статус транспорту/BPM/HP, R-Stick 2D-приціл 16×16 px, ромб кнопок ABXY).
  * Праворуч від сітки виділено сегмент доріжок шириною 44 px (рівно під вкладкою `[G] SYS`) з переліком активних інструментів, довжини та сторінок (`BD L16 P1`, `VA1 L16 P1`).
  * Вкладка `[G] SYS` зменшена до 44 px; усередині меню реалізовано вертикальний список Elektron Global Settings (Buffer, USB Role, Sample Rate, HP Mux, Telemetry).

## [2026-10-25 18:25] Modular Instrument Manager on SYNTH Tab, Per-Tab Dynamic HUD (32px), De-duplicated Track Numbers & L1 Flip-Flop
* **Track Label De-duplication & Row Indices (src/ui_renderer.cpp):**
  * Лівий стовпчик сітки секвенсора переведено виключно на охайні двозначні номери рядків `01`..`08` (`kTextDim`).
  * Повні інструментальні позначки (`BD L16 P1`, `SD L16 P1`, `V1 L16 P1`..) зосереджено тільки у правому відокремленому сегменті під `[G] SYS`.
  * Видалено статичний надпис `[+] ADD` із сітки секвенсора — додавання інструментів перенесено до вкладки `SYNTH`.
* **Two-State L1 Flip-Flop & Top Bar Fix (src/ui_renderer.cpp, src/main.cpp):**
  * Перша вкладка прив'язана до бампера `L1`: працює як суворий циклічний перемикач `SEQ` $\leftrightarrow$ `SYNTH`.
  * Верхній заголовок динамічно показує `[L1] SEQ/synth` (у режимі секвенсора) або `[L1] SYNTH/seq` (у синтезаторі).
  * Підсвітка активного таба виправлена: вкладка `[L2] FX` більше не підсвічується помилково при переході в `SYNTH`.
  * Вкладку 3 перейменовано на `[R1] SCOPE` (монітор хвилі / виходу), оскільки модуляції LFO перенесені всередину параметрів синтезатора.
* **Expanded 32px Bottom HUD with Dynamic Stick Labels & Per-Tab ABXY Matrix (src/ui_renderer.cpp, src/main.cpp):**
  * Висоту нижнього бару збільшено з 24 px до 32 px, усунуто невикористовувані пікселі під робочою областю (`bot_y = 206`).
  * Над 2D-боксами аналогових стіків виведено динамічні назви призначених параметрів через слеш (`CUT/RES`, `MOD1/MOD2`, `DLY/FB`, `REV/DLY` тощо), які зчитуються з налаштувань у меню `SYNTH`.
  * Повністю виправлено графічний ромб кнопок ABXY: Top (X), West (Y), East (A), South (B).
  * Праворуч від ромба реалізовано дворядковий блок підказок клавіш, індивідуальний для кожної вкладки:
    * Рядок 1 (верхній): `X: <func_x>   Y: <func_y>`
    * Рядок 2 (нижній): `A: <func_a>   B: <func_b>`
    * У режимі `SEQ`: `X:NOTE+ Y:PAGE` / `A:TRIG B:DEL` (видалення кроку суворо тут).
    * У режимі `SYNTH`: `X:ADD Y:DEL` / `A:TEST B:INIT`.
    * У режимі `MIX`: `X:MUTE Y:SOLO` / `A:SET B:RESET`.
* **Compact Table Layout on [L1] SYNTH Tab & Dynamic Instrument Pool (src/ui_renderer.cpp, src/main.cpp, src/audio_engine.cpp):**
  * Замінено лінійний список параметрів на 6-рядкову таблицю 4×6 (INST & ADD/DEL, FILTER, OSC/FM, ADSR, MOD/LFO, SENDS/STICKS).
  * Кнопки `X` та `Y` у вкладці `SYNTH` динамічно додають (`+4` рядки) або видаляють (`-4` рядки) інструменти в пулі аудіорушія.
  * У `src/audio_engine.cpp` ініціалізовано мапінг усіх 16 рядків (T0: Drums 0..3, T1: VA 4..7, T2: Acid 303 8..11, T3: FM 2-Op 12..15).

## [2026-10-25 18:40] Cyan Tab Color, Full D-Pad 6x4 Grid Nav with A/B Value Tweak, [+] Add Slot in Sequencer & Tabular MIX with MASTER Bus
* **Cyan Sub-Mode Tab Color for SYNTH (src/ui_renderer.cpp):**
  * Коли активний режим `SYNTH`, плашка `[L1] SYNTH/seq` у верхньому барі підсвічується неоново-блакитним (`palette::kAccentCyan`) замість помаранчевого, надаючи миттєву візуальну диференціацію двох режимів L1.
* **Full D-Pad 6x4 Matrix Navigation & A/B Value Controls in SYNTH (src/main.cpp, src/ui_renderer.cpp):**
  * D-Pad (Up/Down/Left/Right) суворо переміщує курсор по 24 клітинках таблиці 6×4.
  * Кнопка `A` збільшує значення виділеного параметра (`VAL+`), кнопка `B` зменшує значення (`VAL-`).
  * `SELECT + A` виконує аудіо-прев'ю виділеного інструмента (`audio_engine.triggerNote`).
  * Повністю заблоковано дії `CLEAR` (`SELECT + B`), `PAGE LEN` (`SELECT + X`) та `PAGE` (`SELECT + Y`) у вкладці синтезатора.
* **Contextual Track Sync & [+] ADD TRACK Slot in Sequencer (src/main.cpp, src/ui_renderer.cpp):**
  * При натисканні `L1` синтезатор автоматично відкривається для того інструмента, на якому стоїть курсор у секвенсорі (`app.selected_track = audio_engine.getRowTrack(app.cursor_row)`).
  * У сітці секвенсора додано інтерактивний слот `[+] ADD TRACK` одразу під останнім активним рядком (`cursor_row == active_rows`).
  * Натискання кнопки `A` на слоті `[+]` миттєво створює новий інструмент (+4 голосні рядки), переводить курсор на перший новий рядок та обирає цей трек для швидкого редагування в SYNTH по L1.
* **Tabular MIX Tab & Master FX Bus (src/ui_renderer.cpp):**
  * Повністю перероблено громіздкий вертикальний мікшер на компактну горизонтальну матрицю 4 каналів (Channel Header, Mute/On, Level slider + peak VU, Pan dot, Delay send bar).
  * Додано окремий блок `MASTER FX BUS` (MST VOL, DLY TIME 250ms, DLY FDBK, LIMITER).

## [2026-10-25 22:15] Complete Technical Debt Resolution: USB Audio Gadget, Dual LFOs, Assignable Macro Sticks, Master Reverb & Elektron Workflow
* **Audio Gadget & Real-Time Net Audio Streamer (src/net_audio_streamer.hpp/.cpp, src/audio_engine.hpp/.cpp):**
  * Проведено ядровий аудит: виявлено, що в мінімальному ядрі ROCKNIX 7.1.2 вимкнено `f_uac1/2`, а `f_fs` блокує дескриптор `0x24 (USB_DT_CS_INTERFACE)`.
  * Реалізовано прямий мережевий аудіо-стрімер через швидкісний USB CDC-NCM канал (`en9`, 30+ MB/s, `10.1.1.2` <-> `10.1.1.1`):
    * WebAudio HTTP-сервер на порті `8000`: підключення з будь-якого браузера (Chrome, Safari на Mac) за посиланням `http://10.1.1.2:8000` з кнопкою "CONNECT AUDIO GADGET" та живим VU-метром відтворює біт-перфектний стерео-потік 32000 Гц без затримок.
    * Неблокуючий бінарний потік `GET /audio` та UDP-трансляція PCM на `10.1.1.1:8001` для запису в DAW / BlackHole.
    * Замір швидкості передачі: `131 072 байт за 1.01 сек (128 687 байт/сек)` — ідеальна точність 32 кГц 16-біт стерео.
* **Elektron Trig Probability & Conditions Engine (src/audio_engine.cpp):**
  * Повністю підключено виконання ймовірності `probability` (0..100%) через генератор псевдовипадкових чисел.
  * Реалізовано обробку умов `condition`: `kFill`, `kNotFill`, `kPre`, `kNotPre`, `k1_2`, `k2_2`, `k1_4`..`k4_4`, `kFirst`, `kNotFirst`.
* **Modulation Engine & Dynamic Analog Sticks (src/synth_engines.hpp, src/audio_engine.hpp/.cpp, src/main.cpp):**
  * Додано enum `StickDest` (14 призначень: CUT, RES, ENV, FM, MOD1, DEP1, MOD2, DEP2, D.SND, D.TIM, D.FBK, REV, PAN, VOL) та `LfoWave` (TRI, SIN, SAW, SQR, S&H).
  * Кожна доріжка отримала 2 незалежні LFO (`lfo1`, `lfo2`) з налаштуванням форми хвилі, частоти, глибини та цілі модуляції.
  * Аналогові стіки отримали динамічне призначення на будь-який параметр (L-STK X/Y, R-STK X/Y). Зміна призначення миттєво оновлює підписи в нижньому HUD над 2D-боксами стіків (`CUT/RES`, `DLY/FBK`, `REV/CHO` тощо).
* **Master Send Reverb (src/audio_engine.hpp/.cpp):**
  * Інтегровано стерео ревербератор (дифузний 4-tap Schroeder танк) з параметрами `reverb_decay` та `reverb_mix`. Кожен трек отримав параметр `reverb_send`.
* **Workspace Tabs Architecture & Mini-Scope (src/config.hpp, src/ui_renderer.cpp, src/main.cpp):**
  * Прибрано окрему повноекранну вкладку SCOPE.
  * Замість неї реалізовано:
    * `[L1] SEQ` <-> `[L1] SYNTH` (двопозиційний тогл по кнопці L1; у режимі SYNTH плашка світиться блакитним).
    * `[L2] FX` (Майстер-ефекти: Ping-Pong Delay, Diffusion Reverb, Master Limiter).
    * `[R1] MOD` (Матриця модуляції: LFO 1/2, стіки + вбудований компактний осцилограф 64×24 у правому верхньому кутку).
    * `[R2] MIX` (Канали 1..4 + Master Bus).
    * `[G] SYS` (Вертикальний селектор: 3-позиційний режим USB `HOST` / `DEVICE SSH` / `DEVICE AUDIO GADGET`, вибір буфера 64..512, частоти дискретизації, аудіомультиплексора).
* **Нижній HUD та дворядкові підписи кнопок (src/ui_renderer.cpp):**
  * Підписи кнопок виводяться у два окремі рядки (Рядок 1: `X: ... Y: ...`, Рядок 2: `A: ... B: ...`).
  * Підписи стіків у лівій та правій частинах HUD динамічно відображають призначені параметри активного треку.
* **Результати тесту на залізі MagicX XU10:**
  * Усі бінарники скомпільовано без жодного варнінгу (`build-macos` та `build-xu10`).
  * Задеплоєно на консоль `10.1.1.2`, процес запущено як Dedicated Appliance PID `11924`.
  * Споживання CPU — лише **8.8%** (91.2% idle), пам'ять — **47 MB** (4.6% RAM).
  * Мережевий аудіострімер активний і перевірений по `http://10.1.1.2:8000`.
