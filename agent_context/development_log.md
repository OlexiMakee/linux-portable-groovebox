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


## [2026-10-27] Milestone 11: Comprehensive Functional & Ergonomics Audit, Minimalist Pixel Art & Modal UI Overhaul
* **Глобальна стандартизація кнопок: `B = VAL+`, `A = VAL-`:**
  * Уніфіковано всі екрани (`SYNTH`, `FX`, `MIX`, `SYS`, `INSPECT`, `ARP`): кнопка B скрізь збільшує значення, A зменшує.
  * Реалізовано плавний автоповтор значень при утриманні кнопок A або B ($\ge 250$ мс з кроком 60 мс).
  * Утримання A або B на виборі `ENGINE` у `SYNTH` відкриває модальне випадаюче меню селектора рушіїв (`EngineSelectorPopup`).
* **Модальне вікно арпеджиатора (`ArpView`):**
  * Перетворено з повного екрана на центрований модальний попап (260×132 px) з рамкою, акцентним заголовком та збереженням видимого фону робочої області.
  * Підтримка закриття по повторному кліку лівого стіка `L3`, по `SELECT + B` або кнопкою `ARP.CLOSE`.
* **Мікшер (`MIX`) та вертикальна міні-індикація:**
  * Рендериться рівно стільки смуг каналів, скільки реально створено в проєкті (`active_rows_count_`), плюс рядок Master.
  * Нижня панель: вертикальні міні-VU для кожного активного каналу зліва направо, мініатюрний квадратний осцилограф (24×24) праворуч та Master VU.
  * Повністю усунено зайвий текстовий напис "SCOPE".
* **Синтезатор (`SYNTH`) та системне меню (`SYS`):**
  * Прибрано `TRK.TRANSP` та `TRK.GLIDE` з верхнього рядка. На їхнє місце додано селектор `TRK.WAVE_SAMPLE`: для синтезаторів вибирає форму хвилі осцилятора (Saw, Square, Tri, Sine, Noise), для семплерних рушіїв — семпл/кіт.
  * Перейменовано движок `kSampleDrums` $\rightarrow$ `kSampleOneShot`.
  * Усунено дублювання `LFO1.RATE`. Додано повноцінне призначення модуляції `LFO1.DEST` та `LFO2.DEST` у сітку.
  * Заборонено додавання/видалення доріжок у SYNTH (тільки в SEQ).
  * Системне меню `SYS` перетворено на 2D-таблицю 4×3 (`PROJ`, `SMPL`, `AUD`) з неінтерактивним інформаційним банером системної телеметрії (CPU, RAM, Temp, SR) у заголовку.
* **Секвенсор (`SEQ`):**
  * Додавання доріжок виконується строго по 1 через рядок `[+] ADD TRACK`.
  * Видалення поточної доріжки — тільки за комбінацією `SELECT + B` (`SEQ.DEL_TRACK_SAFE`) із захистом від випадкового натискання.
  * Прибрано підказку `R3: INIT` у секвенсорі.
* **Мінімалістичний піксель-арт (Elektron Style):**
  * У нижньому рядку HUD текст `PLAY` / `STOP` замінено на піксельні піктограми Play (трикутник ▶ 5×5) та Stop (квадрат ■ 4×4).
  * Створено піктограми форм хвиль `iconWave` та віджет вертикального індикатора `verticalMeter`.
* **Деплой на MagicX XU10:**
  * Згенеровано оновлений тур знімків `tour_01..11.png`.
  * Бінарник зібрано під `aarch64` і перезапущено на консолі (PID 60062, KMSDRM, ALSA 32k).

## [2026-10-26] Milestone 10: Analog Stick Clicks (L3/R3), Smart Musical Randomizer & Cursor-Relative Arp Apply
* **Активація апаратних кліків стіків L3 / R3:**
  * Додано прапорці `l3_pressed` та `r3_pressed` у `InputState` та `InputManager`. Усунено фіктивний `dummy_edge` у `pollEvdev()`.
  * Прив'язано `L3`: короткий тап відкриває / закриває каскадний арпеджиатор (`ARP.TOGGLE`), довге утримання ($\ge 200$ мс) активує живий Elektron Fill Mode (`FILL.ON` / `FILL.OFF`).
  * Прив'язано `R3`: одиночний клік скидає параметр (`PARAM.DEFAULT`), а клік із затиснутим `SELECT` (або в екранах SYNTH/ARP) запускає рандомайзер (`SYNTH.RANDOMIZE` / `ARP.RANDOMIZE`).
  * У нижньому рядку HUD додано підказки: `L3:ARP` (або `L3:BCK` в Arp), `R3:RND` (або `R3:INIT`), з підсвіткою активного стану жовтим кольором `kParamTrig`.
* **Музично-зважений розумний рандомайзер (Smart Musical Randomizer):**
  * Для `SYNTH`: калібровані діапазони огинаючих ADSR (Attack 2..80 мс, Decay 60..600 мс, Sustain 10..85%), Cutoff/Resonance SVF-фільтра та помірні посили на Delay/Reverb.
  * Для `ARP`: випадковий вибір із 9 музичних ладів, когерентних акордів, прогресій ступенів, патернів руху та евклідових сіток.
  * Додано параметри `TRK.RND` у сітку `SYNTH` та `ARP.RND` у сітку `ARP`.
* **Прив'язка генерації Arp до позиції курсора (`ARP.COMMIT`):**
  * Генерація секвенції тепер починається від поточного активного кроку курсора (`cursor_step`), заповнюючи кроки до кінця активної довжини рядка (`row_len`).
  * Кнопка дії в інтерфейсі динамічно показує точку старту: `APPLY @ST05 [A]`.
* **Оптимізація пам'яті PatternManager:**
  * Переведено сховище 128 патернів з масиву на стеці (8 МБ) на динамічний `std::vector` у купі, усунувши переповнення стека на macOS та вбудованому Linux.
* **Збірка та деплой на MagicX XU10:**
  * Успішна крос-компіляція та запуск на консолі (PID 42179, KMSDRM, ALSA 32k). Згенеровано оновлені скріншоти `tour_01..11.png`.

## [2026-10-26] Milestone 9: Dedicated R1 Performance, Integrated MIX Scope, Chorus, Overdrive & Polymetry Restoration
* **Повна реорганізація верхніх вкладок під 4 плечові бампери (70/70/70/70/40 px):**
  * `[L1]` $\rightarrow$ `SEQ`: 16-рядковий матричний секвенсор із поліметрією.
  * `[L2]` $\rightarrow$ `SYNTH`: Звуковий дизайн синтезатора (осцилятори, фільтр, ADSR, LFO) + Arp.
  * `[R2]` $\rightarrow$ `MIX / FX`: Перше натискання відкриває `MIX`, друге — `FX` (Master Send FX).
  * `[R1]` $\rightarrow$ `PERF`: Виділена пряма апаратна кнопка для живого режиму виступу (4 великі педи ABXY, швидкі октави D-Pad, живі макроси стіків).
  * `[G]` $\rightarrow$ `SYS`: Системні налаштування, буфер, телеметрія та банк патернів.
* **Інтеграція компактного Scope та VU-метрів у MIX:**
  * У `src/ui/views.cpp` додано рендер компактного 26px осцилографа та стереофонічного VU-метра Master Out L/R безпосередньо під 5 рядками мікшера. Усунено потребу у великій надлишковій окремій вкладці SCOPE.
* **Майстер-ефекти Stereo Chorus та Tanh Overdrive:**
  * У `src/audio_engine.hpp/.cpp` додано DSP-алгоритм подвійного LFO-модульованого хоруса (5–25 мс) та нелінійного сатуратора $y = \tanh(k \cdot x)$ з 1-pole tilt-фільтром тембру.
  * Зареєстровано 5 параметрів `FX.CHR_MIX`, `FX.CHR_RATE`, `FX.CHR_DEP`, `FX.DRIVE`, `FX.DRV_TONE` у `ParamRegistry` та сітці `FX`.
* **Відновлення та розширення поліметрії в SEQ:**
  * Додано дії `SEQ.LEN_INC` та `SEQ.LEN_DEC` для швидкої зміни довжини активної доріжки по `SELECT + D-Pad Right/Left`.
  * Відновлено дію `SEQ.CYCLE_LEN` на кнопці `SELECT + X`.
* **Деплой на XU10:**
  * Згенеровано оновлені скріншоти `tour_01..04.png`, `tour_10_perf.png`.
  * Бінарник успішно зібрано під `aarch64` і запущено на MagicX XU10 (PID 31064), навантаження CPU 6.5%.

## [2026-10-26] Milestone 8: Live Performance Mode, Dual Sequential Arpeggiator & Procedural Algorithmic Engine
* **Апаратна корекція індикації ABXY та геометрії вкладок:**
  * У `src/ui/widgets.cpp` переприв'язано піктограми ABXY до реального маркування кнопок на корпусі XU10 (верхня: Y, ліва: X, права: B, нижня: A).
  * У `src/ui/layout.cpp` реалізовано симетричні ширини вкладок верхнього рядка: `[L1]` 70px, `[L2]` 70px, `[R2]` 70px, `[R1]` 70px, `[G]` 40px (разом рівно 320px).
