#pragma once

#include "input_manager.hpp"
#include "ui/canvas.hpp"

#include <array>
#include <string>

namespace groovebox {

// Перевикористовувані віджети: кожен — незалежна функція малювання, яку можна викликати з будь-якого вікна.
namespace widgets {

// Клітинка параметра: назва, значення, опційна смужка (norm < 0 => без смужки)
// Підтримує живий візуалізатор модуляцій (жовта статична відмітка + плаваюча блакитна відмітка)
void paramCell(Canvas& cv, Rect r, const std::string& label, const std::string& value,
               float norm, bool selected, bool editable, ColorRGB accent,
               float live_norm = -1.0f, bool is_mod_target = false);

// Рядок списку: назва ліворуч, значення праворуч (value_x — зміщення колонки значень)
void listRow(Canvas& cv, Rect r, const std::string& label, const std::string& value,
             bool selected, bool editable, int value_x, ColorRGB accent);

// Осцилограф у довільному прямокутнику
void scope(Canvas& cv, Rect r, const std::array<float, 256>& samples, ColorRGB color);

// Горизонтальний індикатор рівня 0..1 (червоний вище clip_at)
void meter(Canvas& cv, Rect r, float level, float clip_at = 0.85f);

// Вертикальний міні-індикатор рівня 0..1 (знизу догори)
void verticalMeter(Canvas& cv, Rect r, float level, float clip_at = 0.85f);

// Мінімалістичні піксельні піктограми (Elektron-style pixel art)
void iconPlay(Canvas& cv, int x, int y, ColorRGB color);
void iconStop(Canvas& cv, int x, int y, ColorRGB color);
void iconWave(Canvas& cv, int x, int y, int wave_type, ColorRGB color);

// 2D-індикатор аналогового стіка (16x16)
void stickBox(Canvas& cv, int x, int y, float fx, float fy, bool clicked, ColorRGB color);

// Піктограма хрестовини
void dpad(Canvas& cv, int cx, int cy, const InputState& in);

// Ромб ABXY (X — північ, Y — захід, A — схід, B — південь)
void abxy(Canvas& cv, int cx, int cy, const InputState& in);

} // namespace widgets

} // namespace groovebox
