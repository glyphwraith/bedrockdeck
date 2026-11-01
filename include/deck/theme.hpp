// theme.hpp - the two color schemes: a warm sunflower-and-cream day, and a midnight-indigo night.
#pragma once
#include "canvas.hpp"

namespace deck {
namespace ui {

struct Theme {
    bool dark = false;
    pix::Color sky_a, sky_b, star, cloud;
    pix::Color hill_back, hill_front, grass_hi;
    pix::Color win_bg, win_edge, win_hi, win_lo, title_bg, title_fg;
    pix::Color panel_bg, panel_alt, panel_dark, panel_light;
    pix::Color text, dim;
    pix::Color btn, btn_hi, btn_lo, btn_press, btn_text, btn_hover;
    pix::Color accent, accent2, hover, select, select_text;
    pix::Color foot_bg, foot_text, track;
};

inline Theme make_theme(bool dark) {
    using pix::rgb;
    Theme t;
    t.dark = dark;
    if (!dark) {
        t.sky_a = 0x7fc8c0; t.sky_b = 0xfbeccb; t.star = 0xffffff; t.cloud = 0xffffff;
        t.hill_back = 0x9ccaa0; t.hill_front = 0x6fae7e; t.grass_hi = 0x8fc48f;
        t.win_bg = 0xfbf1d9; t.win_edge = 0x5a3d2b; t.win_hi = 0xfffaf0; t.win_lo = 0xd9bf8e;
        t.title_bg = 0xf0a83a; t.title_fg = 0x3b2a1e;
        t.panel_bg = 0xfffaf0; t.panel_alt = 0xfbf0d6; t.panel_dark = 0xb9966b; t.panel_light = 0xfffdf6;
        t.text = 0x3b2a1e; t.dim = 0x8a6d4f;
        t.btn = 0xf7d98a; t.btn_hi = 0xfff0bd; t.btn_lo = 0xc79a45; t.btn_press = 0xe8c366; t.btn_text = 0x3b2a1e; t.btn_hover = 0xffe9a8;
        t.accent = 0x3f9a92; t.accent2 = 0xe0572c; t.hover = 0xffe9a8; t.select = 0xf2b84b; t.select_text = 0x3b2a1e;
        t.foot_bg = 0xe7c98e; t.foot_text = 0x3b2a1e; t.track = 0x9fd6ee;
    } else {
        t.sky_a = 0x0c1030; t.sky_b = 0x2e3f82; t.star = 0xfff2b0; t.cloud = 0x3a4a8c;
        t.hill_back = 0x1e3a4a; t.hill_front = 0x16303c; t.grass_hi = 0x2a5560;
        t.win_bg = 0x1f2748; t.win_edge = 0x0a0d1f; t.win_hi = 0x3f4f93; t.win_lo = 0x11162e;
        t.title_bg = 0x3b4a96; t.title_fg = 0xffd98a;
        t.panel_bg = 0x151a34; t.panel_alt = 0x1a2140; t.panel_dark = 0x0a0d1f; t.panel_light = 0x3a4888;
        t.text = 0xf1e6cc; t.dim = 0x8f9ac8;
        t.btn = 0x36427f; t.btn_hi = 0x5565b0; t.btn_lo = 0x1c2450; t.btn_press = 0x2a346a; t.btn_text = 0xfbe9c0; t.btn_hover = 0x46549a;
        t.accent = 0x55d0c0; t.accent2 = 0xffb03b; t.hover = 0x34407a; t.select = 0x5b4fa0; t.select_text = 0xfff2d0;
        t.foot_bg = 0x141a38; t.foot_text = 0xf1e6cc; t.track = 0x1c2450;
    }
    return t;
}

}  // namespace ui
}  // namespace deck