* **Модуль каскадного арпеджиатора (`src/arpeggiator.hpp/.cpp`):**
  * Створено `Arpeggiator` та таблицю квантування для 11 музичних ладів (Major, Minor, Harmonic Minor, Dorian, Phrygian, Lydian, Mixolydian, Pentatonic Minor/Major, Blues, Chromatic).
  * **Arp 1 (Гармонійний макро-генератор):** Дільники 1/1, 1/2, 1/4, 1/8, типи акордів (Root, Octaves, Power 5th, Triad, 7th, Sus4, Add9) та гармонічні прогресії ступенів (I-IV-V-I, i-VI-III-VII тощо).
  * **Arp 2 (Мелодійний мікро-генератор):** Дільники 1/4, 1/8, 1/16, 1/32, 1/16T, патерни обходу нот (Up, Down, Up-Down, Ping-Pong, Converge, Diverge, Random, Brownian Walk, Euclidean).
  * **Процедурний алгоритм (Euclidean + Turing Machine):** 16-бітний LFSR-регістр мутації (0% = жорсткий луп, 100% = безперервна мутація) та генератор евклідових ритмів Бйорклунда ($K$ ударів на $N$ кроків).
  * **Commit to Sequencer:** Метод `commitToTrack()` запікає згенеровані ноти, гейти та слайди прямо у кроки вибраного рядка секвенсора.
* **Режим живого виступу `PerfView` (`ViewId::kPerf`):**
  * Вкладка `[L1]` тепер циклічно перемикає три інструментальні режими: `SEQ` $\rightarrow$ `SYNTH` $\rightarrow$ `PERF` $\rightarrow$ `SEQ`.
  * Реалізовано 4 великі чутливі педи у форматі діаманта (A/B/X/Y) з миттєвою підсвіткою та назвами нот у поточному ладі (або BD/SD/CH/OH для драмів).
  * Інтегровано живі візуалізатори осей аналогових стіків: лівий стік (Pitch Bend / Cutoff) та правий стік (Resonance / FX Send).
  * D-Pad Up/Down перемикає октави гри (C1..C6), D-Pad Left/Right перемикає активний трек.
* **Вікно налаштування генератора `ArpView` (`ViewId::kArp`):**
  * Зареєстровано 12 параметрів `ARP.*` у `ParamRegistry` та дія `ARP.COMMIT`.
  * Доступ із секвенсора через комбінацію `SELECT + X`.
