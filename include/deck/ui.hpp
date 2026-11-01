// ui.hpp - the whole interface, drawn with the software canvas. It knows nothing about Windows:
// the host feeds it mouse/keyboard input and data, and reads back events (what to do) and sounds (what to play).
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include "canvas.hpp"
#include "pets.hpp"
#include "sfx.hpp"
#include "sprites.hpp"
#include "theme.hpp"

namespace deck {
namespace ui {

using pix::Canvas;
using pix::Color;
using pix::Rect;

enum class Act { None, Launch, Inject, AddFiles, Refresh, RemoveSel, ExportSel, ToggleMod, ToggleTheme, ToggleSound, SetFolder };
struct Event {
    Act act = Act::None;
    int tab = 0;
    int index = -1;
};
struct Row {
    std::string name, version, size, folder;
    bool checked = false;
};
enum class Key { Up, Down, Delete, Refresh };

class Ui {
public:
    static const int kTabs = 5, kModsTab = 4, kWorldsTab = 3;
    static const int ROW_H = 13, HEAD_H = 11, SKY_H = 14, FOOT_H = 16, GROUND_H = 44, PS = 2;

    Ui() { th_ = make_theme(false); cat_.rng.s = 90210u; rng_.s = 777u; }

    // ------------------------------------------------ host -> ui
    void resize(int w, int h) { W = w; H = h; layout(); clamp_scroll(); }
    void set_rows(int tab, std::vector<Row> rows) {
        rows_[tab] = std::move(rows);
        if (tab == tab_) { if (sel_ >= (int)rows_[tab].size()) sel_ = -1; clamp_scroll(); }
    }
    void set_flags(bool layout_ok, bool running, bool busy) { layout_ok_ = layout_ok; running_ = running; busy_ = busy; }
    void set_status(const std::string& s) { status_ = s; }
    void set_theme(bool dark) { dark_ = dark; th_ = make_theme(dark); dark_anim_ = dark ? 1.f : 0.f; }
    void set_sound(bool on) { sound_ = on; }
    void say(const std::string& s) { status_ = s; if (s.size() <= 26) cat_.say(s, 2.6f); }
    void celebrate() { cat_.cheer(); flower_.poke(); play(sfx::Id::Success); static const Color pal[] = {0xf7c93b, 0xffffff, 0xe8903a}; pets::spawn_burst(fx_, (cat_.x + 8) * PS, (float)ground_y() - 14 * PS, 12, pal, 3, rng_); }
    void oops() { cat_.shake(); play(sfx::Id::Error); }

    // ------------------------------------------------ input
    void mouse_move(int x, int y) {
        layout();
        mx_ = x; my_ = y; mouse_in_ = true; last_move_ = t_;
        int h = hit(x, y);
        if (h != hover_) {
            hover_ = h;
            if (h > 0 && h < 30) { Widget* w = find(h); if (w && w->enabled) play(sfx::Id::Tick); }
            if (h == PET_FLOWER) flower_.hover = true;
        }
        if (h != PET_FLOWER) flower_.hover = false;
        row_hover_ = h == 0 ? row_at(x, y) : -1;
        if (sb_drag_) drag_scroll(y);
    }
    void mouse_leave() { mouse_in_ = false; hover_ = 0; row_hover_ = -1; flower_.hover = false; }
    void mouse_down(int x, int y) {
        layout();
        mx_ = x; my_ = y; mouse_in_ = true; last_move_ = t_;
        int h = hit(x, y);
        pressed_ = h;
        static const Color pal[] = {0xf7c93b, 0xe8903a, 0xffffff, 0x55d0c0};
        pets::spawn_burst(fx_, (float)x, (float)y, 7, pal, 4, rng_);
        if (h == SCROLL) {
            Rect th = thumb();
            if (th.has(x, y)) { sb_drag_ = true; sb_off_ = y - th.y; }
            else { scroll_t_ += (y < th.y ? -1.f : 1.f) * view_h(); clamp_scroll(); }
            play(sfx::Id::Tick);
        } else if (h == 0) {
            int r = row_at(x, y);
            if (r >= 0) {
                sel_ = r;
                if (tab_ == kModsTab && x >= list_inner().x + 2 && x < list_inner().x + 14) toggle_mod(r);
                else play(sfx::Id::Click);
            }
        }
    }
    void mouse_up(int x, int y) {
        layout();
        if (pressed_ && pressed_ == hit(x, y)) activate(pressed_);
        pressed_ = 0;
        sb_drag_ = false;
    }
    void wheel(int notches) {   // positive = scroll up
        scroll_t_ -= (float)notches * ROW_H * 3.f;
        clamp_scroll();
    }
    void key(Key k) {
        layout();
        int n = (int)rows_[tab_].size();
        if (k == Key::Up && n) { sel_ = sel_ <= 0 ? 0 : sel_ - 1; ensure_visible(); play(sfx::Id::Tick); }
        else if (k == Key::Down && n) { sel_ = std::min(n - 1, sel_ + 1); ensure_visible(); play(sfx::Id::Tick); }
        else if (k == Key::Delete && sel_ >= 0 && !busy_) { events_.push_back({Act::RemoveSel, tab_, sel_}); play(sfx::Id::Click); }
        else if (k == Key::Refresh) { events_.push_back({Act::Refresh, tab_, -1}); play(sfx::Id::Click); }
    }

