#include "ui/canvas.hpp"

#include <SDL.h>

namespace groovebox {

namespace {

// Компактний 3x5 піксельний растровий шрифт (кожен символ — 15 бітів, 5 рядків по 3 біти)
// Займає 4x6 пікселів з інтервалом -> 40 символів у рядку на 160x120!
constexpr uint16_t kFont3x5[96] = {
    0x0000, // ' '
    0x2492, // '!'
    0x002D, // '"'
    0x5F7D, // '#'
    0x279E, // '$'
    0x52A5, // '%'
    0x6CA9, // '&'
    0x0012, // '''
    0x4494, // '('
    0x1491, // ')'
    0x0550, // '*'
    0x05D0, // '+'
    0x1400, // ','
    0x01C0, // '-'
    0x0800, // '.'
    0x12A4, // '/'
    0x7B6F, // '0'
    0x749A, // '1'
    0x73E7, // '2'
    0x79E7, // '3'
    0x49ED, // '4'
    0x79CF, // '5'
    0x7BCF, // '6'
    0x4927, // '7'
    0x7BEF, // '8'
    0x79EF, // '9'
    0x0410, // ':'
    0x1410, // ';'
    0x4454, // '<'
    0x1C70, // '='
    0x1511, // '>'
    0x21A7, // '?'
    0x73EF, // '@'
    0x5BEF, // 'A'
    0x3BEB, // 'B'
    0x724F, // 'C'
    0x3B6B, // 'D'
    0x73CF, // 'E'
    0x13CF, // 'F'
    0x7B4F, // 'G'
    0x5BED, // 'H'
    0x7497, // 'I'
    0x7B24, // 'J'
    0x5AED, // 'K'
    0x7249, // 'L'
    0x5B7D, // 'M'
    0x5B6F, // 'N'
    0x7B6F, // 'O'
    0x13EF, // 'P'
    0x6B6F, // 'Q'
    0x5AEF, // 'R'
    0x79CF, // 'S'
    0x2497, // 'T'
    0x7B6D, // 'U'
    0x2B6D, // 'V'
    0x5F6D, // 'W'
    0x5AAD, // 'X'
    0x24AD, // 'Y'
    0x72A7, // 'Z'
    0x6496, // '['
    0x4291, // '\'
    0x3493, // ']'
    0x002A, // '^'
    0x7000, // '_'
    0x0011, // '`'
    0x5BEF, // 'a' -> uppercase for clean tracker readability
    0x3BEB, // 'b'
    0x724F, // 'c'
    0x3B6B, // 'd'
    0x73CF, // 'e'
    0x13CF, // 'f'
    0x7B4F, // 'g'
    0x5BED, // 'h'
    0x7497, // 'i'
    0x7B24, // 'j'
    0x5AED, // 'k'
    0x7249, // 'l'
    0x5B7D, // 'm'
    0x5B6F, // 'n'
    0x7B6F, // 'o'
    0x13EF, // 'p'
    0x6B6F, // 'q'
    0x5AEF, // 'r'
    0x79CF, // 's'
    0x2497, // 't'
    0x7B6D, // 'u'
    0x2B6D, // 'v'
    0x5F6D, // 'w'
    0x5AAD, // 'x'
    0x24AD, // 'y'
    0x72A7, // 'z'
    0x64D6, // '{'
    0x2492, // '|'
    0x3593, // '}'
    0x0360, // '~'
    0x7FFF  // DEL
};

} // namespace

void Canvas::setColor(ColorRGB c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, 255);
}

void Canvas::fill(int x, int y, int w, int h, ColorRGB c) {
    if (w <= 0 || h <= 0) return;
    setColor(c);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r_, &rc);
}

void Canvas::rect(int x, int y, int w, int h, ColorRGB c) {
    if (w <= 0 || h <= 0) return;
    setColor(c);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderDrawRect(r_, &rc);
}

void Canvas::line(int x1, int y1, int x2, int y2, ColorRGB c) {
    setColor(c);
    SDL_RenderDrawLine(r_, x1, y1, x2, y2);
}

void Canvas::glyph(int x, int y, char ch) {
    int idx = static_cast<unsigned char>(ch) - 32;
    if (idx < 0 || idx >= 96) idx = 0;
    const uint16_t bits = kFont3x5[idx];
    if (bits == 0) return;
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if ((bits >> (row * 3 + col)) & 1) {
                SDL_RenderDrawPoint(r_, x + col, y + row);
            }
        }
    }
}

void Canvas::text(int x, int y, const std::string& s, ColorRGB c) {
    setColor(c);
    int cx = x;
    for (char ch : s) {
        glyph(cx, y, ch);
        cx += kCharW;
    }
}

void Canvas::textClip(int x, int y, const std::string& s, int max_chars, ColorRGB c) {
    if (max_chars <= 0) return;
    if (static_cast<int>(s.size()) <= max_chars) {
        text(x, y, s, c);
    } else {
        text(x, y, s.substr(0, static_cast<size_t>(max_chars)), c);
    }
}

} // namespace groovebox
