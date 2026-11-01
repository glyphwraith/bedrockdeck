// gui_win.hpp - the Windows host for the pixel interface in ui.hpp.
// It only creates a window, forwards mouse/keyboard to the interface, blits the rendered frame, and plays sounds.
// All drawing is done by our own software renderer, so there are no standard Windows controls here.
#pragma once
#ifdef _WIN32

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "app.hpp"
#include "ui.hpp"

#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mmsystem.h>

#ifdef _MSC_VER
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#endif

namespace deck {
namespace gui {

static const UINT WM_APP_STATE = WM_APP + 1;  // lParam: State*   (fresh scan of the Minecraft folders)
static const UINT WM_APP_DONE = WM_APP + 2;   // lParam: std::string*  (result text of a background job)
static const UINT_PTR TIMER_ANIM = 1, TIMER_GAME = 2;

struct App {
    Settings st;
    Prefs prefs;
    HWND hwnd = nullptr;
    ui::Ui ui;
    State state;
    std::vector<size_t> map[ui::Ui::kTabs];   // list row -> index into state.items (tabs 0-3) or state.mods (tab 4)
    pix::Canvas canvas;
    HDC memdc = nullptr;
    HBITMAP dib = nullptr;
    void* bits = nullptr;
    int cw = 0, ch = 0, scale = 2;
    bool tracking = false, asked_folder = false, running = false;
    std::atomic<bool> busy{false};
};
inline App g;

inline const char* kKinds[4] = {"resource", "behavior", "skin", "world"};

// ---------------------------------------------------------------- sound
inline void play_sound(sfx::Id id) {
    const auto& w = sfx::get(id);
    PlaySoundW((LPCWSTR)w.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
inline void pump() {   // hand the interface's pending events to the app, and play its sounds
    sfx::Id id;
    if (g.ui.take_sound(id)) play_sound(id);
}

// ---------------------------------------------------------------- data -> interface
inline void push_rows() {
    for (int t = 0; t < 4; ++t) {
        std::vector<ui::Row> rows;
        g.map[t].clear();
        for (size_t i = 0; i < g.state.items.size(); ++i) {
            auto& it = g.state.items[i];
            if (it.kind != kKinds[t]) continue;
            rows.push_back({it.name, it.version, fmt_size(it.size), it.folder, false});
            g.map[t].push_back(i);
        }
        g.ui.set_rows(t, std::move(rows));
    }
    std::vector<ui::Row> mods;
    g.map[4].clear();
    for (size_t i = 0; i < g.state.mods.size(); ++i) {
        auto& m = g.state.mods[i];
        mods.push_back({m.name, "", fmt_size(m.size), "", m.enabled});
        g.map[4].push_back(i);
    }
    g.ui.set_rows(4, std::move(mods));
    g.ui.set_flags(g.state.layout.ok(), g.running, g.busy);
}

// ---------------------------------------------------------------- background work (keeps the window responsive)
inline void refresh() {
    Settings st = g.st;
    HWND h = g.hwnd;
    std::thread([st, h]() {
        State* s = nullptr;
        try { s = new State(load_state(st)); } catch (...) { s = new State(); }
        if (!PostMessageW(h, WM_APP_STATE, 0, (LPARAM)s)) delete s;
    }).detach();
}

inline void run_bg(const std::string& busy_text, std::function<std::string()> job) {
    if (g.busy.exchange(true)) { g.ui.say("Still busy with the last task..."); g.ui.oops(); return; }
    g.ui.set_flags(g.state.layout.ok(), g.running, true);
    g.ui.set_status(busy_text);
    HWND h = g.hwnd;
    std::thread([h, job]() {
        std::string msg;
        try { msg = job(); } catch (const std::exception& e) { msg = std::string("Error: ") + e.what(); }
        auto* p = new std::string(std::move(msg));
        if (!PostMessageW(h, WM_APP_DONE, 0, (LPARAM)p)) delete p;
    }).detach();
}

inline void install_paths(const std::vector<fs::path>& files) {
    Settings st = g.st;
    run_bg("Installing...", [st, files]() -> std::string {
        Layout L = current_layout(st);
        std::vector<std::string> out;
        for (auto& f : files) {
            std::string name = S(f.filename());
            try {
                if (ends_with(lower(name), ".dll")) {
                    add_mod(st.data, name, read_file(f));
                    out.push_back("Added mod " + name + " (tick it in the Mods tab to enable)");
                } else if (!L.ok()) {
                    out.push_back("Minecraft folder not found. Click \"Choose Minecraft folder\" first.");
                    break;
                } else {
                    Report r = install_file(L, name, read_file(f));
                    for (auto& l : r.lines) out.push_back(l);
                }
            } catch (const std::exception& e) {
                out.push_back(name + ": " + e.what());
            }
        }
        return join_lines(out);
    });
}

// ---------------------------------------------------------------- dialogs and actions
inline void open_folder(const fs::path& p) { ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

inline void add_dialog() {
    std::vector<wchar_t> buf(32768, L'\0');
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o;
    o.hwndOwner = g.hwnd;
    o.lpstrFile = buf.data();
    o.nMaxFile = (DWORD)buf.size();
    o.lpstrFilter = L"Minecraft content\0*.mcpack;*.mcaddon;*.mcworld;*.mctemplate;*.zip;*.dll\0All files\0*.*\0";
    o.lpstrTitle = L"Add packs, worlds or mods";
    o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&o)) return;
    std::vector<fs::path> files;
    const wchar_t* p = buf.data();
    std::wstring first = p;
    p += first.size() + 1;
    if (*p == L'\0') files.push_back(fs::path(first));
    else while (*p) { std::wstring n = p; files.push_back(fs::path(first) / n); p += n.size() + 1; }
    install_paths(files);
}

inline void set_dir_dialog() {
    BROWSEINFOW bi{};
    bi.hwndOwner = g.hwnd;
    bi.lpszTitle = L"Select your Minecraft \"com.mojang\" folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH] = {0};
    if (SHGetPathFromIDListW(pidl, path)) {
        try { save_dir(g.st, fs::path(std::wstring(path))); } catch (...) {}
        refresh();
    }
    CoTaskMemFree(pidl);
}

inline bool ask(const std::wstring& text, UINT icon) { return MessageBoxW(g.hwnd, text.c_str(), L"Bedrock Deck", MB_YESNO | icon) == IDYES; }
inline void tell(const std::wstring& text, UINT icon) { MessageBoxW(g.hwnd, text.c_str(), L"Bedrock Deck", MB_OK | icon); }

inline void do_remove(int tab, int row) {
    if (tab < 0 || tab >= ui::Ui::kTabs || row < 0 || row >= (int)g.map[tab].size()) return;
    Settings st = g.st;
    size_t idx = g.map[tab][(size_t)row];
    if (tab == ui::Ui::kModsTab) {
        std::string name = g.state.mods[idx].name;
        if (!ask(L"Remove the mod \"" + widen(name) + L"\"?", MB_ICONQUESTION)) return;
        run_bg("Removing...", [st, name]() { remove_mod(st.data, name); return "Removed mod " + name; });
    } else {
        Item it = g.state.items[idx];
        std::wstring q = L"Remove \"" + widen(it.name) + L"\"?";
        if (it.kind == "world") q += L"\n\nA backup copy (.mcworld) is saved first.";
        if (!ask(q, MB_ICONQUESTION)) return;
        run_bg("Removing...", [st, it]() { return remove_item(current_layout(st), st.data, it.kind, it.folder); });
    }
}

inline void do_export(int row) {
    if (row < 0 || row >= (int)g.map[ui::Ui::kWorldsTab].size()) return;
    Settings st = g.st;
    std::string folder = g.state.items[g.map[ui::Ui::kWorldsTab][(size_t)row]].folder;
    run_bg("Exporting world (big worlds take a while)...", [st, folder]() { return "Exported to " + S(backup_world(current_layout(st), st.data, folder)); });
}

inline void do_inject() {
    std::vector<std::string> enabled;
    for (auto& m : g.state.mods) if (m.enabled) enabled.push_back(m.name);
    if (enabled.empty()) { tell(L"No mods are switched on.\n\nOpen the Mods tab and tick the ones you want.", MB_ICONINFORMATION); return; }
    if (!minecraft_running()) { tell(L"Minecraft is not running.\n\nClick \"Launch Minecraft\", wait for the title screen, then inject.", MB_ICONINFORMATION); return; }
    std::wstring q = L"These mods will be loaded into Minecraft:\n\n";
    for (auto& n : enabled) q += L"  \u2022 " + widen(n) + L"\n";
    q += L"\nOnly continue if you trust them. Some servers ban modified clients.";
    if (!ask(q, MB_ICONWARNING)) return;
    fs::path data = g.st.data;
    run_bg("Loading mods into Minecraft...", [data]() { return join_lines(inject_enabled(data)); });
}

inline void handle(const ui::Event& e) {
    switch (e.act) {
        case ui::Act::Launch: g.ui.say(launch_minecraft()); break;
        case ui::Act::Inject: do_inject(); break;
        case ui::Act::AddFiles: add_dialog(); break;
        case ui::Act::Refresh: refresh(); break;
        case ui::Act::RemoveSel: do_remove(e.tab, e.index); break;
        case ui::Act::ExportSel: do_export(e.index); break;
        case ui::Act::SetFolder: set_dir_dialog(); break;
        case ui::Act::ToggleMod:
            if (e.index >= 0 && e.index < (int)g.map[ui::Ui::kModsTab].size()) {
                auto& m = g.state.mods[g.map[ui::Ui::kModsTab][(size_t)e.index]];
                bool on = !m.enabled;
                try { set_mod_enabled(g.st.data, m.name, on); m.enabled = on; g.ui.set_status(m.name + (on ? " enabled" : " disabled")); }
                catch (const std::exception& ex) { g.ui.set_status(std::string("Could not change mod: ") + ex.what()); g.ui.oops(); refresh(); }
            }
            break;
        case ui::Act::ToggleTheme:
        case ui::Act::ToggleSound:
            g.prefs.dark = g.ui.dark();
            g.prefs.sound = g.ui.sound_on();
            save_prefs(g.st, g.prefs);
            break;
        default: break;
    }
}

inline void process_events() {
    for (auto& e : g.ui.take_events()) handle(e);
    pump();
}

// ---------------------------------------------------------------- drawing
inline void ensure_buffer(int cw, int ch) {
    if (cw < 1 || ch < 1) return;
    if (g.dib && cw == g.cw && ch == g.ch) return;
    if (g.dib) { DeleteObject(g.dib); g.dib = nullptr; }
    HDC dc = GetDC(g.hwnd);
    if (!g.memdc) g.memdc = CreateCompatibleDC(dc);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cw;
    bmi.bmiHeader.biHeight = -ch;   // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g.dib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &g.bits, nullptr, 0);
    ReleaseDC(g.hwnd, dc);
    if (g.dib) SelectObject(g.memdc, g.dib);
    g.cw = cw; g.ch = ch;
}

inline void render_frame() {
    if (!g.dib || !g.bits) return;
    int vw = std::max(1, g.cw / g.scale), vh = std::max(1, g.ch / g.scale);
    if (g.canvas.w != vw || g.canvas.h != vh) { g.canvas.resize(vw, vh); g.ui.resize(vw, vh); }
    g.ui.render(g.canvas);
    // crisp integer upscale: every virtual pixel becomes a scale x scale block
    uint32_t* dst = (uint32_t*)g.bits;
    for (int ry = 0; ry < g.ch; ++ry) {
        uint32_t* row = dst + (size_t)ry * g.cw;
        if (ry % g.scale == 0 || ry >= vh * g.scale) {
            int vy = std::min(ry / g.scale, vh - 1);
            const uint32_t* src = &g.canvas.px[(size_t)vy * vw];
            for (int rx = 0; rx < g.cw; ++rx) row[rx] = src[std::min(rx / g.scale, vw - 1)];
        } else {
            std::memcpy(row, row - g.cw, (size_t)g.cw * sizeof(uint32_t));
        }
    }
}

inline void update_title_size(HWND hwnd, MINMAXINFO* m) {
    RECT r = {0, 0, 440 * g.scale, 300 * g.scale};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    m->ptMinTrackSize.x = r.right - r.left;
    m->ptMinTrackSize.y = r.bottom - r.top;
    (void)hwnd;
}

// ---------------------------------------------------------------- window procedure
inline LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, TIMER_ANIM, 33, nullptr);
        SetTimer(hwnd, TIMER_GAME, 2500, nullptr);
        g.ui.set_theme(g.prefs.dark);
        g.ui.set_sound(g.prefs.sound);
        g.ui.set_status("Hi! Drag packs, worlds or mods onto this window.");
        refresh();
        return 0;
    case WM_SIZE: {
        int cw = LOWORD(lp), ch = HIWORD(lp);
        if (wp != SIZE_MINIMIZED && cw > 0 && ch > 0) {
            ensure_buffer(cw, ch);
            render_frame();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_GETMINMAXINFO:
        update_title_size(hwnd, (MINMAXINFO*)lp);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g.memdc && g.dib) BitBlt(dc, 0, 0, g.cw, g.ch, g.memdc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_ANIM) {
            if (!IsIconic(hwnd)) {
                g.ui.tick(0.033f);
                pump();
                render_frame();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else if (wp == TIMER_GAME) {
            bool r = minecraft_running();
            if (r != g.running) { g.running = r; g.ui.set_flags(g.state.layout.ok(), g.running, g.busy); }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (!g.tracking) {
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g.tracking = true;
        }
        g.ui.mouse_move(GET_X_LPARAM(lp) / g.scale, GET_Y_LPARAM(lp) / g.scale);
        pump();
        return 0;
    case WM_MOUSELEAVE:
        g.tracking = false;
        g.ui.mouse_leave();
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        g.ui.mouse_down(GET_X_LPARAM(lp) / g.scale, GET_Y_LPARAM(lp) / g.scale);
        pump();
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        g.ui.mouse_up(GET_X_LPARAM(lp) / g.scale, GET_Y_LPARAM(lp) / g.scale);
        process_events();
        return 0;
    case WM_MOUSEWHEEL:
        g.ui.wheel(GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_UP) g.ui.key(ui::Key::Up);
        else if (wp == VK_DOWN) g.ui.key(ui::Key::Down);
        else if (wp == VK_DELETE) g.ui.key(ui::Key::Delete);
        else if (wp == VK_F5) g.ui.key(ui::Key::Refresh);
        process_events();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(g.ui.hand() ? 32649 : 32512)));   // hand or arrow
            return TRUE;
        }
        break;
    case WM_DROPFILES: {
        HDROP d = (HDROP)wp;
        UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
        std::vector<fs::path> files;
        for (UINT i = 0; i < n; ++i) {
            UINT len = DragQueryFileW(d, i, nullptr, 0);
            std::wstring w((size_t)len + 1, L'\0');
            DragQueryFileW(d, i, &w[0], len + 1);
            w.resize(len);
            files.push_back(fs::path(w));
        }
        DragFinish(d);
        if (!files.empty()) { g.ui.say("Yum, new files!"); install_paths(files); }
        return 0;
    }
    case WM_APP_STATE: {
        std::unique_ptr<State> s((State*)lp);
        g.state = std::move(*s);
        g.running = minecraft_running();
        push_rows();
        if (!g.state.layout.ok()) {
            g.ui.set_status("Minecraft folder not found. Click \"Choose Minecraft folder\".");
            if (!g.asked_folder) {
                g.asked_folder = true;
                if (ask(L"Bedrock Deck could not find Minecraft's data folder automatically.\n\nDo you want to choose it now? (It is called \"com.mojang\".)", MB_ICONQUESTION))
                    set_dir_dialog();
            }
        }
        return 0;
    }
    case WM_APP_DONE: {
        std::unique_ptr<std::string> m((std::string*)lp);
        g.busy = false;
        g.ui.set_flags(g.state.layout.ok(), g.running, false);
        bool bad = m->compare(0, 5, "Error") == 0 || m->find("Could not") != std::string::npos || m->find("not found") != std::string::npos ||
                   m->find("Skipped") != std::string::npos || m->find("No Minecraft") != std::string::npos || m->find("refused") != std::string::npos;
        bool multi = m->find('\n') != std::string::npos;
        std::string first = m->substr(0, m->find('\n'));
        if (bad) g.ui.oops(); else g.ui.celebrate();
        g.ui.say(first);
        if (bad || multi) tell(widen(*m), bad ? MB_ICONWARNING : MB_ICONINFORMATION);
        pump();
        refresh();
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ANIM);
        KillTimer(hwnd, TIMER_GAME);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- entry point
inline int run(Settings st) {
    g.st = std::move(st);
    g.prefs = load_prefs(g.st);
    OleInitialize(nullptr);
    SetProcessDPIAware();
    HDC dc = GetDC(nullptr);
    double dpi = GetDeviceCaps(dc, LOGPIXELSX) / 96.0;
    ReleaseDC(nullptr, dc);
    g.scale = std::max(2, (int)std::lround(2.0 * dpi));

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"BedrockDeckWindow";
    if (!RegisterClassExW(&wc)) return 1;

    RECT r = {0, 0, 500 * g.scale, 340 * g.scale};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Bedrock Deck", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    OleUninitialize();
    return (int)m.wParam;
}

}  // namespace gui
}  // namespace deck

#endif  // _WIN32