    // ------------------------------------------------ time
    void tick(float dt) {
        layout();
        t_ += dt;
        dark_anim_ += ((dark_ ? 1.f : 0.f) - dark_anim_) * std::min(1.f, dt * 12.f);
        scroll_ += (scroll_t_ - scroll_) * std::min(1.f, dt * 14.f);
        if (std::fabs(scroll_t_ - scroll_) < 0.3f) scroll_ = scroll_t_;
        pets::World w = pet_world();
        cat_.update(dt, w, fx_);
        flower_.x = (float)((W - 44) / PS);
        flower_.update(dt, w);
        pets::update_particles(fx_, dt);
    }

    // ------------------------------------------------ outputs
    std::vector<Event> take_events() { auto e = std::move(events_); events_.clear(); return e; }
    bool take_sound(sfx::Id& id) {
        if (sounds_.empty()) return false;
        id = sounds_.back();   // if several arrived in one go, the most important (latest) wins
        sounds_.clear();
        return true;
    }
    bool hand() const { return hover_ != 0 || row_hover_ >= 0 || sb_drag_; }
    bool dark() const { return dark_; }
    bool sound_on() const { return sound_; }
    int tab() const { return tab_; }
    int selected() const { return sel_; }
    void select_tab(int t) { tab_ = t; sel_ = -1; scroll_ = scroll_t_ = 0; layout(); }

    // programmatic input used by the preview tool
    void poke_cat() { cat_.poke(fx_); }
    void poke_flower() { flower_.poke(); }

    void render(Canvas& c) {
        layout();
        c.ink = th_.text;
        c.accent = th_.accent;
        draw_sky(c);
        draw_ground(c);
        draw_window(c);
        draw_footer(c);
        pets::World w = pet_world();
        c.scale = PS; c.ox = 0; c.oy = pet_oy();
        flower_.draw(c, w);
        cat_.draw(c, w);
        c.scale = 1; c.ox = c.oy = 0;
        pets::draw_cat_bubble(c, cat_, W, ground_y(), PS);
        pets::draw_particles(c, fx_, th_.text);
    }

private:
    enum Id { B_LAUNCH = 1, B_INJECT, B_ADD, B_REFRESH, B_REMOVE, B_EXPORT, B_SETDIR, T0 = 10, SW_THEME = 20, SW_SOUND = 21, PET_CAT = 30, PET_FLOWER = 31, SCROLL = 40 };
    struct Widget {
        int id;
        Rect r;
        std::string label;
        bool enabled = true;
    };

