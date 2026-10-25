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


## [2026-10-25] Milestone 4 & 5: Modular Synth Engine Registry, 16-Step Sequencer with P-Locks, Stereo Delay & MIDI Clock PLL
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
