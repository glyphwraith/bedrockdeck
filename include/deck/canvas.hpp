// canvas.hpp - a tiny software pixel canvas: rectangles, lines, sprites and a 5x7 bitmap font.
// The whole interface is drawn here at a low "virtual" resolution and scaled up with crisp pixels.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace deck {
namespace pix {

using Color = uint32_t;  // 0x00RRGGBB
constexpr Color rgb(int r, int g, int b) { return ((Color)(r & 255) << 16) | ((Color)(g & 255) << 8) | (Color)(b & 255); }
inline int cr(Color c) { return (int)((c >> 16) & 255); }
inline int cg(Color c) { return (int)((c >> 8) & 255); }
inline int cb(Color c) { return (int)(c & 255); }
inline Color mix(Color a, Color b, float t) {
    t = std::max(0.f, std::min(1.f, t));
    return rgb((int)(cr(a) + (cr(b) - cr(a)) * t + 0.5f), (int)(cg(a) + (cg(b) - cg(a)) * t + 0.5f), (int)(cb(a) + (cb(b) - cb(a)) * t + 0.5f));
}

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool has(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

// ---------------------------------------------------------------- 5x7 font (ASCII 32..126), column-major, bit0 = top row
inline const uint8_t kFont[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5f,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7f,0x14,0x7f,0x14},
    {0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1c,0x22,0x41,0x00},{0x00,0x41,0x22,0x1c,0x00},{0x14,0x08,0x3e,0x08,0x14},{0x08,0x08,0x3e,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3e},{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},
    {0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},
    {0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7f,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7f,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7f,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7f},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7e,0x09,0x01,0x02},{0x0c,0x52,0x52,0x52,0x3e},
    {0x7f,0x08,0x04,0x04,0x78},{0x00,0x44,0x7d,0x40,0x00},{0x20,0x40,0x44,0x3d,0x00},{0x7f,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7f,0x40,0x00},{0x7c,0x04,0x18,0x04,0x78},{0x7c,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7c,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7c},{0x7c,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3f,0x44,0x40,0x20},{0x3c,0x40,0x40,0x20,0x7c},{0x1c,0x20,0x40,0x20,0x1c},{0x3c,0x40,0x30,0x40,0x3c},
    {0x44,0x28,0x10,0x28,0x44},{0x0c,0x50,0x50,0x50,0x3c},{0x44,0x64,0x54,0x4c,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7f,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08}};

// Next character of a UTF-8 string. Anything outside ASCII becomes '?' (the pixel font is ASCII only).
inline int utf8_next(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { ++i; return c; }
    size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    i = std::min(s.size(), i + n);
    return '?';
}
inline int text_w(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) { utf8_next(s, i); ++n; }
    return n * 6;
}
inline std::string fit_text(const std::string& s, int maxw) {
    if (text_w(s) <= maxw) return s;
    std::string o;
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        size_t st = i;
        utf8_next(s, i);
        if (w + 6 + 12 > maxw) break;
        o += s.substr(st, i - st);
        w += 6;
    }
    return o + "..";
}

// ---------------------------------------------------------------- sprites
struct Sprite {
    int w = 0, h = 0;
    std::vector<char> c;
};
inline Sprite make_sprite(std::initializer_list<const char*> rows) {
    Sprite s;
    s.h = (int)rows.size();
    for (auto r : rows) {
        std::string row = r;
        s.w = (int)row.size();
        s.c.insert(s.c.end(), row.begin(), row.end());
    }
    return s;
}
// Fixed art palette; '#' and '+' take the current theme's ink and accent colors.
inline bool sprite_color(char ch, Color ink, Color accent, Color& out) {
    switch (ch) {
        case 'k': out = 0x3a2418; return true;   // outline
        case 'o': out = 0xe8903a; return true;   // orange
        case 'O': out = 0xc46a22; return true;   // dark orange
        case 'w': out = 0xfff3d6; return true;   // cream
        case 'p': out = 0xf2a0a8; return true;   // pink
        case 'y': out = 0xf7c93b; return true;   // yellow
        case 'Y': out = 0xe0a020; return true;
        case 'n': out = 0x6b3b1e; return true;   // brown
        case 'N': out = 0x4e2a14; return true;
        case 'g': out = 0x5aa04a; return true;   // green
        case 'G': out = 0x3b7a3c; return true;
        case 't': out = 0xc8643c; return true;   // terracotta
        case 'T': out = 0x9c4524; return true;
        case 'r': out = 0xd9453a; return true;   // red
        case 'h': out = 0xffffff; return true;
        case 'b': out = 0x4a7fd0; return true;   // blue
        case 's': out = 0x7cc4d8; return true;   // sky
        case '#': out = ink; return true;
        case '+': out = accent; return true;
        default: return false;
    }
}

// ---------------------------------------------------------------- canvas
struct Canvas {
    int w = 0, h = 0;
    std::vector<Color> px;
    Rect clip;
    Color ink = 0x3a2418, accent = 0x3f9a92;
    int scale = 1, ox = 0, oy = 0;   // scale > 1 draws every pixel as a scale x scale block (chunky pets)