    // ------------------------------------------------ geometry
    int ground_y() const { return H - FOOT_H - 2; }
    Rect list_inner() const { return list_.inset(1); }
    Rect rows_area() const { Rect i = list_inner(); return {i.x, i.y + HEAD_H, i.w, std::max(0, i.h - HEAD_H)}; }
    int view_h() const { return rows_area().h; }
    int content_h() const { return (int)rows_[tab_].size() * ROW_H; }
    float max_scroll() const { return (float)std::max(0, content_h() - view_h()); }
    bool has_scrollbar() const { return max_scroll() > 0; }
    Rect track() const { Rect a = rows_area(); return {a.right() - 7, a.y, 7, a.h}; }
    Rect thumb() const {
        Rect t = track();
        float vh = (float)view_h(), ch = (float)std::max(1, content_h());
        int th = std::max(10, (int)(t.h * vh / ch));
        float ms = max_scroll();
        int ty = t.y + (ms > 0 ? (int)((t.h - th) * (scroll_t_ / ms)) : 0);
        return {t.x, ty, t.w, th};
    }
    void clamp_scroll() { scroll_t_ = std::max(0.f, std::min(max_scroll(), scroll_t_)); scroll_ = std::max(0.f, std::min(max_scroll(), scroll_)); }
    void drag_scroll(int y) {
        Rect t = track(), th = thumb();
        float range = (float)std::max(1, t.h - th.h);
        scroll_t_ = max_scroll() * std::max(0.f, std::min(1.f, (float)(y - sb_off_ - t.y) / range));
        scroll_ = scroll_t_;
    }
    void ensure_visible() {
        if (sel_ < 0) return;
        float top = (float)(sel_ * ROW_H), bot = top + ROW_H;
        if (top < scroll_t_) scroll_t_ = top;
        else if (bot > scroll_t_ + view_h()) scroll_t_ = bot - view_h();
        clamp_scroll();
    }
    int row_at(int x, int y) const {
        Rect a = rows_area();
        if (!a.has(x, y) || (has_scrollbar() && x >= a.right() - 7)) return -1;
        int r = (int)((y - a.y + scroll_) / ROW_H);
        return r >= 0 && r < (int)rows_[tab_].size() ? r : -1;
    }
    // The pets live in "pet pixels" (each one PS screen pixels wide), so they are chunky and easy to notice.
    pets::World pet_world() const {
        pets::World w;
        w.W = W / PS; w.ground_y = ground_y() / PS; w.mx = (float)mx_ / PS; w.my = (float)my_ / PS;
        w.mouse_in = mouse_in_; w.idle = t_ - last_move_; w.t = t_; w.ps = (float)PS;
        return w;
    }
    int pet_oy() const { return ground_y() - (ground_y() / PS) * PS; }
    Rect to_screen(Rect r) const { return {r.x * PS, r.y * PS + pet_oy(), r.w * PS, r.h * PS}; }

    Widget* find(int id) { for (auto& w : widgets_) if (w.id == id) return &w; return nullptr; }

    void layout() {
        widgets_.clear();
        int fy = H - FOOT_H;
        foot_ = {0, fy, W, FOOT_H};
        int win_bottom = fy - GROUND_H + 4;
        win_ = {6, SKY_H, W - 12, std::max(60, win_bottom - SKY_H)};
        title_ = {win_.x + 2, win_.y + 2, win_.w - 4, 11};
        int cx = win_.x + 6, cw = win_.w - 12;
        int ty = title_.bottom() + 5;

        int x = cx;
        auto btn = [&](int id, const std::string& label, bool en) {
            int w = pix::text_w(label) + 12;
            widgets_.push_back({id, {x, ty, w, 14}, label, en});
            x += w + 4;
        };
        btn(B_LAUNCH, "Launch Minecraft", true);
        btn(B_INJECT, "Inject mods", true);
        btn(B_ADD, "Add files", true);
        btn(B_REFRESH, "Refresh", true);
        std::string pill = std::string("Minecraft: ") + (running_ ? "running" : "closed");
        int pw = pix::text_w(pill) + 18;
        pill_ = {cx + cw - pw, ty, pw, 14};

        static const char* names[kTabs] = {"Resource", "Behavior", "Skins", "Worlds", "Mods"};
        int tabs_y = ty + 14 + 5;
        int tx = cx;
        for (int i = 0; i < kTabs; ++i) {
            std::string label = std::string(names[i]) + " " + std::to_string(rows_[i].size());
            int w = 9 + 3 + pix::text_w(label) + 12;
            widgets_.push_back({T0 + i, {tx, tabs_y, w, 13}, label, true});
            tx += w + 2;
        }

        int ay = win_.bottom() - 5 - 14;
        actions_y_ = ay;
        int ax = cx;
        bool sel = sel_ >= 0 && !busy_;
        {
            int w = pix::text_w("Remove selected") + 12;
            widgets_.push_back({B_REMOVE, {ax, ay, w, 14}, "Remove selected", sel});
            ax += w + 4;
        }
        if (tab_ == kWorldsTab) {
            int w = pix::text_w("Export world") + 12;
            widgets_.push_back({B_EXPORT, {ax, ay, w, 14}, "Export world", sel});
        }
        list_ = {cx, tabs_y + 13, cw, std::max(20, ay - 4 - (tabs_y + 13))};
        if (!layout_ok_) {
            int w = pix::text_w("Choose Minecraft folder...") + 12;
            widgets_.push_back({B_SETDIR, {list_.x + (list_.w - w) / 2, list_.y + list_.h / 2 + 6, w, 14}, "Choose Minecraft folder...", true});
        }
        sw_theme_ = {W - 6 - 30, fy + 2, 30, 12};
        sw_sound_ = {sw_theme_.x - 5 - 14, fy + 2, 14, 12};
    }

