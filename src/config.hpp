#pragma once

#include <cstdint>

namespace groovebox {

// Фізичний дисплей MagicX XU10: 640x480 IPS @ 60Hz
inline constexpr int kPhysicalWidth = 640;
inline constexpr int kPhysicalHeight = 480;

// Базовий логічний режим: 160x120 (Integer Scale 4x до 640x480, стиль OLED Elektron)
inline constexpr int kLowResWidth = 160;
inline constexpr int kLowResHeight = 120;

// Розширений логічний режим: 320x240 (Integer Scale 2x до 640x480, стиль Dirtywave M8)
inline constexpr int kHighResWidth = 320;
inline constexpr int kHighResHeight = 240;

// Частота оновлення UI (адаптивна: 0 FPS у спокої, 15 FPS у навігації, 24 FPS для осцилографа/VU)
inline constexpr int kTargetFpsIdle = 2;       // мінімальний пульс телеметрії
inline constexpr int kTargetFpsUi = 15;        // навігація/секвенсор
inline constexpr int kTargetFpsScope = 24;     // активний осцилограф / VU-метр / рух стіків

// Прапорець режиму розробника (дозволяє SELECT + Y для миттєвої зміни 160x120 <-> 320x240)
inline constexpr bool kDefaultDevShortcuts = true;

// 8-кольорова палітра (без альфа-блендингу для нульового навантаження на шину пам'яті)
struct ColorRGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

namespace palette {
    inline constexpr ColorRGB kBgDark        = {10,  12,  16};   // #0A0C10 - OLED чорний фон
    inline constexpr ColorRGB kPanelBg       = {20,  24,  32};   // #141820 - фон блоків верх/низ
    inline constexpr ColorRGB kGridDim       = {44,  54,  72};   // #2C3648 - рамки та неактивні елементи
    inline constexpr ColorRGB kTextDim       = {120, 135, 155};  // #78879B - підписи другого плану
    inline constexpr ColorRGB kTextMain      = {230, 237, 243};  // #E6EDF3 - основний чіткий білий шрифт
    inline constexpr ColorRGB kAccentOrange  = {255, 106, 0};    // #FF6A00 - Elektron активна вкладка/курсор
    inline constexpr ColorRGB kAccentCyan    = {0,   229, 255};  // #00E5FF - хвиля осцилографа / MIDI / стіки
    inline constexpr ColorRGB kLevelOk       = {0,   230, 118};  // #00E676 - зелений (USB OK / VU норма)
    inline constexpr ColorRGB kLevelWarnClip = {255, 42,  85};   // #FF2A55 - червоний (Clip / Host warning)
} // namespace palette

// 4 головні вкладки верхнього бара (відповідають фізичним бамперам зліва направо: L1, L2, R2, R1)
enum class WorkspaceTab : uint8_t {
    kHwSys    = 0, // L1: Апаратний монітор, аудіо-роутинг RK817 <-> Digitakt II, тест вводу
    kSynth    = 1, // L2: Параметри синтезатора (4 доріжки, осцилятори, фільтр)
    kMixFx    = 2, // R2: Мікшер доріжок, панорама, Send FX, загальний VU-метр
    kScopeSeq = 3  // R1: Осцилограф у реальному часі + 16-кроковий секвенсор / MIDI Sync
};

} // namespace groovebox