    void resize(int W, int H) { w = std::max(1, W); h = std::max(1, H); px.assign((size_t)w * h, 0); clip = {0, 0, w, h}; }
    void set_clip(Rect r) {
        int x0 = std::max(0, r.x), y0 = std::max(0, r.y), x1 = std::min(w, r.right()), y1 = std::min(h, r.bottom());
        clip = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
    }
    void reset_clip() { clip = {0, 0, w, h}; }

    void put(int x, int y, Color c) {
        if (scale != 1) { fill_raw({ox + x * scale, oy + y * scale, scale, scale}, c); return; }
        if (x < clip.x || y < clip.y || x >= clip.x + clip.w || y >= clip.y + clip.h) return;
        px[(size_t)y * w + x] = c;
    }
    Color get(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h) ? 0 : px[(size_t)y * w + x]; }
    void fill(Rect r, Color c) {
        if (scale != 1) r = {ox + r.x * scale, oy + r.y * scale, r.w * scale, r.h * scale};
        fill_raw(r, c);
    }
    void fill_raw(Rect r, Color c) {
        int x0 = std::max(r.x, clip.x), y0 = std::max(r.y, clip.y), x1 = std::min(r.right(), clip.right()), y1 = std::min(r.bottom(), clip.bottom());
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) px[(size_t)y * w + x] = c;
    }
    void hline(int x, int y, int len, Color c) { fill({x, y, len, 1}, c); }
    void vline(int x, int y, int len, Color c) { fill({x, y, 1, len}, c); }
    void frame(Rect r, Color c) { hline(r.x, r.y, r.w, c); hline(r.x, r.bottom() - 1, r.w, c); vline(r.x, r.y, r.h, c); vline(r.right() - 1, r.y, r.h, c); }
    // rectangle with the four corner pixels cut off (cute rounded retro look)
    void rfill(Rect r, Color c) {
        fill({r.x + 1, r.y, r.w - 2, r.h}, c);
        fill({r.x, r.y + 1, 1, r.h - 2}, c);
        fill({r.right() - 1, r.y + 1, 1, r.h - 2}, c);
    }
    void rframe(Rect r, Color c) {
        hline(r.x + 1, r.y, r.w - 2, c); hline(r.x + 1, r.bottom() - 1, r.w - 2, c);
        vline(r.x, r.y + 1, r.h - 2, c); vline(r.right() - 1, r.y + 1, r.h - 2, c);
    }
    void line(int x0, int y0, int x1, int y1, Color c) {
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            put(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void disc(int cx, int cy, float r, Color c) {
        int ri = (int)std::ceil(r);
        for (int y = -ri; y <= ri; ++y) for (int x = -ri; x <= ri; ++x) if (x * x + y * y <= r * r) put(cx + x, cy + y, c);
    }

    // 4x4 ordered dithering: gives smooth-looking gradients with only two palette colors
    static bool bayer(int x, int y, float t) {
        static const int m[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        return t * 16.f > (float)m[y & 3][x & 3] + 0.5f;
    }
    void vgrad(Rect r, Color a, Color b, float t0 = 0.f, float t1 = 1.f) {
        for (int y = r.y; y < r.bottom(); ++y) {
            float t = t0 + (t1 - t0) * (float)(y - r.y) / (float)std::max(1, r.h - 1);
            for (int x = r.x; x < r.right(); ++x) put(x, y, bayer(x, y, t) ? b : a);
        }
    }

    int text(int x, int y, const std::string& s, Color c) {
        int cx = x;
        for (size_t i = 0; i < s.size();) {
            int ch = utf8_next(s, i);
            if (ch < 32 || ch > 126) ch = '?';
            const uint8_t* g = kFont[ch - 32];
            for (int col = 0; col < 5; ++col) for (int row = 0; row < 7; ++row) if ((g[col] >> row) & 1) put(cx + col, y + row, c);
            cx += 6;
        }
        return cx - x;
    }
    void text_shadow(int x, int y, const std::string& s, Color c, Color shadow) { text(x + 1, y + 1, s, shadow); text(x, y, s, c); }
    void text_center(Rect r, const std::string& s, Color c, int dy = 0) { text(r.x + (r.w - text_w(s)) / 2, r.y + (r.h - 7) / 2 + dy, s, c); }

    // rows [r0, r1) of a sprite, top-left at (x, y); flip mirrors horizontally
    void sprite_part(const Sprite& s, int x, int y, int r0, int r1, bool flip = false) {
        for (int j = r0; j < std::min(r1, s.h); ++j)
            for (int i = 0; i < s.w; ++i) {
                Color col;
                if (!sprite_color(s.c[(size_t)j * s.w + i], ink, accent, col)) continue;
                put(x + (flip ? s.w - 1 - i : i), y + (j - r0), col);
            }
    }
    void sprite(const Sprite& s, int x, int y, bool flip = false) { sprite_part(s, x, y, 0, s.h, flip); }
};

}  // namespace pix
}  // namespace deck