    int hit(int x, int y) {
        for (auto& w : widgets_) if (w.r.has(x, y)) return w.id;
        if (sw_theme_.has(x, y)) return SW_THEME;
        if (sw_sound_.has(x, y)) return SW_SOUND;
        if (has_scrollbar() && track().has(x, y)) return SCROLL;
        pets::World wd = pet_world();
        if (to_screen(cat_.rect(wd)).has(x, y)) return PET_CAT;
        if (to_screen(flower_.rect(wd)).has(x, y)) return PET_FLOWER;
        return 0;
    }

    // ------------------------------------------------ behaviour
    void play(sfx::Id id) { if (sound_) sounds_.push_back(id); }

    void toggle_mod(int r) {
        auto& row = rows_[kModsTab][(size_t)r];
        row.checked = !row.checked;
        events_.push_back({Act::ToggleMod, kModsTab, r});
        play(row.checked ? sfx::Id::On : sfx::Id::Off);
    }

    void activate(int id) {
        Widget* w = find(id);
        if (w && !w->enabled) return;
        if (id >= T0 && id < T0 + kTabs) {
            if (tab_ != id - T0) { tab_ = id - T0; sel_ = -1; scroll_ = scroll_t_ = 0; }
            play(sfx::Id::Tab);
            return;
        }
        switch (id) {
            case B_LAUNCH: events_.push_back({Act::Launch, tab_, -1}); play(sfx::Id::Click); break;
            case B_INJECT: events_.push_back({Act::Inject, tab_, -1}); play(sfx::Id::Click); break;
            case B_ADD: events_.push_back({Act::AddFiles, tab_, -1}); play(sfx::Id::Click); break;
            case B_REFRESH: events_.push_back({Act::Refresh, tab_, -1}); play(sfx::Id::Click); break;
            case B_REMOVE: events_.push_back({Act::RemoveSel, tab_, sel_}); play(sfx::Id::Click); break;
            case B_EXPORT: events_.push_back({Act::ExportSel, tab_, sel_}); play(sfx::Id::Click); break;
            case B_SETDIR: events_.push_back({Act::SetFolder, tab_, -1}); play(sfx::Id::Click); break;
            case SW_THEME:
                dark_ = !dark_;
                th_ = make_theme(dark_);
                events_.push_back({Act::ToggleTheme, tab_, -1});
                play(dark_ ? sfx::Id::Moon : sfx::Id::Sun);
                break;
            case SW_SOUND:
                sound_ = !sound_;
                events_.push_back({Act::ToggleSound, tab_, -1});
                if (sound_) sounds_.push_back(sfx::Id::Pop);
                break;
            case PET_CAT: cat_.poke(fx_); play(sfx::Id::Meow); break;
            case PET_FLOWER: flower_.poke(); play(sfx::Id::Pop); break;
            default: break;
        }
    }

    // ------------------------------------------------ drawing: scenery
    void draw_sky(Canvas& c) {
        c.fill({0, 0, W, H}, th_.sky_b);
        c.vgrad({0, 0, W, SKY_H + 10}, th_.sky_a, th_.sky_b);
        if (!th_.dark) {
            // sun with little rays
            int sx = W - 22, sy = 7;
            c.disc(sx, sy, 4.5f, 0xffd75a);
            c.disc(sx, sy, 3.2f, 0xffe98a);
            const int rx[8] = {0, 0, 7, -7, 5, -5, 5, -5}, ry[8] = {-7, 7, 0, 0, -5, -5, 5, 5};
            for (int i = 0; i < 8; ++i) if (sy + ry[i] >= 0 && ((int)(t_ * 3.f) + i) % 2 == 0) c.put(sx + rx[i], sy + ry[i], 0xffd75a);
            // drifting clouds
            for (int i = 0; i < 3; ++i) {
                int cx = (int)std::fmod(t_ * (3.f + i) + i * 190.f, (float)(W + 60)) - 30, cy = 3 + (i % 2) * 3;
                c.fill({cx, cy + 2, 16, 3}, th_.cloud);
                c.fill({cx + 3, cy, 9, 3}, th_.cloud);
                c.fill({cx + 12, cy + 1, 6, 3}, th_.cloud);
            }
        } else {
            // crescent moon and twinkling stars
            int mx = W - 22, my = 7;
            c.disc(mx, my, 4.5f, 0xfff2b0);
            for (int dy = -6; dy <= 6; ++dy) for (int dx = -6; dx <= 6; ++dx) if ((dx - 2) * (dx - 2) + (dy + 1) * (dy + 1) <= 14) c.put(mx + dx, my + dy, th_.sky_a);
            for (int i = 0; i < 26; ++i) {
                unsigned h = (unsigned)(i * 2654435761u);
                int sx = (int)(h % (unsigned)std::max(1, W - 40)), sy = (int)((h >> 8) % (unsigned)(SKY_H - 2)) + 1;
                float tw = 0.5f + 0.5f * std::sin(t_ * (1.2f + (i % 5) * 0.4f) + (float)i);
                c.put(sx, sy, pix::mix(th_.sky_a, th_.star, 0.35f + 0.65f * tw));
                if (i % 6 == 0 && tw > 0.8f) { c.put(sx - 1, sy, 0x8892c8); c.put(sx + 1, sy, 0x8892c8); }
            }
            for (int i = 0; i < 2; ++i) {
                int cx = (int)std::fmod(t_ * 2.f + i * 260.f, (float)(W + 60)) - 30;
                c.fill({cx, 9 + i, 14, 2}, th_.cloud);
            }
        }
    }

