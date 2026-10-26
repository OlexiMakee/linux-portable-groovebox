#pragma once

#include "config.hpp"

#include <string>

struct SDL_Renderer;

namespace groovebox {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Canvas — єдина точка доступу до піксельних примітивів (SRP: тільки малювання, нуль бізнес-логіки)
class Canvas {
public:
    explicit Canvas(SDL_Renderer* r, int w, int h) : r_(r), w_(w), h_(h) {}

    int width() const { return w_; }
    int height() const { return h_; }

    void fill(int x, int y, int w, int h, ColorRGB c);
    void rect(int x, int y, int w, int h, ColorRGB c);
    void line(int x1, int y1, int x2, int y2, ColorRGB c);
    void text(int x, int y, const std::string& s, ColorRGB c);
    // Текст, обрізаний до max_chars символів
    void textClip(int x, int y, const std::string& s, int max_chars, ColorRGB c);

    static constexpr int kCharW = 4;
    static int textWidth(const std::string& s) { return static_cast<int>(s.size()) * kCharW; }

private:
    void setColor(ColorRGB c);
    void glyph(int x, int y, char ch);

    SDL_Renderer* r_;
    int w_;
    int h_;
};

} // namespace groovebox