* **Збірка, тести та деплой на консоль:**
  * Згенеровано 11 тестових знімків `tour_01..11.png` (зокрема [tour_10_perf.png](file:///Users/oleksii/.gemini/antigravity/brain/a2583e1d-b2ce-4f26-885f-f7402638aa14/tour_10_perf.png) та [tour_11_arp.png](file:///Users/oleksii/.gemini/antigravity/brain/a2583e1d-b2ce-4f26-885f-f7402638aa14/tour_11_arp.png)).
  * Успішна крос-компіляція та деплой на MagicX XU10 (`10.1.1.2`): процес `xu10_groovebox` запущено (PID 18682), навантаження CPU лише 6.5%, RAM 47.4 МБ.

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
* Успішний деплой на консоль MagicX XU10 (`10.1.1.2`): процес `xu10_groovebox` запущено (PID 30479), навантаження CPU 4.4%, RAM 47 МБ, WebAudio потік на `:8000` активний.

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

## [2026-10-27] Milestone 12: Yamaha DX7 6-OP FM Engine, 8-Slot Sample Drum Rack & ROM Pack Curation
* **Курація та структурування бібліотеки семплів (res/samples/ & res/dx7/):**
  * Створено автоматичний пайплайн `scripts/curate_samples.py` для відбору найкращих семплів з Desktop (`Drum Machines` та `Cycle Wave Forms`).
  * Відібрано та нормалізовано 8 класичних наборів ударних:
    * `01_tr808` (Roland TR-808)
    * `02_tr909` (Roland TR-909)
    * `03_tr707` (Roland TR-707)
    * `04_linndrum` (LinnDrum LM-2)
    * `05_oberheim_dmx` (Oberheim DMX)
    * `06_cr78` (Roland CR-78 CompuRhythm)
    * `07_sp1200` (E-mu SP-1200)
    * `08_tr606` (Roland TR-606 Drumatix)
    * Кожен кіт структуровано за стандартом 8 слотів: `bd.wav`, `sd.wav`, `ch.wav`, `oh.wav`, `cp.wav`, `rim.wav`, `tom.wav`, `perc.wav`.
  * Відібрано структурований набір одноциклових хвиль AKWF (`saw`, `square`, `triangle`, `sine`, `organ`, `epiano`, `bass`, `chip`).
  * Згенеровано `src/rom_samples.hpp` із вбудованим у бінарник набором TR-808 для нульової залежності від SD-карти.
  * Завантажено та розгорнуто 5 класичних картриджів DX7 (.syx): `01_Yamaha_ROM1A`, `02_Brian_Eno`, `03_Dexed_01`, `04_Synprez_Basses`, `05_Synprez_Keys`.
* **Заміна analog-drum на 8-слотовий Drum Rack (kDrumRack):**
  * Замінено старий синтетичний драм-рушій на семплерний Drum Rack з підтримкою 8 слотів на базі `SamplerEngine`.
  * Реалізовано потоковий лінійний ресемплінг з дробовим акумулятором фази, зміною висоти тону (`TUNE`), огинаючою спаду (`DECAY`), маршрутизацією через SVF-фільтр, овердрайв та шини Delay/Reverb.
  * Вибір кіта через `TRK.WAVE_SAMPLE` (TR-808 <-> TR-909 <-> TR-707 <-> LINN <-> DMX <-> CR-78 <-> SP1200 <-> TR-606).
* **Інтеграція рушія Yamaha DX7 6-OP FM (src/dx7_engine.hpp/.cpp):**
  * Повна реалізація 6-операторного FM-синтезу з усіма 32 класичними матричними алгоритмами маршрутизації фазової модуляції.
  * Швидкий табличний синус (LUT 4096 записів з лінійною інтерполяцією) та точні 4-стадійні DX7-огинаючі для кожного оператора.
  * Парсер 128-байтних войсів DX7 зі стандартних 4104-байтних SysEx (.syx) картриджів з вбудованим фабричним банком ROM1A (32 класичні пресети: `E.PIANO 1`, `BASS 1`, `STRINGS 1`, `BRASS 1`, `SYN-LEAD 1`, `TUB BELLS`, `MARIMBA` тощо).
  * Параметри рушія в інтерфейсі `SYNTH`: `PRESET` (1..32 з відображенням назви патча), `CUTOFF`, `BRIGHT`, `DECAY`, `ALGO` (1..32 оверрайд алгоритму), `FEEDBCK`.
* **Верифікація на залізі MagicX XU10:**
  * Успішна крос-компіляція бінарника `build-xu10/xu10_groovebox` та деплой на консоль (PID `67984`).
  * Семпли та банки розгорнуто у `/storage/roms/groovebox/`.
  * Навантаження CPU становить лише **9.1%** (94.5% idle!), споживання пам'яті — **71.4 MB** (7.1% RAM). Звуковий вивід ALSA 32k без жодного XRUN.

## [2026-10-27] Milestone 13: Specialized Synth UI Parameters, Chorus/Overdrive Integration, Sequencer Checkerboard & SELECT Shortcuts
* **Спеціалізація параметрів вкладки SYNTH для кожного рушія (src/ui/views.hpp, src/ui/views.cpp, src/ui/layout.cpp):**
  * Впроваджено динамічну генерацію рядків таблиці параметрів `dynamic_rows` у `GridSpec` та метод `GridView::resolveRows()`, що адаптує інтерфейс під тип рушія обраного треку:
    * `kDrumRack`: Kit, Tune, Filter, Drive, Overdrive, Drv Tone, Chr Mix, Reverb, ADSR, Sends, Pan, Level.
    * `kDx7`: Preset, Algo, Feedback, Bright, Cutoff, Decay, Chr Mix, Overdrive, ADSR, LFO1, Sends, Pan, Level.
    * `kAcid303`: Cutoff, Res, Env Mod, Accent, Drive, Drv Tone, Overdrive, Chr Mix, Decay, Slide, Sends, Pan, Level.
    * `kFm2Op`: Wave, Mod Ratio, Mod Index, Feedback, Filter, Overdrive, Chr Mix, Reverb, ADSR, Sends, Pan, Level.
    * `kChordPad`: Root, Chord, Inversion, Voicing, Filter, Res, Drive, Overdrive, ADSR, Sends, Pan, Level.
    * `kSampleOneShot`: Sample Sel, Tune, Start, Loop, Filter, Overdrive, Chr Mix, Reverb, ADSR, Sends, Pan, Level.
    * `kSliceSampler`: Slices, Slice Sel, Reverse, Pitch, Filter, Overdrive, Chr Mix, Reverb, ADSR, Sends, Pan, Level.
    * `kVaMono`: Wave, Sub, Unison, Noise, Cutoff, Res, Overdrive, Chr Mix, ADSR, Sends, Pan, Level.
    * `kMidiOut`: Channel, Prog Chg, Bank MSB, Bank LSB, CC1, CC2, CC3, CC4, Gate Len, Transp, Octave, Velocity.
* **Інтеграція Chorus та Overdrive у SYNTH та MIX (src/ui/layout.cpp, src/ui/actions.cpp):**
  * У вкладці SYNTH додано параметри `FX.DRIVE`, `FX.DRV_TONE`, `FX.CHR_MIX` прямо у блоки тембру/кольору.
  * У вкладці MIX додано виділений рядок `BUS` з прямим доступом до `OVERDRIVE`, `DRV TONE`, `CHR MIX`, `CHR RATE` поруч із шиною `MST`.
* **Регулювання довжини треку кроком у 4 та шахова 2D-сітка секвенсора (src/audio_engine.cpp, src/ui/actions.cpp, src/ui/views.cpp):**
  * `SEQ.LEN_INC` та `SEQ.LEN_DEC` тепер змінюють довжину кроком у 4 ($4 \le \text{len} \le 128$) для зручного створення поліритмів.
  * У фоні секвенсора додано шахову 2D-сітку у тонких відтінках сірого (`((s / 4) + r) % 2`), що візуально розбиває трек на 4-крокові такти з яскравими точками долей та чіткою лінією межі треку.
* **Візуалізація тогла R2 MIX/FX та шорткати SELECT (src/ui/ui_shell.cpp, src/ui/keymap.cpp, src/ui/actions.cpp):**
  * Додано пурпурний акцент `kAccentPurple` (`#DC3CFF`) для вкладки `FX`.
  * У вкладці MIX відображається плашка `[R2] MIX/FX` (помаранчевий колір), а при повторному натисканні — `[R2] FX/MIX` (пурпурний колір).
  * Реалізовано шорткати `SELECT + L1, L2, R2, R1`:
    * `SELECT + L1` $\rightarrow$ Step Inspector
    * `SELECT + L2` $\rightarrow$ Arpeggiator Modal
    * `SELECT + R2` $\rightarrow$ Oscilloscope View
    * `SELECT + R1` $\rightarrow$ Performance Pads View
  * При утриманні `SELECT` весь верхній TabBar динамічно перемикається на відображення жовтих рамок (`kParamTrig` `#FFD600`) з підписами призначених швидких вікон (`[L1] INSP`, `[L2] ARP`, `[R2] SCOPE`, `[R1] PERF`, `[G] SYS`), повністю виключаючи неінтуїтивні "приховані" комбінації.
* **Повна ревізія тур-скріншотів та деплой на MagicX XU10:**
  * Усі 13 скріншотів згенеровано та візуально верифіковано: відсутні накладання шрифтів, рамки та модальні вікна центровані, контрастність сітки секвенсора оптимальна для 320x240 LCD.
  * Успішний крос-деплой на апаратну консоль MagicX XU10 (PID `75992`). Споживання CPU ~**12%**, пам'ять — **71.5 MB** (7.1% RAM). Звуковий рушій ALSA 32k без затримок та трісків.

## [2026-10-27] Milestone 14: 1-Track per Instrument Sequencer, Step Editor Navigation & Smooth Horizontal Scroll
* **Вхідний роутер та Step Editor (src/ui/keymap.cpp, src/ui/layout.cpp):**
  * Додано автоповтор (Repeat/Hold acceleration) для кнопок D-Pad (затримка 220 мс, інтервал 75 мс), відновлено швидку навігацію утриманням стрілок.
  * Виправлено конфлікт утримання кнопки `A`: у Step Editor (колишній Step Inspector) автоповтор `PARAM.DEC` для кнопки `A` повністю вимкнено.
  * Переведено Step Editor на роздільне керування: `D-Pad Up/Down` — перехід між параметрами списку, `D-Pad Left/Right` — зміна значення (`VAL-` / `VAL+`). Кнопка `A` тепер лише перемикає крок (`SEQ.TRIG`), а `B` закриває редактор (`INSPECT.CLOSE`).
  * Перейменовано модальне вікно на `STEP EDITOR`.
* **Секвенсер «1 інструмент = 1 доріжка» та плавний скрол (src/audio_engine.cpp, src/ui/views.cpp, src/ui/actions.cpp, src/ui/ui_shell.cpp):**
  * Прибрано автоматичне розгортання 8 рядків на один драм-кіт. Дефолтний проект ініціалізується 3 окремими доріжками: `01SMP` (Kick), `02303` (Bassline), `03DX7` (FM Keys) + слот `[+] ADD TRACK`.
  * Висоту рядків збільшено (до 20px) для кращої читабельності та ергономіки на 320x240.
  * Прибрано кнопку `Page`: реалізовано плавний покроковий горизонтальний скрол сітки секвенсера за курсором при русі праворуч/ліворуч.
  * Додано підтримку розгортання режиму `REC` по кнопці `L1` з динамічною індикацією `[L1] REC` / `[L1] SEQ` у таб-барі.
* **Глобальний автоматизований тест функціоналу та UI (tools/auto_test.cpp):**
  * Створено автономний тестовий стенд `auto_test` із 58 строгими перевірками.
  * **DSP & Engines (1/4):** верифіковано синтез усіх 9 рушіїв без нулів та NaN, роботу сендів Reverb/Delay/Chorus/Overdrive та захист вихідного майстер-лімітера від кліпінгу.
  * **Секвенсер (2/4):** перевірено Elektron 3-стадійний тригер (Note -> Param Trig -> Off), поліритмію кроком у 4, додавання та видалення треків по одному.
  * **Input & Step Editor (3/4):** перевірено роботу автоповтору D-Pad (220мс/75мс), безпеку утримання кнопки `A` без дрейфу параметрів, плавний скрол `seq_scroll_step` за межі 32 кроків, тогл `REC` та комбінації `SELECT + L1/L2/R2/R1`.
  * **Рендеринг (4/4):** верифіковано безпомилковий рендеринг усіх 9 вкладок, модальних оверлеїв та спеціалізованих матриць SYNTH для кожного з 9 рушіїв. Результат: **58/58 PASSED [OK]**.

## [2026-10-27] Milestone 15: Polyphonic Chords, Mini Chord Piano-Roll, Sampling Workstation & Async USB Gadget
* **Поліфонія кроку та міні-піанорол (src/synth_engines.hpp, src/audio_engine.hpp/.cpp, src/ui/views.cpp, src/ui/params.cpp):**
  * Розширено структуру `SequencerStep` масивом `chord_notes` (до 4 нот на крок, спільний velocity).
  * Додано параметри `STEP.CHORD1`, `STEP.CHORD2`, `STEP.CHORD3` у `ParamRegistry` та виведено в список `STEP EDITOR`.
  * У правому секторі Step Editor додано міні-піанорол на 1 октаву (`CHORD ROLL`) за прикладом Digitakt: активні ноти акорду підсвічуються яскравим бірюзовим кольором (`C`, `C#`, `D` ... `B`).
* **Робоча станція семплювання (SAMPLING) замість Scope (src/ui/views.hpp/.cpp, src/ui/layout.cpp, src/ui/actions.cpp):**
  * Вкладку `SCOPE` замінено на повноцінну станцію `[R1] SAMPLE` (`ViewId::kSample`). Живий `PERF` викликається через `SELECT + R1`.
  * Реалізовано графічний інтерфейс Digitakt-style:
    * Велике вікно хвилеформи з підсвіткою активної зони семпла між маркерами `TRIM START` (помаранчевий) та `TRIM END` (бірюзовий).
    * Таблиця параметрів (2x4): `SRC`, `REC ARM`, `MODE`, `THRESH`, `TRIM ST`, `TRIM END`, `ZOOM`, `ASSIGN`.
    * Вертикальний світлодіодний VU-метр рівня вхідного аудіосигналу праворуч.
    * Якщо буфер порожній — відображається живий монітор сигналу для налаштування гучності перед записом.
  * Меню `SYS` очищено від опцій семплювання — вони повністю перенесені у вкладку `SAMPLING`.
* **Асинхронний USB Role Switch (src/hw_monitor.cpp):**
  * Виклики перемикання USB ролі (`host` / `device_ssh` / `device_audio`) винесено у відокремлений фоновий потік (`std::thread(...).detach()`), що повністю усунуло будь-які зависання UI під час реконфігурації UDC та модулів ядра на ROCKNIX.
* **Глобальний автоматизований тест та верифікація:**
  * Тестовий стенд `auto_test` розширено до 65 перевірок: верифіковано роботу поліфонічних акордів кроку, навігацію та параметри `SampleView`, неблокуюче перемикання USB ролей.
  * Результат тестування: **65/65 PASSED [OK]**.
  * Оновлено тур-скріншоти `tour_01_seq.png`, `tour_05_sample.png`, `tour_06_sys.png`, `tour_07_inspector.png` та підтверджено їх візуальну бездоганність.
  * Успішний крос-деплой на апаратну консоль MagicX XU10: процес запущено (PID `85013`), споживання CPU ~**10.9%**, пам'ять — **72 MB** (7.1% RAM). Звуковий тракт ALSA 32k стабільний.

## [2026-10-27] Milestone 16: Expanded REC Mode (Drum Lanes & Piano Roll) and Live Looper Auto-Slicing
* **Режим детального розгортання доріжки REC (src/ui/views.cpp, src/ui/actions.cpp, src/ui/ui_context.hpp):**
  * По натисканню кнопки `L1` (перемикач `[L1] REC` / `[L1] SEQ`) активний інструмент миттєво розгортається у спеціалізований детальний редактор:
    * **Drum Rack (8 Drum Lanes):** 8 рядків для індивідуальних ударів (`01BD`, `02SD`, `03CH`, `04OH`, `05CP`, `06RM`, `07TM`, `08PC`), підтримка поліфонічного накладання кількох ударів на один крок, навігація `D-Pad Up/Down` по лейнах, `D-Pad Left/Right` по кроках зі збереженням плавного скролу.
    * **Melodic Synths (12-Semitone Piano Roll):** повноцінний хроматичний піанорол від `C` до `B` із затемненням чорних клавіш (`A#`, `G#`, `F#`, `D#`, `C#`), перемиканням октав клавішами `X` (OCT-) та `Y` (OCT+), прямим тригером нот клавішею `A` та очищенням кроку клавішею `B`.
* **Live Looper та автоматичне нарізання на 8 слайсів (src/sampler_engine.hpp/.cpp, src/ui/params.cpp):**
  * Додано темпо-синхронізований запис `armSyncedRecording` (1, 2, 4 бари за темпом BPM) з автоматичною зупинкою та розбиттям на 8 слайсів.
  * Реалізовано методи `assignTrimmedToSlot` (призначення обрізаного або нормалізованого фрагмента в будь-який слот драм-машини) та `sliceToDrumSlots` (миттєвий розподіл 8 слайсів записаного лупу по всіх 8 слотах `DrumRack`).
  * Параметри `SMP.MODE` та `SMP.ASSIGN` зв'язано з логікою синхронізованого запису та розподілу слайсів.
* **Автоматизоване тестування та верифікація:**
  * Тестовий стенд розширено до **69 строгих перевірок** у `tools/auto_test.cpp`: верифіковано тригер нот у Piano Roll, тригер ударів у Drum Lanes та розподіл слайсів у слоти.
  * Усі 69 перевірок успішно пройдено: **69/69 PASSED [OK]**.
  * Згенеровано та візуально верифіковано нові тур-скріншоти `tour_01_rec_drums.png` та `tour_01_rec_piano.png`.
  * Успішний деплой на консоль MagicX XU10: процес запущено як Dedicated Appliance (PID `90647`), споживання CPU ~**13.0%**, пам'ять — **72.5 MB** (7.2% RAM). Аудіовихід ALSA 32k стабільний, без деградацій.

## [2026-10-27] Milestone 17: Live Looper Overdub Engine and 8-Slice Waveform Visualizer
* **Live Looper Overdub Engine (src/sampler_engine.hpp/.cpp):**
  * Впроваджено стан `RecState::kOverdub`, змінну фідбеку накладання `overdub_feedback_` (0.20..1.00) та циклічну позицію відтворення `loop_play_pos_`.
  * Реалізовано алгоритм закільцьовування та підмішування аудіосигналу в реальному часі ($s_{prev} \cdot fb + s_{in}$) без затримок та переривань потоку.
  * Додано методи `startOverdub()` та `clearRecordedSample()`.
* **Візуалізація 8 слайсів та підсвітка активного слайсу (src/ui/views.cpp, src/ui/params.cpp):**
  * У великому вікні хвилеформи `SampleView` відмальовуються 8 вертикальних пунктирних ліній поділу на слайси та мітки `S1`..`S8`.
  * Реалізовано live-підсвітку активного відтворюваного слайсу (`activePlaybackSlice`) напівпрозорим прямокутником морської хвилі з помаранчевим виділенням поточної мітки.
  * Параметр `SMP.ARM` розширено підтримкою циклу `OFF` -> `ARM` -> `REC` -> `DUB` -> `LOOP` -> `CLEAR`.
  * Додано параметр `SMP.DUB_FBK` для точного регулювання відсотка накладання фідбеку лупера.
* **Автоматизоване тестування та деплой на MagicX XU10:**
  * Тестовий стенд розширено до **73 перевірок** у `tools/auto_test.cpp`: верифіковано стан `kOverdub`, зміну фідбеку, відстеження активного слайсу та очищення буфера лупера. Результат: **73/73 PASSED [OK]**.
  * Оновлено тур-скріншот `tour_05_sample.png` із демонстрацією 8 слайсів та підсвіткою активного слайсу `S3`.
  * Успішний деплой на консоль MagicX XU10 (PID `99470`), споживання CPU ~**13.0%**, пам'ять — **72.4 MB** (7.2% RAM). Аудіотракт ALSA без помилок.

## [2026-10-27] Milestone 18: Unified 16-Track Model, Dynamic Mixer Synchronization, Engine Picker & Sample Pool Browser
* **Єдиний 16-доріжковий масив та усунення десинхронізації Секвенсера й Мікшера (src/synth_engines.hpp, src/audio_engine.hpp/.cpp):**
  * `kMaxTracks` та `kTracksCount` розширено до 16 доріжок із співвідношенням 1 інструмент = 1 трек (`kVoicesPerTrack = 1`).
  * Ліквідовано хардкод 4-доріжкового мікшера: додано методи `addTrack(engine, name)`, `deleteTrack(idx)`, `setTrackName(track, name)`, `getTrackName(track)`.
  * Додано поле `char name[16]` у `TrackParameters`. За замовчуванням створюються 3 чисті треки (T1: KICK 808 [SAMPLER], T2: 303 BASS [ACID 303], T3: DX7 KEYS [DX7 6-OP]).
  * Розширено телеметрію аудіорушія (`AudioEngineTelemetry`): поле `active_tracks`, масив рівнів `std::array<float, 16> track_levels` та назви треків `track_names`.
* **Динамічний мікшер та закріплена нижня панель індикації (src/ui/layout.cpp, src/ui/views.hpp/.cpp):**
  * У вкладці `MIX` смуги каналів генеруються динамічно через `dynamic_rows`: рівно стільки каналів, скільки реально існує в проєкті ($N \in 1..16$), плюс смуги `BUS` та `MST`.
  * Усі назви каналів відповідають іменам у секвенсері (`01:KICK`, `02:303`, `03:DX7`). Кожен канал має незалежні параметри `LEVEL`, `PAN`, `DLY`, `REV`, `MUTE`.
  * Запроваджено закріплений знизу над HUD лейаут індикації (висота 26px): квадратний міні-осцилограф (22px), мініатюрні світлодіодні стовпчики рівнів для кожного треку (висота 22px, що відповідає осцилографу) та Master L/R VU.
  * Реалізовано вертикальний скрол таблиці каналів: якщо кількість треків перевищує 7 видимих рядків, таблиця плавно прокручується за курсором вниз/вгору зі збереженням нижньої панелі індикації. Цей самий закріплений лейаут підтримується у вкладці `FX`.
* **Модальне меню вибору рушіїв (Engine Picker Modal) та швидке перемикання у SYNTH:**
  * По кліку на слот `[+] ADD TRACK` у секвенсері з'являється спливаюче вертикальне модальне вікно селектора рушіїв (Sampler, Acid 303, DX7, VA Mono, 2-OP FM, Chord Pad, Loop Slicer, MIDI Out). Кнопка `A` підтверджує додавання треку, `B` закриває вікно.
  * У вкладці `SYNTH` додано швидке гортання створених треків через `SELECT + D-Pad Left/Right` та `SELECT + X/Y` без необхідності повернення до секвенсера. Заголовок відображає `SYNTH [T01/03: KICK 808 - SMPL ONESHOT] | < SEL+X/Y >`.
* **Семпл-браузер та доступ до пулу семплів файлової системи (src/sampler_engine.hpp/.cpp, src/ui/actions.cpp, src/ui/params.cpp, src/ui/ui_shell.cpp):**
  * Відмова від спеціалізації драм-реку: запроваджено універсальний семплер `kSampleOneShot` з підтримкою індивідуального буфера семплу для кожного з 16 треків (`trackSample(t)`).
  * Реалізовано модальне вікно `Sample Pool Browser` (260×144 px) з доступом до файлової системи (`res/samples/drums/` та `res/samples/waves/`).
  * Підтримка навігації по теках (`[DIR] 01_tr808`, `[..] PARENT DIR`), слухового прев'ю `.wav` файлів під час вибору (`Audition Preview`), завантаження у пам'ять треку та автоіменування треку назвою вибраного семплу.
* **Глобальне тестування, скріншот-тур та крос-компіляція:**
  * Тестовий стенд `auto_test` розширено з 73 до **96 автоматизованих строгих тверджень**: верифіковано створення 16 треків, іменування, телеметрію рівнів усіх каналів мікшера, виклики Engine Picker, Sample Browser та рендеринг закріпленої панелі індикації.
  * Результат тестування: **96/96 PASSED [OK]**.
  * Оновлено повний пакет скріншотів: `tour_01_seq.png`, `tour_02_synth.png`, `tour_04_mix.png`, `tour_13_engine_menu.png`, `tour_14_sample_browser.png`.
  * Успішна крос-компіляція aarch64 через Zig C++ під ROCKNIX: бінарник `build-xu10/xu10_groovebox` зібрано з нульовими попередженнями та помилками.

## [2026-10-27] Milestone 19: Pure Unified Sampler, Complete DrumRack Elimination, Robust WAV Chunk Parser & Unblocked Browser Navigation
* **Чанковий RIFF/WAVE парсер та виправлення завантаження всіх 79 семплів (src/sampler_engine.cpp):**
  * Замінено старий 44-байтовий жорсткий заголовок на динамічний сканер блоків RIFF: парсер ітерує чанки та коректно пропускає метадані `JUNK`, `LIST`, `bext` за формулою `chunk_size + (chunk_size & 1)`.
  * Додано підтримку 16-бітних, 24-бітних (знакові цілі 3 байти) та 32-бітних (PCM/Float32) WAV-аудіофайлів для моно та стерео.
  * Усунуто причину відмови завантаження семплів у пулі (всі 79 семплів тепер зчитуються зі 100% надійністю).
* **Повна ліквідація DrumRack та перехід на чистий універсальний SAMPLER (src/synth_engines.hpp, src/audio_engine.cpp, src/ui/actions.cpp, src/ui/params.cpp, src/ui/views.cpp, src/ui/layout.cpp):**
  * Повністю вилучено движок `DrumRack` з кодової бази, меню селектора рушіїв та сіток параметрів.
  * Усі 16 доріжок є рівноправними абстрактними інструментами з номерами від 1 до 16. Для роботи з будь-якими звуками (ударні, баси, інструменти, вокал) використовується уніфікований `SAMPLER` (`kSampleOneShot`).
  * Голоси `SAMPLER` відтворюють семпл безпосередньо з буфера треку `trackSample(t)` з базовою нотою 48 (C2) без сторонніх зсувів висоти тону.
  * При старті проєкту доріжка 1 автоматично завантажує стандартний кік `01_tr808/bd.wav` у свій буфер доріжки.
  * У REC-режимі ліквідовано 8-рядкову барабанну сітку: усі 16 доріжок використовують єдиний 12-півтоновий хроматичний Piano Roll із підтримкою акордів.
  * Параметр `STEP.SLIDE` зроблено універсальним для всіх движків.
* **Розблокування навігації у браузері семплів (src/ui/actions.cpp, src/ui/keymap.cpp, src/ui/ui_shell.cpp):**
  * Дія `SAMPLE_BROWSER.CLOSE` безумовно закриває модальне вікно браузера при натисканні кнопки `B` (`sample_browser_open = false;`), виключаючи блокування користувача в інтерфейсі.
  * Додано ергономічне керування з D-pad: `Left` здійснює вихід на рівень вище (`PARENT`), `Right` та `A` входять у теку або завантажують семпл із прев'ю-нотою C2.
  * Нижні підказки оновлено: `[A] SELECT  [B] CLOSE  [<] PARENT`.
* **Автоматизоване тестування, скріншот-тур та верифікація:**
  * Тестовий стенд `auto_test` розширено до **97 строгих перевірок**: перевірено чанковий парсер WAV на реальному файлі `01_tr808/bd.wav`, наявність аудіосигналу на виході аудіорушія при виклику ноти 48, відсутність DrumRack у переліку рушіїв та безвідмовне закриття браузера на кнопку B.
  * Результат тестування: **97/97 PASSED [OK]**.
  * Оновлено тур-скріншоти: `tour_14_sample_browser.png`, `tour_13_engine_menu.png`, `tour_01_rec_sampler.png`.

## [2026-10-28] Milestone 20: 4-Voice Polyphony Across All Engines, Freeverb Algorithmic Reverb, Dual LFOs with TRIG/FREE Mode & Parameter Disentanglement
* **Справжня 4-голосна поліфонія для всіх движків (src/synth_engines.hpp, src/audio_engine.hpp/.cpp):**
  * `kVoicesPerTrack` збільшено з 1 до 4, загальний пул розширено до 64 голосів (`kTotalVoices = 64`).
  * Реалізовано динамічний розподіл 4 голосів на кожен трек (`allocateVoice`) з підтримкою Voice Stealing (LRU/Release-level) та одночасного звучання акордів до 4 нот.
  * `advanceSequencer` та `previewStep` активують кореневу ноту та слоти акордів 1..3 одночасно через незалежні голоси доріжки.
  * У `renderBlock` підсумовуються виходи всіх 4 голосів доріжки перед обробкою фільтра, поканальних сендів на ділей/ревербератор та майстер-панорамування. Поліфонія тепер повністю функціонує на DX7, Sampler, VA Saw+Sub, 2-OP FM та Chord Pad.
* **Алгоритмічна реверберація Freeverb Schroeder-Moorer (src/audio_engine.hpp/.cpp):**
  * Замінено примітивну 300мс лінію затримки на повноцінний алгоритмічний ревербератор Freeverb:
    * 8 паралельних гребінчастих фільтрів зворотного зв'язку (Lowpass Feedback Comb Filters) на канал із простими числами затримок (1116..1617 семплів) та стерео-розведенням (+23 семпли).
    * 4 послідовних всепропускних фільтри (Allpass diffusers) на канал (225..556 семплів) для створення щільного просторового дифузного хвоста без дискретного "луна-ехо" ефекту та флаттеру.
    * Демпфування високих частот (1-pole lowpass у зворотній петлі) для реалістичного згасання в кімнаті.
  * Споживання CPU на ARM Cortex-A55 залишилося нижче 0.5%.
* **Подвійні LFO та перемикання режиму TRIG vs FREE (Elektron style):**
  * Додано `enum class LfoMode : uint8_t { kTrig = 0, kFree = 1 };`.
  * Кожна доріжка має два незалежних низькочастотних генератори: `lfo1` та `lfo2`.
  * Режим `TRIG`: фаза LFO скидається в 0.0 при кожному тригері ноти з секвенсора або клавіатури.
  * Режим `FREE`: фаза LFO коливається безперервно незалежно від секвенсора, плавно розвиваючи текстуру треку.
  * У `src/ui/layout.cpp` розгорнуто окремі рядки для `LFO1` та `LFO2` із параметрами `{WAVE, RATE, DEPTH, DEST, MODE}`.
* **Нульові сенди за замовчуванням (Default Zero Sends):**
  * Для всіх нових треків та стартового патерна параметри `delay_send` та `reverb_send` ініціалізуються рівно в `0.0f`.
* **Розподіл параметрів та ліквідація дублювань у меню SYNTH (src/ui/layout.cpp, src/ui/params.cpp):**
  * Параметр `TRK.WAVE_SAMPLE` ізольовано виключно для семплерних движків (`kSampleOneShot`, `kSliceSampler`).
  * Для синтезаторів перший рядок `INST` переналаштовано:
    * `DX7`: `{"TRK.ENGINE", "TRK.P1" (PRESET), "TRK.RND"}` — усунено синхронну зміну `PATCH` та `PRESET`.
    * `VA SAW+SUB`: `{"TRK.ENGINE", "TRK.GLIDE", "TRK.RND"}` — `PULSE W` тепер керується виключно через `TRK.P6`.
    * `2-OP FM`: `{"TRK.ENGINE", "TRK.GLIDE", "TRK.RND"}` — `MOD DEC` тепер керується виключно через `TRK.P6`.
    * `ACID 303`: `{"TRK.ENGINE", "TRK.P6" (WAVEFRM), "TRK.RND"}` — перемикання пилки/прямокутника.
* **Автоматизоване тестування:**
  * Тестовий стенд `auto_test` розширено з 97 до **108 строгих перевірок**: протестовано 4-голосну поліфонію (3-нотні акорди на DX7 та VA), дефолтні нульові сенди, LFO TRIG/FREE режими, дифузний хвіст Freeverb та делінкацію параметрів `TRK.WAVE_SAMPLE`.
  * Результат тестування: **108/108 PASSED [OK]**.

## [2026-10-28] Milestone 21: Autonomous USB CDC-NCM Device & SSH Persistence Watchdog for RK3326S Appliance
* **Автономний системний сервіс утримання USB Device (CDC-NCM / SSH 10.1.1.2) (scripts/deploy_xu10.sh):**
  * Створено та увімкнено виділений системний сервіс `usb-gadget-device.service` (`/storage/.config/system.d/usb-gadget-device.service`), який стартує на рівні `rocknix.target` та `multi-user.target` незалежно від стану головного додатку DAW.
  * Реалізовано фоновий скрипт сторожового контролю `/storage/dev/groovebox/usb_watchdog.sh`:
    * Перевіряє прив'язку до UDC (`/sys/kernel/config/usb_gadget/cdc/UDC`) і запускає гаджет тільки у випадку відсутності зв'язку, ніколи не скидаючи шину при активному з'єднанні.
    * Призначає IP `10.1.1.2/24` та піднімає інтерфейс `gadget` безпосередньо в мережевому стеку Linux без деструктивного перезапуску USB PHY.
    * Гарантує автоматичний запуск та утримання `sshd.service`.
* **Автоматичне повернення з режиму хоста (Host-to-Device Auto-Revert):**
  * У разі перемикання користувачем у режим `HOST (MIDI / CC)` через меню `SYSTEM`, watchdog моніторить стан фізичного підключення зовнішнього USB-пристрою на кореневому хабі (`/sys/bus/usb/devices/usb1/1-0:1.0/usb1-port1/state`).
  * Якщо протягом 12 секунд жоден USB MIDI контролер або хаб не підключений (або після його від'єднання), watchdog автоматично повертає роль порту в `device`, відновлюючи CDC-NCM та SSH доступ за адресою `10.1.1.2`.
* **Захист на рівні додатку та профілю (src/hw_monitor.cpp, profiles/magicx-xu10.ini):**
  * Метод `HardwareMonitor::start()` примусово ініціалізує USB роль у `device_ssh` при старті додатку.
  * До профілю `magicx-xu10.ini` додано явний шлях до конфігурації гаджета: `gadget_conf = /storage/.cache/usbgadget/usbgadget.conf`.
* **Надійний пайплайн деплою без втрати екранування змінних (scripts/deploy_xu10.sh):**
  * Переведено передачу віддалених скриптів конфігурації на heredoc `ssh ... 'bash -s' << 'REMOTE_SCRIPT'`, що повністю виключає локальне розкриття змінних `$ROLE`, `$UDC_BOUND`, `$IP` та пошкодження лапок у віддаленому скрипті.
* **Верифікація на фізичному обладнанні (Hardware Verification):**
  * На MagicX XU10 перевірено одночасну стабільну роботу двох процесів: `xu10_groovebox` (PID 4620, CPU 18.4%, RAM 76MB) та `usb_watchdog.sh` (PID 4801, CPU 0.3%, RAM 2.8MB).
  * RTT пінгу: ~0.86 мс. Зв'язок SSH `root@10.1.1.2` підтверджено як повністю відновлюваний та постійно активний.

## [2026-10-28] Milestone 22: Dual Filters (HPF+LPF), Digitakt LFOs, Polyphonic Piano Roll & Discrete Step Gate
* **Ліквідація витоку кнопок у Sample Browser (src/ui/actions.cpp, src/ui/keymap.cpp, src/ui/ui_context.hpp):**
  * Додано `modal_cooldown_until_ms` (300мс) для блокування залишкових подій кнопок A та B після підтвердження або скасування вибору семплу.
  * Кнопки позначаються як `consumed_` при закритті модалки, усуваючи повторне відкриття вікна браузера через активацію параметра `TRK.WAVE_SAMPLE` підкладки `SYNTH`.
* **Архітектура подвійних фільтрів (HPF + LPF) (src/synth_engines.hpp, src/audio_engine.hpp/.cpp, src/ui/params.cpp, src/ui/layout.cpp):**
  * У DSP-рушій перед 2-полюсним резонансним фільтром низьких частот (SVF Lowpass) додано 1-полюсний фільтр високих частот (Low-Cut High-Pass Filter):
    $$y[n] = x[n] - \text{low}[n], \quad \alpha = 1 - e^{-2\pi f_c / f_s}$$
  * Діапазон зрізу HPF: від 20 Гц до 16 кГц ($20.0 \times 800.0^{hpf\_cutoff}$), при $hpf\_cutoff \le 0.005$ фільтр стає повністю прозорим (`OFF`).
  * Параметри `TRK.HPF` та `T1..T16.HPF` інтегровано в рядок `SEND` синтезатора та поканальні смуги мікшера `MIX`.
* **Digitakt-Style LFO: пропорційна шкала 1..128, Hz та дільники/множники (x1..x64):**
  * LFO1 та LFO2 відображають швидкість у пропорційній шкалі Digitakt: `1..128 (X.X Hz)` або `(XX Hz)`.
  * Додано селектор множника частоти `L1 MULT` та `L2 MULT` із дискретними ступенями: `x1, x2, x4, x8, x16, x32, x64`.
* **Рефакторинг секвенсора та відкриття Step Editor по кнопці X:**
  * З вікна секвенсора прибрано кнопки зміни ноти на X та октави на Y.
  * Кнопку `X` призначено на відкриття та перемикання Step Editor (`INSPECT.OPEN` / `INSPECT.CLOSE`).
  * Кнопка `A` переведена на миттєвий тригер кроку (`Gesture::kPress`), усуваючи затримку очікування утримання.
  * Зі Step Editor прибрано рядки обвідної `ATK`, `DEC`, `SUS`, `REL`.
* **Дискретна довжина Gate Length у кроках (1..128) та візуальний хвіст тривалості:**
  * Параметр `gate_len` переведено з відсотків у дискретні кроки секвенсора (від 1 до 128 кроків).
  * У сітці секвенсора реалізовано делікатне підсвічування клітинок, охоплених тривалістю ноти (`ColorRGB{18, 52, 72}` та смуга `ColorRGB{45, 115, 150}`), без блокування навігації.
* **Багатооктавний Piano Roll з поліфонією 4 нот та алертом вичерпання голосів:**
  * Підтримка безшовного вертикального скролу по всіх октавах від C1 до C8.
  * Piano Roll автоматично розкривається на поточному активному треку.
  * Кроки секвенсора розпаковують і редагують до 4 голосів акорду (`chord_notes[4]`).
  * При спробі призначити 5-й голос на один крок виводиться червоний банер сповіщення `"VOICE LIMIT: MAX 4 VOICES / STEP"` та статус у HUD.
* **Автоматизоване тестування та деплой на консоль:**
  * Розширено тестовий набір `tools/auto_test.cpp` до **114 строгих тестів**: 100% проходження (**114/114 PASSED [OK]**).
  * Оновлено графічні артефакти туру інтерфейсу (17 скріншотів 320×240 PNG).
  * Зібрано ARM64 appliance та успішно задеплоєно на MagicX XU10 (`10.1.1.2`, PID 48399).

## [2026-10-28] Milestone 23: Hardware Key Debounce & 850ms Repeat, Dedicated Dual SVF Filter Rows (LPF + HPF) with Bipolar Decay Envelopes
* **Ліквідація подвійного спрацьовування клавіш (Double-Press Elimination) (src/ui/keymap.cpp):**
  * Виявлено кореневу причину "подвійного натискання": у `Keymap::lookup` при обробці відпускання клавіші (`ev.released_edge` $\rightarrow$ `Gesture::kTap`) був присутній фолбек на `Gesture::kPress`, що призводило до повторного виконання дії при кожному відпусканні кнопки!
  * Фолбек видалено — `lookup` строго зіставляє дію за запитаним жестом.
  * Налаштовано чіткий стакато-клік з апаратним автоповтором за стандартами апаратних грувбоксів (Elektron / MPC): затримка до початку повтору 850 мс, циклічний інтервал повтору 120 мс (не прив'язаний до частоти кадрів процесора).
* **Виділені рядки LPF та HPF у розділі SYNTH (src/ui/layout.cpp, src/ui/params.cpp):**
  * Замінено старий одинарний параметр фільтра на два незалежні повноформатні рядки у розділі `SYNTH` для всіх рушіїв:
    * Рядок **LPF**: `LP CUT`, `LP RES`, `LP ENV`, `LP DEC`.
    * Рядок **HPF**: `HP CUT`, `HP RES`, `HP ENV`, `HP DEC`.
  * Очищено рядок `SEND` до чистих посилів та рівня: `DLY SND`, `REV SND`, `PAN`, `LEVEL`.
* **Повноцінні 2-полюсні SVF-фільтри та біполярні експоненційні огинаючі згасання (src/synth_engines.hpp, src/audio_engine.hpp/.cpp):**
  * Обидва фільтри (LPF та HPF) реалізовано як 2-полюсні State-Variable Filters (SVF) з незалежним резонансом та експоненційними огинаючими згасання.
  * Огинаючі фільтрів `LP ENV` та `HP ENV` за замовчуванням рівні `0%` і мають біполярний діапазон Elektron (-100% .. +100%):
    * Додатні значення (`+`): відкривають фільтр при тригері ноти та плавно згасають до базового зрізу.
    * Від'ємні значення (`-`): інвертована огинаюча, закриває фільтр при тригері ноти з наступним поверненням до базового зрізу.
    * Параметри `LP DEC` та `HP DEC` керують часом згасання огинаючої фільтра від 10 мс до 2500 мс.
* **Автоматизоване тестування та деплой на MagicX XU10:**
  * Додано тести затухання енергії сигналу для LPF та HPF, перевірку поведінки біполярних огинаючих та параметричного реєстру.
  * Увімкнено крос-компіляцію та виконання повного набору `auto_test` безпосередньо на цільовому процесорі Rockchip RK3326S ARM64 під ROCKNIX.
  * Загальна кількість автоматичних тестів: **125 (125/125 PASSED [OK])** як локально на macOS, так і на фізичній консолі MagicX XU10.
  * Оновлено графічні артефакти туру інтерфейсу (включаючи `tour_02_synth.png`).
  * Зібрано та задеплоєно на MagicX XU10 (`root@10.1.1.2`, PID 55519, KMSDRM, ALSA 32k).

## [2026-10-28] Milestone 24: Sequencer Remap, 256-Step Polymetry, Global Digitakt Temp Snapshots, DX7 Live Algo/FB, Song Mode, MIX Send Meters & Project System
* **Перепризначення шорткатів секвенсора та операцій з треками (src/ui/keymap.cpp, src/ui/actions.cpp):**
  * `SELECT + B`: очищення послідовності на поточному треку (`SEQ.CLEAR_TRACK`, стирає кроки без видалення треку).
  * `SELECT + L1`: безпечне видалення треку (`SEQ.DEL_TRACK_SAFE`).
  * Ліквідовано дублювання `SELECT + Left/Right` та `SELECT + X/Y`:
    * `SELECT + Left/Right`: регулювання довжини треку.
    * `SELECT + X`: копіювання виділеного кроку (`STEP.COPY`).
    * `SELECT + Y`: вставка скопійованого кроку у виділену комірку (`STEP.PASTE`).
  * Видалено перехоплення `L2` у секвенсорі (`Layer::kHoldL2`), натискання `L2` миттєво перемикає на екран `SYNTH`.
* **Розширення довжини треку до 256 кроків та циклічний луп (src/synth_engines.hpp, src/ui/actions.cpp):**
  * Максимальну довжину треків збільшено до 256 кроків (`kMaxSteps = 256`).
  * Реалізовано безшовний циклічний луп при регулюванні довжини: зменшення нижче 4 переходить на 256, а збільшення понад 256 повертає на 4 (`4 <-> 256`).
* **Глобальні моментальні знімки Digitakt Temp Save & Reload (src/ui/ui_context.hpp, src/ui/actions.cpp, src/ui/keymap.cpp):**
  * `SELECT + G`: миттєве швидке збереження (`PROJECT.TEMP_SAVE`) активного патерну та параметрів усіх треків у пам'ять (аналог Digitakt `[FUNC] + [YES]`). Працює глобально в усіх екранах.
  * `SELECT + START`: миттєве відновлення (`PROJECT.TEMP_RELOAD`) останнього збереженого знімка (аналог Digitakt `[FUNC] + [NO]`).
* **Виправлення перемикання патернів та система збереження проектів (src/pattern_manager.hpp/.cpp, src/ui/actions.cpp, src/ui/ui_shell.cpp):**
  * Реалізовано метод `switchPatternNow(bank, pattern, audio, bpm)`: перед перемиканням зберігає поточну роботу в пам'ять, а новий патерн коректно ініціалізує звуками без витирання налаштувань.
  * Додано автостворення директорій `std::filesystem::create_directories("/storage/dev/groovebox/projects")` — усунуто помилку відмови запису проектів.
  * Додано модальне вікно списку проектів для `Save As` та `Load Project` з навігацією та підтвердженням по кнопці A.
* **Рушій DX7: оверрайд алгоритму, зворотного зв'язку та розширення Detune (src/dx7_engine.hpp/.cpp, src/audio_engine.cpp, src/ui/layout.cpp):**
  * Усунуто дефект статичності параметрів DX7: додано `override_algo` (1..32) та `override_feedback` (0..7) у реальному часі безпосередньо в обчислення войсу `Dx7Voice::process`.
  * Зайвий дубльований cutoff у розділі DX7 замінено на повноцінний параметр розладу унісону операторів `DETUNE` (`tp.p[1]`).
* **Параметр TUNE семплера у першому рядку INST (src/ui/layout.cpp, src/ui/params.cpp):**
  * До першого рядка інструментів семплера (`kSampleOneShot` та `kSliceSampler`) поруч із вибором файлу додано параметр висоти тону `TUNE` (`-24 ST .. +24 ST`, дефолт `+0 ST`).
* **3-режимний цикл кнопки L1 та Song Mode (src/ui/actions.cpp, src/ui/views.cpp, src/main.cpp):**
  * Кнопка `L1` здійснює 3-етапний послідовний цикл: `Matrix Sequencer` $\rightarrow$ `Piano Roll` $\rightarrow$ `Song Mode` $\rightarrow$ `Matrix Sequencer`.
  * У `Song Mode` реалізовано 16-кроковий аранжувальник послідовності патернів (A01..H16) з налаштуванням повторів (`x1..x16`) та статусом увімкнення (`PLAY`/`SKIP`).
  * Навігація в Song Mode автоматично завантажує патерн для живого прев'ю, а при переході в Sequencer патерн одразу відкривається для редагування.
  * Таймінг транспорту під час відтворення послідовно проходить ланцюжок Song Mode згідно з лічильником повторів.
* **MIX View: 3 вертикальні VU-метри посилів (DLY, REV, CHR) (src/audio_engine.hpp/.cpp, src/ui/views.cpp):**
  * В аудіорушії додано відстеження рівнів сенд-шин: `send_delay`, `send_reverb`, `send_chorus`.
  * У нижній панелі екрана `MIX` безпосередньо ліворуч від Master VU розміщено три вертикальні рівнеміри: `D` (Delay, ціан), `R` (Reverb, фіолетовий), `C` (Chorus, зелений).
* **Повне автоматизоване тестування та деплой на консоль:**
  * Тестовий набір `auto_test` розширено до **126 тестів**: 100% проходження (**126/126 PASSED [OK]**) як на macOS, так і на фізичному процесорі Rockchip RK3326S ARM64 під ROCKNIX.
  * Оновлено всі 17 PNG-артефактів туру інтерфейсу.
  * Зібрано та задеплоєно на консоль MagicX XU10 (`10.1.1.2`, PID 98345, Dedicated Appliance, KMSDRM, ALSA 32k).

## [2026-10-28] Milestone 25: 256-Step Engine Unclamp, Multi-Buffer Hard Stress Benchmark on RK3326S & Open-Source Groovebox Architectural Audit
* **Зняття 128-крокового обмеження в AudioEngine та перевірка лупу (src/audio_engine.cpp, src/audio_engine.hpp, tools/auto_test.cpp):**
  * Виявлено кореневу причину зупинки на 128 кроках: методи `AudioEngine::setRowLength`, `cycleRowLength` та `setPatternLength` містили жорсткий `std::clamp(len, 1u, 128u)`.
  * Обмеження замінено на `kMaxSteps` (`256u`).
  * До тестового набору `tools/auto_test.cpp` додано перевірку встановлення 256 кроків, циклічного переходу `4 -> 256` по `SEQ.LEN_DEC` та `256 -> 4` по `SEQ.LEN_INC`.
  * Загальна кількість тестів зросла до **129 (129/129 PASSED [OK])** як на macOS, так і на MagicX XU10.
* **Результати стрес-тесту 64 активних голосів + 100% FX на процесорі Rockchip RK3326S ARM64:**
  * Тестовий стенд: 16 треків $\times$ 4 поліфонічні голоси = **64 одночасно синтезовані голоси** (PolyBLEP Saw+Sub, 2-Op FM, Acid 303, Chord Pad, Samplers, Yamaha DX7 6-OP), Stereo Delay (Ping-Pong), Diffusion Reverb (100% decay), Chorus та подвійні 2-полюсні SVF-фільтри (LPF+HPF) на всіх 16 каналах з активною модуляцією.
  * Фактичні виміри на залізі MagicX XU10 (15 комбінацій частот та буферів):

| Sample Rate | Buffer Size | Block Budget |  Avg Render  |  Max Render  | DSP Load | Peak Load | Headroom | Memory RSS |
|:-----------:|:-----------:|:------------:|:------------:|:------------:|:--------:|:---------:|:--------:|:----------:|
|  **32000 Hz**   |    96 smp   |    3.00 ms   |    5.62 ms   |    6.24 ms   |  187.2%  |  208.1%   |    0.0%  |   13.0 MB  |
|  **32000 Hz**   |   128 smp   |    4.00 ms   |    5.92 ms   |    7.93 ms   |  147.9%  |  198.4%   |    0.0%  |   13.4 MB  |
|  **32000 Hz**   |   256 smp   |    8.00 ms   |    6.76 ms   |   15.34 ms   |   84.5%  |  191.8%   |   15.5%  |   13.4 MB  |
|  **32000 Hz**   |   **512 smp**   |   **16.00 ms**   |    **8.08 ms**   |   28.53 ms   |   **50.5%**  |  178.3%   |   **49.5%**  |   **13.4 MB**  |
|  **32000 Hz**   |  **1024 smp**   |   **32.00 ms**   |   **10.89 ms**   |   57.91 ms   |   **34.0%**  |  181.0%   |   **66.0%**  |   **13.4 MB**  |
|  **44100 Hz**   |   512 smp   |   11.61 ms   |   10.03 ms   |   29.40 ms   |   86.4%  |  253.2%   |   13.6%  |   13.4 MB  |
|  **44100 Hz**   |  1024 smp   |   23.22 ms   |   12.82 ms   |   57.02 ms   |   55.2%  |  245.6%   |   44.8%  |   13.4 MB  |
|  **48000 Hz**   |   512 smp   |   10.67 ms   |   10.63 ms   |   29.08 ms   |   99.6%  |  272.6%   |    0.4%  |   13.4 MB  |
|  **48000 Hz**   |  **1024 smp**   |   **21.33 ms**   |   **10.47 ms**   |   49.02 ms   |   **49.1%**  |  229.8%   |   **50.9%**  |   **13.4 MB**  |

* **Ключові інженерні висновки з вимірів:**
  * **Пам'ять:** споживання процесу з 64 войсами становить лише **13.4 МБ** із 1024 МБ RAM (**1.3%**), вільний запас пам'яті — понад **98.7%**.
  * **Рекомендований робочий режим:** при дефолтній частоті внутрішнього кодека RK817 **32000 Гц з буфером 512 семплів (16 мс)** навантаження на процесор під максимальним стресом становить **50.5%**, залишаючи **49.5% чистого запасу потужності**! При буфері 1024 семпли запас складає **66.0%**.
  * **Вузьке місце пікового навантаження:** розрахунок трансцендентних функцій (`std::sin`, `std::pow`) у SVF-фільтрах на кожному окремому семплі створює пікові сплески. Винесення перерахунку коефіцієнтів фільтрів на блоковий рівень (раз на 16..64 семпли) додатково знизить пікове навантаження на 40-50%.
* **Архітектурний аудит відкритих систем (dirtywave M8, Mutable Instruments Braids, Elektron):**
  * Досліджено паттерни Dirtywave M8 (Teensy Audio float pipeline, SLIP protocol, 8 monophonic/polyphonic tracks) та Mutable Instruments Braids (MacroOscillator з монолітними функціями ядра та швидкими lookup-таблицями `resources.py`).
  * Для максимальної швидкодії компіляції та виконання рекомендовано:
    1. Винести логіку секвенсора з `audio_engine.cpp` в окремий модуль `src/sequencer.hpp/.cpp`.
    2. Синтезаторні ядра (VA, FM2, Acid, Chord) винести у легкі заголовкові структури `src/dsp/` без використання віртуальних функцій (Virtual Method Tables створюють пенальті на ARM Cortex-A35 через скидання кешу гілок конвеєра).

## [2026-10-28] Milestone 26: First-Class Modal View Architecture, Scoped Routing, Dynamic HUD Button Hints & Master Volume Display
* **Уніфікація архітектури модальних вікон (src/ui/ui_context.hpp):**
  * Модальні вікна виведено в статус першокласних екранів перегляду (`ViewId::kProjectModal`, `ViewId::kSampleBrowser`, `ViewId::kEngineMenu`).
  * Реалізовано чисті допоміжні функції:
    * `activeViewId(const AppModel&)`: повертає активний модальний екран (якщо відкритий) або базову вкладку робочого простору.
    * `isModalView(ViewId)`: визначає, чи є екран модальним (для блокування автоповтору значень A/B та фокусування).
    * `closeAllModals(AppModel&)`: гарантовано закриває всі модальні вікна без залишкових станів.
* **Суворі рамки відкриття модальних вікон (src/ui/actions.cpp):**
  * Арпеджиатор (`ARP.OPEN`, `ARP.TOGGLE`) дозволено відкривати виключно з вкладки секвенсора (`kSeq`). При спробі виклику з інших вкладок статус показує `ARP: SEQ ONLY`.
  * Меню збереження/завантаження проектів (`PROJECT.SAVE_AS_OPEN`, `PROJECT.LOAD_OPEN`) дозволено відкривати виключно з вкладки системи (`kSys`).
  * Селектор звукового рушія (`ENGINE.MENU_OPEN`) дозволено викликати лише з екранів `kSeq` та `kSynth`.
  * Автозакриття при перемиканні вкладок: будь-яке перемикання табів (`TAB.L1`, `TAB.L2`, `TAB.R1`, `TAB.R2`, `TAB.G`, `TAB.SEL_*`) викликає `closeAllModals(c.app)`, усуваючи зависання вікон при переході.
* **Модульний кеймап для кожного вікна (src/ui/keymap.cpp, src/ui/actions.cpp):**
  * Для кожного модального вікна зареєстровано індивідуальні прив'язки кнопок:
    * `ViewId::kProjectModal`: D-Pad Up/Down (`PROJECT.MODAL_UP`/`DOWN`), Button A (`PROJECT.MODAL_CONFIRM`), Button B / SELECT+B (`PROJECT.MODAL_CANCEL`).
    * `ViewId::kSampleBrowser`: D-Pad Up/Down/Left/Right, Button A (`SAMPLE_BROWSER.CONFIRM`), Button B / SELECT+B (`SAMPLE_BROWSER.CLOSE`).
    * `ViewId::kEngineMenu`: D-Pad Up/Down (`ENGINE.MENU_UP`/`DOWN`), Button A (`ENGINE.MENU_CONFIRM`), Button B / SELECT+B (`ENGINE.MENU_CLOSE`).
  * В `InputRouter::processInput()` прибрано жорсткі блоки перехоплення: маршрутизація здійснюється уніфіковано через `activeViewId(ctx.app)`.
  * Додано захисний таймер `modal_cooldown_until_ms = c.now_ms + 300` при підтвердженні та скасуванні модалок для запобігання витоку натискань кнопок A/B у фоновий екран.
* **Оновлення нижнього оверлея (HUD) та ліквідація конфліктів підказок (src/ui/ui_shell.cpp, src/audio_engine.hpp):**
  * Поряд із показником BPM у першому рядку виведено поточну гучність у відсотках (наприклад, `120 BPM  80%`), що зчитується атомарно через `AudioEngine::getMasterVolume()`.
  * У другому рядку виводиться точний заголовок модального вікна (`PROJECT: SAVE AS`, `PROJECT: LOAD`, `SAMPLE BROWSER`, `SELECT SYNTH ENGINE`).
  * У шостому блоці HUD підказки кнопок ABXY формуються динамічно через `keymap.actionLabel(active_v_id, ...)`: для відкритого модального вікна HUD показує `A:CONF B:CANC  X:--  Y:--` без жодного конфлікту.
  * Видалено дублюючі текстові підказки кнопок всередині самих діалогових вікон проектів та семплів — нижній оверлей консолі є єдиним достовірним джерелом підказок дій.
* **Автоматизоване тестування та деплой на консоль:**
  * Додано нові тести перевірки суворих меж відкриття вікон, активного ViewId, динамічних підказок HUD, скасування по кнопці B та автозакриття при перемиканні вкладок.
  * Тестовий набір розширено до **144 тестів**: 100% проходження (**144/144 PASSED [OK]**) як на робочій станції, так і на фізичному пристрої MagicX XU10.
  * Додаток зібрано та перезапущено на залізі MagicX XU10 (`10.1.1.2`, PID 112897).

## [2026-10-28] Milestone 27: Model:Cycles Style GAIN/OVER Drive Curve, Dynamic 3-State Tab Cycles & Color Branding
* **Крива GAIN / OVER за зразком Elektron Model:Cycles/Samples (src/synth_engines.hpp, src/audio_engine.cpp, src/ui/params.cpp):**
  * Додано параметр `gain_over` (-1.0 .. +1.0, що відповідає -24 dB .. +24 dB, дефолт 0.0 = 0 dB clean normalized max) до кожного треку.
  * Реалізовано дводіапазонну передавальну функцію:
    * При $g \le 0.0$: чисте плавне згасання амплітуди без жодних спотворень (0 dB .. -24 dB / тиша).
    * При $g > 0.0$: розгін у теплий синусоїдальний/гіперболічний овердрайв $\tanh(x \cdot drive)$ з компенсацією амплітуди для щільного гітарного кранчу.
  * Відкалібровано та нормалізовано рівні мікшування ядер синтезу (VA Mono, Chord Pad, Acid 303, FM 2-Op, DX7, Sampler) для гарантованого відтворення чистого сигналу без кліпу на дефолтних налаштуваннях.
  * Параметр `TRK.GAIN_OVER` додано у перший рядок `INST` усіх синтезаторних рушіїв у [layout.cpp](file:///Users/oleksii/Documents/XU10%20Groovebox/src/ui/layout.cpp) з динамічним відображенням `0.0 dB [CLN]` / `+X.X dB [CRUNCH]`.
* **3-рівнева циклічна навігація вкладок та первинне скидання (src/ui/ui_context.hpp, src/ui/actions.cpp):**
  * Створено `SynthSubmode` (`kSynth`, `kMod`, `kFx`) та `MixSubmode` (`kMix`, `kSend`, `kMaster`).
  * Реалізовано правило першого натискання бамперів:
    * `TAB.L1`: перше натискання завжди перемикає на головний екран матричного секвенсора `SEQ` (`kMatrix`). Наступні послідовні натискання циклічно змінюють `SEQ` $\rightarrow$ `ROL` (Piano Roll) $\rightarrow$ `SON` (Song Mode) $\rightarrow$ `SEQ`.
    * `TAB.L2`: перше натискання відкриває головний синтезатор `SYN` (`kSynth`). Наступні — перемикають `SYN` $\rightarrow$ `MOD` $\rightarrow$ `FX` $\rightarrow$ `SYN`.
    * `TAB.R2`: перше натискання відкриває головний мікшер `MIX` (`kMix`). Наступні — перемикають `MIX` $\rightarrow$ `SND` $\rightarrow$ `MSTR` $\rightarrow$ `MIX`.
* **Динамічні заголовки підвкладок та кольорове брендування (src/ui/ui_shell.cpp, src/ui/layout.cpp):**
  * Верхній рядок табів формує заголовки згідно зі специфікацією:
    * `[L1]son<SEQ>rol` (помаранчевий `kAccentOrange`), `[L1]seq<ROL>son` (жовтий `kParamTrig`), `[L1]rol<SON>seq` (червоний `kLevelWarnClip`).
    * `[L2]fx<SYN>mod` (ціан `kAccentCyan`), `[L2]syn<MOD>fx` (фіолетовий `kAccentPurple`), `[L2]mod<FX>syn` (зелений `kLevelOk`).
    * `[R2]mstr<MIX>snd` (ціан `kAccentCyan`), `[R2]mix<SND>mstr` (бузковий `kAccentPurple`), `[R2]snd<MSTR>mix` (золотистий `kAccentOrange`).
  * Ширину слотів оптимізовано для гармонійного вигляду та розміщення тексту: 72px (L1), 72px (L2), 72px (R2), 72px (R1: SAMPLE), та звужену 32px під `[G]SYS`.
* **Верифікація та тестування на залізі:**
  * Додано юніт-тести для перевірки дефолтного `GAIN/OVER = 0`, меж регулювання, першого натискання та циклів `L1`, `L2`, `R2`.
  * Тестовий набір розширено до **157 тестів** (**157/157 PASSED [OK]** на macOS та на консолі Rockchip RK3326S ARM64).
  * Додаток оновлено та запущено на MagicX XU10 (`10.1.1.2`, PID 154977).

## [2026-10-28] Milestone 28: Elektron-Style Song Mode Architecture with Per-Row Track Mutes, BPM & Length Overrides
* **Структура рядка Elektron Song Mode (src/ui/ui_context.hpp):**
  * Розширено структуру `SongStep`: додано оверрайд довжини `length` (0 = PAT, 1..256 кроків), оверрайд темпу `bpm` (0 = PAT, 30..300 BPM) та 16-бітну бітову маску м'юту треків `mute_mask` (біт 0 = Track 1, біт 15 = Track 16).
  * Додано навігаційний фокус колонки `song_col` (PAT, REP, LEN, BPM, MUTES) та курсор виділеного треку `song_mute_cursor_track`.
* **Табличний інтерфейс Song Mode 16 рядків (src/ui/views.cpp):**
  * Повністю перероблено екран Song Mode: заголовок у фірмовому червоному кольорі `kLevelWarnClip`, рядок підзаголовків колонок `ROW | PAT | REP | LEN | BPM | TRACK MUTES (1..16) | STATUS`.
  * У колонці `MUTES` для кожного з 16 треків відображається його стан (зелений індекс активного треку або червона крапка `.` заглушеного), з прямокутним маркером поточного виділеного треку.
  * Додано індикатор поточного прогресу відтворення `>` у зеленому кольорі навпроти активного рядка пісні.
* **Керування та гарячі клавіші (src/ui/actions.cpp):**
  * `D-Pad Up/Down`: вибір активного рядка сонгмоду (01..16).
  * `D-Pad Left/Right`: переміщення між колонками або вибір треку в колонці `MUTES`.
  * `SELECT + Left/Right`: швидке перемикання колонок (`NAV.BAR_LEFT` / `NAV.BAR_RIGHT`).
  * `Кнопка A`: перемикання ON/OFF (PLAY/SKIP) для кроку або тогл м'юту виділеного треку в рядку.
  * `Кнопки X / B`: інкремент/декремент параметрів виділеної колонки (PAT +/-, REPEATS +/-, LEN +/-, BPM +/-).
* **Інтеграція з відтворенням у реальному часі (src/main.cpp):**
  * Покроковий секвенсор транспорту враховує оверрайд довжини рядка `length` (якщо задано, інакше дефолт патерну).
  * При переході між рядками сонгмоду автоматично застосовується `mute_mask` до всіх 16 треків аудіорушія та встановлюється локальний BPM рядка.
* **Автоматизоване тестування та деплой на консоль:**
  * Тестовий набір [auto_test.cpp](file:///Users/oleksii/Documents/XU10%20Groovebox/tools/auto_test.cpp) розширено до **162 тестів** (**162/162 PASSED [OK]** на macOS та на консолі Rockchip RK3326S ARM64).
  * Процес `xu10_groovebox` перезапущено як Dedicated Appliance на MagicX XU10 (`10.1.1.2`, PID 162207).