    static int hill_front(int x, int top) { return top + 13 + (int)std::lround(2.0 * std::sin(x * 0.07 + 3.0) + 1.0 * std::sin(x * 0.2)); }
    static int hill_back(int x, int top) { return top + 6 + (int)std::lround(2.5 * std::sin(x * 0.045 + 1.0) + 1.5 * std::sin(x * 0.11)); }

    void draw_ground(Canvas& c) {
        int top = H - FOOT_H - GROUND_H, bottom = H - FOOT_H;
        Color far_c = pix::mix(th_.sky_b, th_.hill_back, 0.5f);
        for (int x = 0; x < W; ++x) {
            int yb = hill_back(x, top), yf = hill_front(x, top);
            int yfar = top + 2 + (int)std::lround(3.0 * std::sin(x * 0.025 + 0.5) + 1.5 * std::sin(x * 0.08));
            c.vline(x, yfar, bottom - yfar, far_c);
            c.vline(x, yb, bottom - yb, th_.hill_back);
            c.vline(x, yf, bottom - yf, th_.hill_front);
            c.put(x, yf, th_.grass_hi);
        }
        const Color lightf[4] = {0xf2a0a8, 0xf7c93b, 0xffffff, 0xff8a5c}, darkf[3] = {0xffb03b, 0x7fe0d0, 0xffe9a0};
        for (int i = 0; i < W / 17; ++i) {
            unsigned h = (unsigned)(i * 2246822519u + 12345u);
            int x = 6 + i * 17 + (int)(h % 9), yf = hill_front(x, top);
            if (x > W - 52 && x < W - 4) continue;   // keep the sunflower's spot clear
            Color f = th_.dark ? darkf[h % 3] : lightf[h % 4];
            c.put(x, yf + 2, 0x3b7a3c); c.put(x, yf + 1, 0x3b7a3c);
            c.put(x, yf, f); c.put(x - 1, yf, f); c.put(x + 1, yf, f); c.put(x, yf - 1, f);
            c.put(x, yf, th_.dark ? 0xfff2b0 : 0xfff6d8);
        }
    }

    // ------------------------------------------------ drawing: window and widgets
    void bevel(Canvas& c, Rect r, Color face, Color hi, Color lo, bool pressed) {
        c.rfill(r, face);
        c.hline(r.x + 1, r.y + 1, r.w - 2, pressed ? lo : hi);
        c.vline(r.x + 1, r.y + 1, r.h - 2, pressed ? lo : hi);
        c.hline(r.x + 1, r.bottom() - 2, r.w - 2, pressed ? hi : lo);
        c.vline(r.right() - 2, r.y + 1, r.h - 2, pressed ? hi : lo);
        c.rframe(r, th_.win_edge);
    }

    void draw_button(Canvas& c, const Widget& w) {
        bool hov = hover_ == w.id && w.enabled, prs = pressed_ == w.id && hov;
        Color face = !w.enabled ? pix::mix(th_.btn, th_.win_bg, 0.55f) : prs ? th_.btn_press : hov ? th_.btn_hover : th_.btn;
        bevel(c, w.r, face, th_.btn_hi, th_.btn_lo, prs);
        c.text_center(w.r, w.label, w.enabled ? th_.btn_text : th_.dim, prs ? 1 : 0);
    }

    const pix::Sprite& tab_icon(int i) { switch (i) { case 0: return art::picture(); case 1: return art::doc(); case 2: return art::person(); case 3: return art::globe(); default: return art::chip(); } }

