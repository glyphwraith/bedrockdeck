// pets.hpp - the little characters: a cat that follows the cursor and a sunflower that watches it.
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include "canvas.hpp"
#include "sprites.hpp"

namespace deck {
namespace pets {

using pix::Canvas;
using pix::Color;
using pix::Rect;

struct Rng {
    uint32_t s = 2463534242u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.f; }
    float range(float a, float b) { return a + (b - a) * f(); }
};

struct World {
    int W = 500, ground_y = 300;
    float mx = 0, my = 0;
    bool mouse_in = false;
    float idle = 0;  // seconds since the mouse last moved
    float t = 0;
    float ps = 1;  // how many screen pixels one pet pixel covers
};

struct Particle {
    float x, y, vx, vy, life, max;
    Color c;
    char kind;  // 'p' pixel, 's' spark, 'h' heart, 'z' sleepy z
};

inline void spawn_burst(std::vector<Particle>& fx, float x, float y, int n, const Color* pal, int npal, Rng& r) {
    for (int i = 0; i < n; ++i) {
        float a = r.range(0.f, 6.2831853f), sp = r.range(18.f, 48.f);
        fx.push_back({x, y, std::cos(a) * sp, std::sin(a) * sp - 14.f, r.range(0.35f, 0.7f), 0.7f, pal[r.next() % npal], i % 3 == 0 ? 's' : 'p'});
    }
}
inline void spawn_hearts(std::vector<Particle>& fx, float x, float y, int n, Rng& r) {
    for (int i = 0; i < n; ++i) fx.push_back({x + r.range(-6.f, 6.f), y, r.range(-8.f, 8.f), r.range(-26.f, -16.f), r.range(0.9f, 1.3f), 1.3f, 0xd9453a, 'h'});
}
inline void update_particles(std::vector<Particle>& fx, float dt) {
    for (auto& p : fx) {
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.kind == 'p' || p.kind == 's') p.vy += 90.f * dt;
        p.life -= dt;
    }
    fx.erase(std::remove_if(fx.begin(), fx.end(), [](const Particle& p) { return p.life <= 0; }), fx.end());
}
inline void draw_particles(Canvas& c, const std::vector<Particle>& fx, Color ink) {
    for (auto& p : fx) {
        if (p.life < 0.25f && ((int)(p.life * 40.f) & 1)) continue;  // blink out
        int x = (int)p.x, y = (int)p.y;
        if (p.kind == 'p') c.fill({x, y, 2, 2}, p.c);
        else if (p.kind == 's') { c.put(x, y, p.c); c.put(x - 1, y, p.c); c.put(x + 1, y, p.c); c.put(x, y - 1, p.c); c.put(x, y + 1, p.c); }
        else if (p.kind == 'h') c.sprite(art::heart(), x - 2, y - 2);
        else if (p.kind == 'z') c.text(x, y, "z", ink);
    }
}

// ---------------------------------------------------------------- cat
struct Cat {
    enum State { Sit, Hop, Happy, Sleep, Shake } st = Sit;
    float x = 50, lift = 0, st_t = 0;
    float target = 50, hop_from = 50, hop_to = 50, hop_t = 0;
    float blink_in = 3.f, blink_t = 0.f, tail_t = 0.f, wander_in = 6.f, zzz_t = 0.f;
    std::string bubble;
    float bubble_t = 0.f;
    Rng rng;

    Rect rect(const World& w) const { return {(int)x, w.ground_y - 16 - (int)lift, 16, 16}; }
    float center() const { return x + 8.f; }

    void say(const std::string& s, float secs = 2.2f) { bubble = s; bubble_t = secs; }

    void poke(std::vector<Particle>& fx) {
        static const char* lines[] = {"mrrp!", "nya~", "pet pet!", "purr...", "hi hi!", ":3"};
        st = Happy; st_t = 0; lift = 0;
        say(lines[rng.next() % 6], 1.8f);
        for (int i = 0; i < 3; ++i)   // hearts float out beside the head so they never cover the speech bubble
            fx.push_back({(x + 8) * ps_ + (i == 0 ? -26.f : i == 1 ? 26.f : 33.f), rng_y() - 6.f - 3.f * (float)i, i == 0 ? -4.f : 4.f, -rng.range(18.f, 26.f), 1.1f, 1.1f, 0xd9453a, 'h'});
    }
    void cheer() { if (st != Happy) { st = Happy; st_t = 0; } }
    void shake() { st = Shake; st_t = 0; }
    float rng_y() const { return (ground_ref_ - 20.f) * ps_; }
    float ground_ref_ = 300.f, ps_ = 1.f;

