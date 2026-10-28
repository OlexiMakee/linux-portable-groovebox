#include "ui/widgets.hpp"

#include <algorithm>

namespace groovebox::widgets {

void paramCell(Canvas& cv, Rect r, const std::string& label, const std::string& value,
               float norm, bool selected, bool editable, ColorRGB accent,
               float live_norm, bool is_mod_target) {
    cv.fill(r.x + 1, r.y + 1, r.w - 2, r.h - 2, selected ? palette::kHeaderSel : palette::kPanelBg);
    cv.rect(r.x, r.y, r.w, r.h, selected ? accent : palette::kGridDim);
    const int max_chars = (r.w - 5) / Canvas::kCharW;
    cv.textClip(r.x + 3, r.y + 3, label, max_chars, selected ? accent : palette::kTextDim);
    const ColorRGB vcol = !editable ? palette::kTextDim : (selected ? palette::kTextMain : palette::kTextMain);
    cv.textClip(r.x + 3, r.y + 10, value, max_chars, vcol);
    if (norm >= 0.0f && r.h >= 19) {
        const int bw = r.w - 6;
        const int fill = static_cast<int>(std::clamp(norm, 0.0f, 1.0f) * bw);
        cv.fill(r.x + 3, r.y + r.h - 4, bw, 2, palette::kGridDim);
        cv.fill(r.x + 3, r.y + r.h - 4, fill, 2, selected ? accent : palette::kLevelOk);

        if (is_mod_target) {
            // Жовта статична відмітка користувача (1 піксель завтовшки)
            int user_tick_x = r.x + 3 + std::clamp(static_cast<int>(norm * bw), 0, bw - 1);
            cv.fill(user_tick_x, r.y + r.h - 5, 1, 4, palette::kParamTrig);

            // Блакитна плаваюча відмітка реального часу (live modulated value)
            if (live_norm >= 0.0f) {
                int live_tick_x = r.x + 3 + std::clamp(static_cast<int>(live_norm * bw), 0, bw - 2);
                cv.fill(live_tick_x, r.y + r.h - 5, 2, 4, palette::kAccentCyan);
            }
        }
    }
}

void listRow(Canvas& cv, Rect r, const std::string& label, const std::string& value,
             bool selected, bool editable, int value_x, ColorRGB accent) {
    if (selected) {
        cv.fill(r.x, r.y, r.w, r.h, palette::kHeaderSel);
        cv.rect(r.x, r.y, r.w, r.h, accent);
    }
    const int ty = r.y + (r.h - 5) / 2;
    cv.text(r.x + 4, ty, label, selected ? accent : palette::kTextDim);
    const int max_chars = (r.w - value_x - 4) / Canvas::kCharW;
    std::string v = (editable && selected) ? ("< " + value + " >") : value;
    cv.textClip(r.x + value_x, ty, v, max_chars,
                editable ? (selected ? palette::kAccentCyan : palette::kTextMain) : palette::kTextDim);
}

void scope(Canvas& cv, Rect r, const std::array<float, 256>& samples, ColorRGB color) {
    cv.fill(r.x, r.y, r.w, r.h, palette::kBgDark);
    cv.rect(r.x, r.y, r.w, r.h, palette::kGridDim);
    const int mid = r.y + r.h / 2;
    cv.line(r.x + 1, mid, r.x + r.w - 2, mid, palette::kGridDim);
    int prev = mid;
    const int amp = r.h / 2 - 2;
    for (int px = 1; px < r.w - 1; ++px) {
        const uint32_t idx = static_cast<uint32_t>(px) * 255u / static_cast<uint32_t>(std::max(1, r.w));
        int y = mid - static_cast<int>(samples[idx] * amp);
        y = std::clamp(y, r.y + 1, r.y + r.h - 2);
        cv.line(r.x + px - 1, prev, r.x + px, y, color);
        prev = y;
    }
}

void meter(Canvas& cv, Rect r, float level, float clip_at) {
    cv.rect(r.x, r.y, r.w, r.h, palette::kGridDim);
    const float lv = std::clamp(level, 0.0f, 1.0f);
    const int fill = static_cast<int>(lv * (r.w - 2));
    cv.fill(r.x + 1, r.y + 1, fill, r.h - 2, lv > clip_at ? palette::kLevelWarnClip : palette::kLevelOk);
}

void verticalMeter(Canvas& cv, Rect r, float level, float clip_at) {
    cv.rect(r.x, r.y, r.w, r.h, palette::kGridDim);
    const float lv = std::clamp(level, 0.0f, 1.0f);
    const int fill_h = static_cast<int>(lv * (r.h - 2));
    if (fill_h > 0) {
        ColorRGB col = (lv > clip_at) ? palette::kLevelWarnClip : palette::kLevelOk;
        cv.fill(r.x + 1, r.y + r.h - 1 - fill_h, r.w - 2, fill_h, col);
    }
}

void iconPlay(Canvas& cv, int x, int y, ColorRGB color) {
    cv.line(x, y, x, y + 4, color);
    cv.line(x + 1, y + 1, x + 1, y + 3, color);
    cv.fill(x + 2, y + 2, 1, 1, color);
}

void iconStop(Canvas& cv, int x, int y, ColorRGB color) {
    cv.fill(x, y, 4, 4, color);
}

void iconWave(Canvas& cv, int x, int y, int wave_type, ColorRGB color) {
    switch (std::abs(wave_type) % 5) {
        case 0: // Saw
            cv.line(x, y + 4, x + 3, y, color);
            cv.line(x + 3, y, x + 3, y + 4, color);
            cv.line(x + 3, y + 4, x + 6, y, color);
            break;
        case 1: // Square
            cv.line(x, y + 4, x, y, color);
            cv.line(x, y, x + 3, y, color);
            cv.line(x + 3, y, x + 3, y + 4, color);
            cv.line(x + 3, y + 4, x + 6, y + 4, color);
            cv.line(x + 6, y + 4, x + 6, y, color);
            break;
        case 2: // Triangle
            cv.line(x, y + 4, x + 2, y, color);
            cv.line(x + 2, y, x + 4, y + 4, color);
            cv.line(x + 4, y + 4, x + 6, y, color);
            break;
        case 3: // Sine
            cv.fill(x, y + 2, 1, 1, color);
            cv.fill(x + 1, y + 1, 1, 1, color);
            cv.fill(x + 2, y, 1, 1, color);
            cv.fill(x + 3, y + 1, 1, 1, color);
            cv.fill(x + 4, y + 3, 1, 1, color);
            cv.fill(x + 5, y + 4, 1, 1, color);
            cv.fill(x + 6, y + 3, 1, 1, color);
            break;
        case 4: // Noise
            cv.fill(x, y, 1, 1, color);
            cv.fill(x + 2, y + 3, 1, 1, color);
            cv.fill(x + 4, y + 1, 1, 1, color);
            cv.fill(x + 5, y + 4, 1, 1, color);
            cv.fill(x + 6, y + 2, 1, 1, color);
            break;
    }
}

void stickBox(Canvas& cv, int x, int y, float fx, float fy, bool clicked, ColorRGB color) {
    cv.rect(x, y, 16, 16, palette::kGridDim);
    cv.line(x + 1, y + 8, x + 14, y + 8, palette::kBgDark);
    cv.line(x + 8, y + 1, x + 8, y + 14, palette::kBgDark);
    const int px = x + 8 + static_cast<int>(fx * 6.0f);
    const int py = y + 8 + static_cast<int>(fy * 6.0f);
    cv.fill(px - 1, py - 1, 3, 3, clicked ? palette::kParamTrig : color);
}

void dpad(Canvas& cv, int cx, int cy, const InputState& in) {
    const auto c = [](bool on) { return on ? palette::kAccentOrange : palette::kGridDim; };
    cv.fill(cx - 2, cy - 7, 4, 4, c(in.dpad_up));
    cv.fill(cx - 2, cy + 3, 4, 4, c(in.dpad_down));
    cv.fill(cx - 7, cy - 2, 4, 4, c(in.dpad_left));
    cv.fill(cx + 3, cy - 2, 4, 4, c(in.dpad_right));
    cv.fill(cx - 2, cy - 2, 4, 4, palette::kHeaderSel);
}

void abxy(Canvas& cv, int cx, int cy, const InputState& in) {
    const auto c = [](bool on) { return on ? palette::kLevelOk : palette::kGridDim; };
    // Верхня - Y, ліва - X, права - B, нижня - A (відповідає фізичній розмітці корпусу XU10)
    cv.fill(cx - 2, cy - 7, 4, 4, c(in.btn_y));
    cv.fill(cx - 7, cy - 2, 4, 4, c(in.btn_x));
    cv.fill(cx + 3, cy - 2, 4, 4, c(in.btn_b));
    cv.fill(cx - 2, cy + 3, 4, 4, c(in.btn_a));
}

} // namespace groovebox::widgets