    void draw_tab(Canvas& c, const Widget& w, int i) {
        bool active = tab_ == i, hov = hover_ == w.id;
        Rect r = w.r;
        if (!active) { r.y += 2; r.h -= 2; if (hov) { r.y -= 1; r.h += 1; } }
        else r.h += 1;
        Color face = active ? th_.panel_bg : hov ? th_.btn_hover : pix::mix(th_.win_bg, th_.panel_dark, th_.dark ? 0.35f : 0.18f);
        c.rfill(r, face);
        c.rframe(r, th_.win_edge);
        if (active) c.hline(r.x + 1, r.bottom() - 1, r.w - 2, th_.panel_bg);   // opens into the list below
        int ty = r.y + (r.h - 9) / 2;
        c.sprite(tab_icon(i), r.x + 5, ty);
        c.text(r.x + 17, r.y + (r.h - 7) / 2, w.label, active ? th_.text : th_.dim);
    }

    void draw_window(Canvas& c) {
        // soft shadow, outline, bevel
        c.rfill({win_.x + 2, win_.y + 2, win_.w, win_.h}, pix::mix(th_.hill_back, 0x000000, 0.3f));
        c.rfill(win_, th_.win_bg);
        c.hline(win_.x + 1, win_.y + 1, win_.w - 2, th_.win_hi);
        c.vline(win_.x + 1, win_.y + 1, win_.h - 2, th_.win_hi);
        c.hline(win_.x + 1, win_.bottom() - 2, win_.w - 2, th_.win_lo);
        c.vline(win_.right() - 2, win_.y + 1, win_.h - 2, th_.win_lo);
        c.rframe(win_, th_.win_edge);

        // title bar with retro stripes
        c.fill(title_, th_.title_bg);
        for (int y = title_.y + 1; y < title_.bottom() - 1; y += 2) c.hline(title_.x + 112, y, title_.w - 112 - 36, pix::mix(th_.title_bg, 0xffffff, 0.22f));
        pets::draw_head(c, title_.x + 7, title_.y + 5, 4.4f, t_ * 1.5f, 0, 0, false, false);
        c.text_shadow(title_.x + 16, title_.y + 2, "Bedrock Deck", th_.title_fg, pix::mix(th_.title_bg, 0x000000, 0.25f));
        for (int i = 0; i < 3; ++i) {   // decorative window buttons
            Rect b{title_.right() - 11 * (3 - i) - 1, title_.y + 2, 9, 7};
            bevel(c, b, th_.btn, th_.btn_hi, th_.btn_lo, false);
        }
        c.hline(title_.right() - 33 + 2, title_.y + 7, 4, th_.win_edge);
        c.frame({title_.right() - 22 + 2, title_.y + 3, 5, 4}, th_.win_edge);
        c.line(title_.right() - 11 + 2, title_.y + 3, title_.right() - 11 + 6, title_.y + 7, th_.win_edge);
        c.line(title_.right() - 11 + 6, title_.y + 3, title_.right() - 11 + 2, title_.y + 7, th_.win_edge);

        // status pill
        c.rfill(pill_, th_.panel_bg);
        c.rframe(pill_, th_.panel_dark);
        Color dot = running_ ? 0x4cc46a : th_.dim;
        c.disc(pill_.x + 7, pill_.y + 7, 2.2f, dot);
        std::string pill = std::string("Minecraft: ") + (running_ ? "running" : "closed");
        c.text(pill_.x + 14, pill_.y + 4, pill, running_ ? th_.accent : th_.dim);

        for (auto& w : widgets_) {
            if (w.id >= T0 && w.id < T0 + kTabs) continue;
            if (w.id == B_SETDIR) continue;
            draw_button(c, w);
        }
        draw_list(c);
        for (auto& w : widgets_) if (w.id >= T0 && w.id < T0 + kTabs) draw_tab(c, w, w.id - T0);
        for (auto& w : widgets_) if (w.id == B_SETDIR) draw_button(c, w);
        std::string hint = "drop files anywhere to install";
        int hw = pix::text_w(hint);
        c.text(win_.right() - 8 - hw - (W > 460 ? 0 : 0), actions_y_ + 4, hint, th_.dim);
    }