    void update(float dt, const World& w, std::vector<Particle>& fx) {
        ground_ref_ = (float)w.ground_y; ps_ = w.ps;
        st_t += dt; tail_t += dt;
        if (bubble_t > 0) bubble_t -= dt;
        // blinking
        blink_in -= dt;
        if (blink_in <= 0) { blink_t = 0.13f; blink_in = rng.range(2.5f, 5.5f); }
        if (blink_t > 0) blink_t -= dt;

        float maxx = (float)(w.W - 38);
        switch (st) {
            case Sit: {
                lift = 0;
                bool near_ground = w.mouse_in && w.my > w.ground_y - 34 && w.idle < 2.0f;
                float want = near_ground ? w.mx - 8.f : target;
                want = std::max(4.f, std::min(maxx, want));
                if (!near_ground) {
                    wander_in -= dt;
                    if (wander_in <= 0) { target = std::max(4.f, std::min(maxx, x + rng.range(-70.f, 70.f))); wander_in = rng.range(6.f, 11.f); }
                    want = target;
                }
                if (std::fabs(want - x) > 4.f) { st = Hop; st_t = 0; hop_t = 0; hop_from = x; hop_to = x + std::max(-14.f, std::min(14.f, want - x)); }
                if (w.idle > 25.f) { st = Sleep; st_t = 0; zzz_t = 0; }
                break;
            }
            case Hop: {
                hop_t += dt;
                const float crouch = 0.09f, air = 0.30f;
                if (hop_t < crouch) { lift = 0; }
                else if (hop_t < crouch + air) { float u = (hop_t - crouch) / air; lift = 6.f * std::sin(3.14159265f * u); x = hop_from + (hop_to - hop_from) * u; }
                else { lift = 0; x = hop_to; st = Sit; st_t = 0; }
                break;
            }
            case Happy:
                lift = std::fabs(std::sin(st_t * 9.f)) * 5.f * (st_t < 0.9f ? 1.f : 0.f);
                if (st_t > 1.3f) { st = Sit; st_t = 0; lift = 0; }
                break;
            case Shake:
                if (st_t > 0.7f) { st = Sit; st_t = 0; }
                break;
            case Sleep:
                lift = 0;
                zzz_t += dt;
                if (zzz_t > 1.4f) { zzz_t = 0; fx.push_back({(x + 12) * w.ps, ((float)w.ground_y - 20) * w.ps, 6.f, -9.f, 1.6f, 1.6f, 0x9ab6ff, 'z'}); }
                if (w.idle < 0.4f) { st = Sit; st_t = 0; say("!", 0.8f); }
                break;
        }
    }

    void draw(Canvas& c, const World& w) const {
        const auto& spr = art::cat();
        int bx = (int)std::round(x + (st == Shake ? std::sin(st_t * 60.f) * 2.f : 0.f));
        int by = w.ground_y - 16 - (int)lift;
        bool crouch = st == Hop && hop_t < 0.09f;
        int ey = 6;
        if (crouch) { c.sprite_part(spr, bx, by + 1, 0, 11); c.sprite_part(spr, bx, by + 12, 12, 16); ey = 7; }
        else c.sprite(spr, bx, by);

        // tail
        int sway = ((int)(tail_t * 2.2f) & 1) && st != Sleep ? 1 : 0;
        for (int i = 0; i < 6; ++i) {
            int xo = i >= 4 ? sway : 0;
            c.put(bx + 14 + xo, by + 14 - i + (crouch ? 1 : 0), 0xc46a22);
            c.put(bx + 15 + xo, by + 14 - i + (crouch ? 1 : 0), 0x3a2418);
        }
        c.put(bx + 14 + sway, by + 8 + (crouch ? 1 : 0), 0x3a2418);

        // eyes follow the cursor
        int dx = 0, dy = 0;
        if (w.mouse_in && st != Sleep) {
            float ddx = w.mx - (x + 8.f), ddy = w.my - (float)(w.ground_y - 10);
            dx = ddx > 24 ? 1 : ddx < -24 ? -1 : 0;
            dy = ddy < -30 ? -1 : ddy > 12 ? 1 : 0;
        }
        const Color K = 0x3a2418, Wh = 0xffffff;
        for (int side = 0; side < 2; ++side) {
            int ex = bx + (side == 0 ? 4 : 10);
            if (st == Sleep || blink_t > 0) { c.fill({ex, by + ey + 1, 2, 1}, K); }
            else if (st == Happy) { c.put(ex, by + ey + 1, K); c.put(ex + 1, by + ey, K); c.put(ex + 2, by + ey + 1, K); }
            else if (st == Shake) { c.put(ex, by + ey, K); c.put(ex + 1, by + ey + 1, K); c.put(ex + 1, by + ey, K); c.put(ex, by + ey + 1, K); }
            else { c.fill({ex + dx, by + ey + dy, 2, 2}, K); c.put(ex + dx, by + ey + dy, Wh); }
        }
        // nose and mouth
        c.put(bx + 7, by + ey + 2, 0xf2a0a8); c.put(bx + 8, by + ey + 2, 0xf2a0a8);
        if (st == Happy) { c.put(bx + 7, by + ey + 3, 0xd9453a); c.put(bx + 8, by + ey + 3, 0xd9453a); c.put(bx + 3, by + ey + 2, 0xf2a0a8); c.put(bx + 12, by + ey + 2, 0xf2a0a8); }
        else { c.put(bx + 6, by + ey + 3, K); c.put(bx + 9, by + ey + 3, K); }

    }
};