    void draw_list(Canvas& c) {
        // sunken panel
        c.fill(list_, th_.panel_bg);
        c.hline(list_.x, list_.y, list_.w, th_.panel_dark);
        c.vline(list_.x, list_.y, list_.h, th_.panel_dark);
        c.hline(list_.x, list_.bottom() - 1, list_.w, th_.panel_light);
        c.vline(list_.right() - 1, list_.y, list_.h, th_.panel_light);

        Rect in = list_inner(), area = rows_area();
        bool mods = tab_ == kModsTab, wide = in.w > 400 && !mods;
        int sbw = has_scrollbar() ? 7 : 0;
        int xend = in.right() - sbw - 4;
        int fol_x = 0, size_x, ver_x = 0;
        if (wide) { fol_x = xend - 120; xend = fol_x - 6; }
        size_x = xend - 48;
        xend = size_x - 6;
        if (!mods) ver_x = xend - 44;
        int name_x = mods ? in.x + 28 : in.x + 16;
        int name_w = (mods ? xend : ver_x - 6) - name_x;

        // header
        c.fill({in.x, in.y, in.w, HEAD_H}, pix::mix(th_.panel_bg, th_.panel_dark, th_.dark ? 0.25f : 0.16f));
        c.hline(in.x, in.y + HEAD_H - 1, in.w, th_.panel_dark);
        c.text(name_x, in.y + 2, "Name", th_.dim);
        if (!mods) c.text(ver_x, in.y + 2, "Version", th_.dim);
        c.text(size_x, in.y + 2, "Size", th_.dim);
        if (wide) c.text(fol_x, in.y + 2, "Folder", th_.dim);

        const auto& rows = rows_[tab_];
        if (rows.empty()) {
            std::string a = layout_ok_ ? (mods ? "No mods yet" : "Nothing here yet") : "Minecraft folder not found";
            std::string b = layout_ok_ ? (mods ? "Drop a mod .dll anywhere on this window" : "Drop .mcpack .mcaddon .mcworld files anywhere on this window") : "";
            int cy = area.y + area.h / 2 - 8;
            c.text(area.x + (area.w - pix::text_w(a)) / 2, cy, a, th_.text);
            if (!b.empty()) c.text(area.x + (area.w - pix::text_w(fit_for(b, area.w - 10))) / 2, cy + 11, fit_for(b, area.w - 10), th_.dim);
            if (layout_ok_) {
                int ax = area.x + area.w / 2, ay = cy + 24 + (((int)(t_ * 3.f)) & 1);
                c.put(ax - 2, ay, th_.accent); c.put(ax - 1, ay + 1, th_.accent); c.put(ax, ay + 2, th_.accent); c.put(ax + 1, ay + 1, th_.accent); c.put(ax + 2, ay, th_.accent);
            }
        } else {
            c.set_clip(area);
            int first = std::max(0, (int)(scroll_ / ROW_H));
            for (int i = first; i < (int)rows.size(); ++i) {
                int y = area.y + i * ROW_H - (int)scroll_;
                if (y >= area.bottom()) break;
                const Row& r = rows[(size_t)i];
                bool selr = i == sel_, hov = i == row_hover_;
                Color bg = selr ? th_.select : hov ? th_.hover : (i & 1) ? th_.panel_alt : th_.panel_bg;
                Color fg = selr ? th_.select_text : th_.text, fg2 = selr ? th_.select_text : th_.dim;
                c.fill({area.x, y, area.w - sbw, ROW_H}, bg);
                int iy = y + (ROW_H - 9) / 2;
                if (mods) {
                    Rect cb{in.x + 4, y + 2, 9, 9};
                    c.fill(cb, th_.panel_bg);
                    c.frame(cb, th_.win_edge);
                    if (r.checked) {
                        c.line(cb.x + 2, cb.y + 4, cb.x + 3, cb.y + 6, th_.accent); c.line(cb.x + 3, cb.y + 6, cb.x + 6, cb.y + 2, th_.accent);
                        c.line(cb.x + 2, cb.y + 5, cb.x + 3, cb.y + 7, th_.accent); c.line(cb.x + 3, cb.y + 7, cb.x + 6, cb.y + 3, th_.accent);
                    }
                    c.sprite(art::chip(), in.x + 16, iy);
                } else {
                    c.sprite(tab_icon(tab_), in.x + 4, iy);
                }
                c.text(name_x, y + 3, pix::fit_text(r.name, name_w), fg);
                if (!mods) c.text(ver_x, y + 3, pix::fit_text(r.version, 44), fg2);
                c.text(size_x, y + 3, r.size, fg2);
                if (wide) c.text(fol_x, y + 3, pix::fit_text(r.folder, 120), fg2);
            }
            c.reset_clip();
        }

        if (sbw) {
            Rect t = track(), th = thumb();
            c.fill(t, pix::mix(th_.panel_bg, th_.panel_dark, 0.3f));
            bevel(c, th, sb_drag_ ? th_.btn_press : th_.btn, th_.btn_hi, th_.btn_lo, false);
        }
    }