// ---------------------------------------------------------------- (cat) speech bubble, drawn at normal pixel size
inline void draw_cat_bubble(Canvas& c, const Cat& cat, int W, int ground_y, int sc) {
    if (cat.bubble_t <= 0 || cat.bubble.empty()) return;
    const Color K = 0x3a2418;
    int head_x = (int)(cat.x * sc) + 8 * sc, head_y = ground_y - (16 + (int)cat.lift) * sc;
    int tw = pix::text_w(cat.bubble) + 6, bxp = std::max(2, std::min(W - tw - 2, head_x - tw / 2)), byp = head_y - 14;
    c.rfill({bxp, byp, tw, 11}, 0xfffdf6);
    c.rframe({bxp, byp, tw, 11}, K);
    c.put(head_x - 1, byp + 11, K); c.put(head_x, byp + 11, K); c.put(head_x - 1, byp + 10, 0xfffdf6); c.put(head_x, byp + 10, 0xfffdf6);
    c.text(bxp + 3, byp + 2, cat.bubble, K);
}

// ---------------------------------------------------------------- sunflower
inline void draw_head(Canvas& c, int cx, int cy, float R, float phase, int ex, int ey, bool smile, bool face) {
    // eight clean petals; the pattern alternates between two frames (phase) so the flower "breathes" or spins
    const float step = 0.7853982f, shift = ((int)std::floor(phase) & 1) ? 0.3926991f : 0.f;
    float core = R * 0.5f, mid = (core + R) * 0.5f, a = (R - core) * 0.5f + 0.7f, b = 1.35f;
    int ri = (int)std::ceil(R) + 1;
    for (int dy = -ri; dy <= ri; ++dy)
        for (int dx = -ri; dx <= ri; ++dx) {
            float d = std::sqrt((float)(dx * dx + dy * dy));
            if (d <= core) { c.put(cx + dx, cy + dy, ((dx + dy) & 1) ? 0x6b3b1e : 0x4e2a14); continue; }
            float best = 9.f;
            for (int k = 0; k < 8; ++k) {
                float ang = shift + k * step, ux = std::cos(ang), uy = std::sin(ang);
                float along = dx * ux + dy * uy, across = -dx * uy + dy * ux;
                float v = ((along - mid) / a) * ((along - mid) / a) + (across / b) * (across / b);
                best = std::min(best, v);
            }
            if (best <= 1.f) c.put(cx + dx, cy + dy, best > 0.6f ? 0xe0a020 : 0xf7c93b);
        }
    if (face) {
        const Color cream = 0xfff3d6;
        c.put(cx - 1 + ex, cy - 1 + ey, cream); c.put(cx + 1 + ex, cy - 1 + ey, cream);
        c.put(cx - 1, cy + 1, cream); c.put(cx + 1, cy + 1, cream);
        if (smile) c.put(cx, cy + 2, cream); else c.put(cx, cy + 1, cream);
    }
}

struct Flower {
    float x = 460, bounce = 0, bounce_v = 0, happy_t = 0, tilt = 0, phase = 0;
    bool hover = false;

    Rect rect(const World& w) const { return {(int)x - 2, w.ground_y - 25, 20, 25}; }
    void poke() { bounce_v = -70.f; happy_t = 1.2f; }

    void update(float dt, const World& w) {
        phase += dt * (happy_t > 0 ? 9.f : 1.2f);
        if (happy_t > 0) happy_t -= dt;
        bounce_v += 420.f * dt;
        bounce += bounce_v * dt;
        if (bounce > 0) { bounce = 0; bounce_v = 0; }
        float want = w.mouse_in ? std::max(-2.f, std::min(2.f, (w.mx - (x + 8.f)) / 40.f)) : 0.f;
        tilt += (want - tilt) * std::min(1.f, dt * 6.f);
    }

    void draw(Canvas& c, const World& w) const {
        int gx = (int)x, base = w.ground_y;
        int hx = gx + 8 + (int)std::lround(tilt), hy = base - 17 + (int)std::lround(bounce / 4.f);
        // stem and leaves
        int sx = gx + 8, sy = base - 7;
        for (int i = 0; i <= 9; ++i) {
            float u = (float)i / 9.f;
            int px = sx + (int)std::lround((hx - sx) * u * u), py = sy - i;
            c.put(px, py, 0x5aa04a); c.put(px + 1, py, 0x3b7a3c);
        }
        c.fill({gx + 3, base - 11, 4, 1}, 0x5aa04a); c.fill({gx + 2, base - 10, 3, 1}, 0x5aa04a); c.put(gx + 2, base - 12, 0x3b7a3c);
        c.fill({gx + 10, base - 9, 4, 1}, 0x5aa04a); c.fill({gx + 11, base - 8, 3, 1}, 0x3b7a3c); c.put(gx + 14, base - 10, 0x3b7a3c);
        c.sprite(art::pot(), gx + 3, base - 7);
        int ex = w.mouse_in ? (w.mx > hx + 18 ? 1 : w.mx < hx - 18 ? -1 : 0) : 0;
        draw_head(c, hx, hy, 5.8f, phase, ex, 0, hover || happy_t > 0, true);
    }
};

}  // namespace pets
}  // namespace deck