    static std::string fit_for(const std::string& s, int w) { return pix::fit_text(s, w); }

    // ------------------------------------------------ drawing: footer with the sun/moon switch
    void draw_footer(Canvas& c) {
        c.fill(foot_, th_.foot_bg);
        c.hline(0, foot_.y, W, th_.win_edge);
        c.hline(0, foot_.y + 1, W, th_.win_hi);
        // status text, with a little spinner while working
        int tx = 6;
        if (busy_) {
            static const char* sp[4] = {"|", "/", "-", "\\"};
            c.text(tx, foot_.y + 5, sp[(int)(t_ * 8.f) & 3], th_.accent);
            tx += 10;
        }
        std::string s = busy_ && status_.empty() ? "working..." : status_;
        c.text(tx, foot_.y + 5, pix::fit_text(s, sw_sound_.x - tx - 8), th_.foot_text);

        // sound button
        bool hs = hover_ == SW_SOUND, ps = pressed_ == SW_SOUND && hs;
        bevel(c, sw_sound_, ps ? th_.btn_press : hs ? th_.btn_hover : th_.btn, th_.btn_hi, th_.btn_lo, ps);
        c.ink = th_.btn_text;
        c.sprite(sound_ ? art::spk_on() : art::spk_off(), sw_sound_.x + 2, sw_sound_.y + 1);
        c.ink = th_.text;

        // theme switch: sun on the left, moon on the right, a knob that slides
        Rect r = sw_theme_;
        c.rfill(r, pix::mix(0x9fd6ee, th_.track, dark_anim_));
        c.rframe(r, th_.win_edge);
        draw_sun(c, r.x + 3, r.y + 2, dark_anim_ < 0.5f);
        draw_moon(c, r.x + r.w - 12, r.y + 2, dark_anim_ >= 0.5f);
        int kx = r.x + 1 + (int)std::lround((r.w - 14) * dark_anim_);
        bool hk = hover_ == SW_THEME;
        bevel(c, {kx, r.y + 1, 12, 10}, hk ? th_.btn_hover : th_.btn, th_.btn_hi, th_.btn_lo, pressed_ == SW_THEME && hk);
        if (dark_anim_ < 0.5f) draw_sun(c, kx + 2, r.y + 1, true); else draw_moon(c, kx + 2, r.y + 1, true);
    }
    void draw_sun(Canvas& c, int x, int y, bool bright) {
        Color a = bright ? 0xf7a81b : 0xb8a07a, b = bright ? 0xffd75a : 0xc9b896;
        c.disc(x + 4, y + 4, 2.3f, b);
        const int rx[8] = {4, 4, 0, 8, 1, 7, 1, 7}, ry[8] = {0, 8, 4, 4, 1, 1, 7, 7};
        for (int i = 0; i < 8; ++i) c.put(x + rx[i], y + ry[i], a);
    }
    void draw_moon(Canvas& c, int x, int y, bool bright) {
        Color m = bright ? 0xfff2b0 : 0x8f9ac8;
        for (int dy = 0; dy < 9; ++dy) for (int dx = 0; dx < 9; ++dx) {
            float d1 = (dx - 4) * (dx - 4) + (dy - 4) * (dy - 4), d2 = (dx - 6) * (dx - 6) + (dy - 3) * (dy - 3);
            if (d1 <= 12.5f && d2 > 10.f) c.put(x + dx, y + dy, m);
        }
    }

    // ------------------------------------------------ state
    int W = 500, H = 340;
    std::vector<Row> rows_[kTabs];
    bool layout_ok_ = true, running_ = false, busy_ = false, dark_ = false, sound_ = true;
    float dark_anim_ = 0.f;
    std::string status_ = "Ready";
    int tab_ = 0, sel_ = -1;
    float scroll_ = 0.f, scroll_t_ = 0.f;
    int mx_ = 0, my_ = 0, hover_ = 0, pressed_ = 0, row_hover_ = -1, sb_off_ = 0, actions_y_ = 0;
    bool mouse_in_ = false, sb_drag_ = false;
    float t_ = 0.f, last_move_ = 0.f;
    Theme th_;
    std::vector<Widget> widgets_;
    Rect win_, title_, pill_, list_, foot_, sw_theme_, sw_sound_;
    pets::Cat cat_;
    pets::Flower flower_;
    std::vector<pets::Particle> fx_;
    pets::Rng rng_;
    std::vector<Event> events_;
    std::vector<sfx::Id> sounds_;
};

}  // namespace ui
}  // namespace deck
