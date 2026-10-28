// gui_win.hpp - the native Windows application window (plain Win32 + common controls, no extra libraries).
//
//   [Launch Minecraft] [Inject enabled mods] [Add files...] [Refresh]            Minecraft: closed
//   +- Resource packs | Behavior packs | Skin packs | Worlds | Mods ----------------------------+
//   |  Name                         Version   Size     Folder                                   |
//   |  ...                                                                                      |
//   +-------------------------------------------------------------------------------------------+
//   [Remove selected] [Export world]
//   status bar
//
// Drag .mcpack / .mcaddon / .mcworld / .mctemplate / .dll files onto the window to install them.
#pragma once
#ifdef _WIN32

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "app.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
// modern look for buttons, tabs and lists
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace deck {
namespace gui {

enum : int {
    ID_LAUNCH = 101, ID_INJECT = 102, ID_ADD = 103, ID_REFRESH = 104, ID_REMOVE = 105, ID_EXPORT = 106,
    ID_TAB = 120, ID_LIST = 121, ID_STATUS = 122, ID_GAME = 123,
    CMD_ADD = 201, CMD_SETDIR = 202, CMD_OPENMC = 203, CMD_OPENBK = 204, CMD_EXIT = 205, CMD_ABOUT = 206
};
static const UINT WM_APP_STATE = WM_APP + 1;  // lParam: State*   (a fresh scan of the Minecraft folders)
static const UINT WM_APP_DONE = WM_APP + 2;   // lParam: std::string*  (result text of a background job)
static const UINT_PTR TIMER_GAME = 1;

static const wchar_t* kTabNames[5] = {L"Resource packs", L"Behavior packs", L"Skin packs", L"Worlds", L"Mods"};
static const char* kTabKinds[5] = {"resource", "behavior", "skin", "world", ""};
static const int kModsTab = 4;
static const int kWorldsTab = 3;

struct App {
    Settings st;
    HWND hwnd = nullptr, tab = nullptr, list = nullptr, status = nullptr, game = nullptr;
    HWND bLaunch = nullptr, bInject = nullptr, bAdd = nullptr, bRefresh = nullptr, bRemove = nullptr, bExport = nullptr;
    HFONT font = nullptr;
    double scale = 1.0;
    State state;
    std::vector<size_t> rows;  // list row -> index into state.items (pack tabs) or state.mods (mods tab)
    int cur = 0;
    bool populating = false;
    bool asked_folder = false;
    std::atomic<bool> busy{false};
};
inline App g;

inline int px(int v) { return (int)(v * g.scale + 0.5); }

inline void set_font(HWND h) { SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE); }

inline void set_status(const std::wstring& t) { SendMessageW(g.status, SB_SETTEXTW, 0, (LPARAM)t.c_str()); }

inline void open_folder(const fs::path& p) {
    ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- list handling
inline int selected_row() { return ListView_GetNextItem(g.list, -1, LVNI_SELECTED); }

inline void update_buttons() {
    bool sel = selected_row() >= 0 && !g.busy;
    EnableWindow(g.bRemove, sel);
    EnableWindow(g.bExport, sel && g.cur == kWorldsTab);
    ShowWindow(g.bExport, g.cur == kWorldsTab ? SW_SHOW : SW_HIDE);
}

inline void fit_columns() {
    RECT r;
    GetClientRect(g.list, &r);
    int w = r.right - GetSystemMetrics(SM_CXVSCROLL);
    if (w < 100) return;
    if (g.cur == kModsTab) {
        ListView_SetColumnWidth(g.list, 0, w * 70 / 100);
        ListView_SetColumnWidth(g.list, 1, w * 30 / 100);
    } else {
        ListView_SetColumnWidth(g.list, 0, w * 40 / 100);
        ListView_SetColumnWidth(g.list, 1, w * 12 / 100);
        ListView_SetColumnWidth(g.list, 2, w * 14 / 100);
        ListView_SetColumnWidth(g.list, 3, w * 34 / 100);
    }
}

inline void populate() {
    g.populating = true;
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g.list);
    while (SendMessageW(g.list, LVM_DELETECOLUMN, 0, 0)) {}
    bool mods = g.cur == kModsTab;
    ListView_SetExtendedListViewStyleEx(g.list, LVS_EX_CHECKBOXES, mods ? LVS_EX_CHECKBOXES : 0);

    auto col = [&](int i, const wchar_t* t) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)t;
        c.cx = px(100);
        SendMessageW(g.list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&c);
    };
    if (mods) { col(0, L"Mod (tick to enable)"); col(1, L"Size"); }
    else { col(0, L"Name"); col(1, L"Version"); col(2, L"Size"); col(3, L"Folder"); }

    auto add_row = [&](const std::vector<std::wstring>& cells, bool checked) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = ListView_GetItemCount(g.list);
        it.pszText = (LPWSTR)cells[0].c_str();
        int idx = (int)SendMessageW(g.list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        for (size_t c = 1; c < cells.size(); ++c) {
            LVITEMW sub{};
            sub.iSubItem = (int)c;
            sub.pszText = (LPWSTR)cells[c].c_str();
            SendMessageW(g.list, LVM_SETITEMTEXTW, (WPARAM)idx, (LPARAM)&sub);
        }
        if (mods) {
            LVITEMW stt{};
            stt.stateMask = LVIS_STATEIMAGEMASK;
            stt.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
            SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)idx, (LPARAM)&stt);
        }
    };

    g.rows.clear();
    if (mods) {
        for (size_t i = 0; i < g.state.mods.size(); ++i) {
            auto& m = g.state.mods[i];
            add_row({widen(m.name), widen(fmt_size(m.size))}, m.enabled);
            g.rows.push_back(i);
        }
    } else {
        for (size_t i = 0; i < g.state.items.size(); ++i) {
            auto& it = g.state.items[i];
            if (it.kind != kTabKinds[g.cur]) continue;
            add_row({widen(it.name), widen(it.version), widen(fmt_size(it.size)), widen(it.folder)}, false);
            g.rows.push_back(i);
        }
    }
    fit_columns();
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
    g.populating = false;

    if (!g.state.layout.ok()) set_status(L"Minecraft folder not found. Use File > Set Minecraft folder...");
    else if (g.rows.empty()) set_status(mods ? L"No mods yet. Drag a .dll onto this window, then tick it." : L"Nothing here yet. Drag packs or worlds onto this window to install them.");
    else set_status(L"Minecraft folder: " + g.state.layout.com.wstring());
    update_buttons();
}

inline void update_tab_titles() {
    for (int i = 0; i < 5; ++i) {
        size_t n = 0;
        if (i == kModsTab) n = g.state.mods.size();
        else for (auto& it : g.state.items) if (it.kind == kTabKinds[i]) ++n;
        std::wstring t = std::wstring(kTabNames[i]) + L" (" + std::to_wstring(n) + L")";
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)t.c_str();
        SendMessageW(g.tab, TCM_SETITEMW, (WPARAM)i, (LPARAM)&ti);
    }
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

inline void run_bg(const wchar_t* busy_text, bool popup, std::function<std::string()> job) {
    if (g.busy.exchange(true)) {
        MessageBoxW(g.hwnd, L"Please wait, another task is still running.", L"Bedrock Deck", MB_OK | MB_ICONINFORMATION);
        return;
    }
    set_status(busy_text);
    update_buttons();
    HWND h = g.hwnd;
    std::thread([h, popup, job]() {
        std::string msg;
        try { msg = job(); } catch (const std::exception& e) { msg = std::string("Error: ") + e.what(); }
        auto* p = new std::string(std::move(msg));
        if (!PostMessageW(h, WM_APP_DONE, popup ? 1 : 0, (LPARAM)p)) delete p;
    }).detach();
}

inline void install_paths(const std::vector<fs::path>& files) {
    Settings st = g.st;
    run_bg(L"Installing...", false, [st, files]() -> std::string {
        Layout L = current_layout(st);
        std::vector<std::string> out;
        for (auto& f : files) {
            std::string name = S(f.filename());
            try {
                if (ends_with(lower(name), ".dll")) {
                    add_mod(st.data, name, read_file(f));
                    out.push_back("Added mod " + name + " (tick it in the Mods tab to enable)");
                } else if (!L.ok()) {
                    out.push_back("Minecraft folder not found. Use File > Set Minecraft folder first.");
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

// ---------------------------------------------------------------- actions
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
    if (*p == L'\0') files.push_back(fs::path(first));              // a single file was chosen
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

inline void do_remove() {
    int sel = selected_row();
    if (sel < 0 || sel >= (int)g.rows.size()) return;
    Settings st = g.st;
    if (g.cur == kModsTab) {
        std::string name = g.state.mods[g.rows[(size_t)sel]].name;
        std::wstring q = L"Remove the mod \"" + widen(name) + L"\"?";
        if (MessageBoxW(g.hwnd, q.c_str(), L"Bedrock Deck", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        run_bg(L"Removing...", false, [st, name]() { remove_mod(st.data, name); return "Removed mod " + name; });
    } else {
        Item it = g.state.items[g.rows[(size_t)sel]];
        std::wstring q = L"Remove \"" + widen(it.name) + L"\"?";
        if (it.kind == "world") q += L"\n\nA backup copy (.mcworld) is saved first.";
        if (MessageBoxW(g.hwnd, q.c_str(), L"Bedrock Deck", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        run_bg(L"Removing...", true, [st, it]() { return remove_item(current_layout(st), st.data, it.kind, it.folder); });
    }
}

inline void do_export() {
    int sel = selected_row();
    if (sel < 0 || sel >= (int)g.rows.size() || g.cur != kWorldsTab) return;
    Settings st = g.st;
    std::string folder = g.state.items[g.rows[(size_t)sel]].folder;
    run_bg(L"Exporting world (this can take a while for big worlds)...", true, [st, folder]() {
        return "Exported to " + S(backup_world(current_layout(st), st.data, folder));
    });
}

inline void do_inject() {
    std::vector<std::string> enabled;
    for (auto& m : g.state.mods) if (m.enabled) enabled.push_back(m.name);
    if (enabled.empty()) {
        MessageBoxW(g.hwnd, L"No mods are switched on.\n\nOpen the Mods tab and tick the ones you want.", L"Bedrock Deck", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!minecraft_running()) {
        MessageBoxW(g.hwnd, L"Minecraft is not running.\n\nClick \"Launch Minecraft\", wait for the title screen, then inject.", L"Bedrock Deck", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring q = L"These mods will be loaded into Minecraft:\n\n";
    for (auto& n : enabled) q += L"  \u2022 " + widen(n) + L"\n";
    q += L"\nOnly continue if you trust them. Some servers ban modified clients.";
    if (MessageBoxW(g.hwnd, q.c_str(), L"Bedrock Deck", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    fs::path data = g.st.data;
    run_bg(L"Loading mods into Minecraft...", true, [data]() { return join_lines(inject_enabled(data)); });
}

// ---------------------------------------------------------------- layout
inline void do_layout() {
    if (!g.status) return;
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    SendMessageW(g.status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g.status, &sr);
    int sh = sr.bottom - sr.top;
    int pad = px(10), bh = px(32), top = pad;

    int x = pad;
    auto place = [&](HWND h, int w) { MoveWindow(h, x, top, w, bh, TRUE); x += w + px(8); };
    place(g.bLaunch, px(150));
    place(g.bInject, px(190));
    place(g.bAdd, px(120));
    place(g.bRefresh, px(90));
    MoveWindow(g.game, W - pad - px(200), top + px(7), px(200), bh - px(7), TRUE);

    int by = H - sh - pad - bh;
    MoveWindow(g.bRemove, pad, by, px(150), bh, TRUE);
    MoveWindow(g.bExport, pad + px(158), by, px(150), bh, TRUE);

    int ty = top + bh + pad;
    int th = by - pad - ty;
    if (th < px(80)) th = px(80);
    MoveWindow(g.tab, pad, ty, W - 2 * pad, th, TRUE);
    RECT tr = {0, 0, W - 2 * pad, th};
    TabCtrl_AdjustRect(g.tab, FALSE, &tr);
    MoveWindow(g.list, pad + tr.left, ty + tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    fit_columns();
}

// ---------------------------------------------------------------- window procedure
inline LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        HINSTANCE inst = GetModuleHandleW(nullptr);
        auto button = [&](const wchar_t* text, int id) {
            HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)id, inst, nullptr);
            set_font(h);
            return h;
        };
        g.bLaunch = button(L"Launch Minecraft", ID_LAUNCH);
        g.bInject = button(L"Inject enabled mods", ID_INJECT);
        g.bAdd = button(L"Add files...", ID_ADD);
        g.bRefresh = button(L"Refresh", ID_REFRESH);
        g.bRemove = button(L"Remove selected", ID_REMOVE);
        g.bExport = button(L"Export world...", ID_EXPORT);
        g.game = CreateWindowExW(0, L"STATIC", L"Minecraft: ?", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_GAME, inst, nullptr);
        set_font(g.game);

        g.tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TCS_TABS, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_TAB, inst, nullptr);
        set_font(g.tab);
        for (int i = 0; i < 5; ++i) {
            TCITEMW ti{};
            ti.mask = TCIF_TEXT;
            ti.pszText = (LPWSTR)kTabNames[i];
            SendMessageW(g.tab, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&ti);
        }
        g.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LIST, inst, nullptr);
        set_font(g.list);
        ListView_SetExtendedListViewStyleEx(g.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        g.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"Loading...", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_STATUS, inst, nullptr);
        set_font(g.status);

        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, TIMER_GAME, 2500, nullptr);
        SendMessageW(hwnd, WM_TIMER, TIMER_GAME, 0);
        do_layout();
        populate();
        refresh();
        return 0;
    }
    case WM_SIZE:
        do_layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* m = (MINMAXINFO*)lp;
        m->ptMinTrackSize.x = px(780);
        m->ptMinTrackSize.y = px(480);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_GAME) SetWindowTextW(g.game, minecraft_running() ? L"Minecraft: running" : L"Minecraft: closed");
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_LAUNCH: set_status(widen(launch_minecraft())); break;
        case ID_INJECT: do_inject(); break;
        case ID_ADD:
        case CMD_ADD: add_dialog(); break;
        case ID_REFRESH: refresh(); break;
        case ID_REMOVE: do_remove(); break;
        case ID_EXPORT: do_export(); break;
        case CMD_SETDIR: set_dir_dialog(); break;
        case CMD_OPENMC: if (g.state.layout.ok()) open_folder(g.state.layout.com); break;
        case CMD_OPENBK: {
            std::error_code ec;
            fs::create_directories(g.st.data / "backups", ec);
            open_folder(g.st.data / "backups");
            break;
        }
        case CMD_EXIT: DestroyWindow(hwnd); break;
        case CMD_ABOUT:
            MessageBoxW(hwnd, L"Bedrock Deck\n\nManage Minecraft Bedrock packs, skins, worlds and mods.\nDrag files onto the window to install them.", L"About Bedrock Deck", MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;
    case WM_NOTIFY: {
        auto* nm = (NMHDR*)lp;
        if (nm->hwndFrom == g.tab && nm->code == TCN_SELCHANGE) {
            g.cur = TabCtrl_GetCurSel(g.tab);
            populate();
        } else if (nm->hwndFrom == g.list && nm->code == LVN_ITEMCHANGED && !g.populating) {
            auto* v = (NMLISTVIEW*)lp;
            if (g.cur == kModsTab && (v->uChanged & LVIF_STATE) && ((v->uNewState ^ v->uOldState) & LVIS_STATEIMAGEMASK) &&
                v->iItem >= 0 && v->iItem < (int)g.rows.size()) {
                bool on = ((v->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
                size_t idx = g.rows[(size_t)v->iItem];
                try {
                    set_mod_enabled(g.st.data, g.state.mods[idx].name, on);
                    g.state.mods[idx].enabled = on;
                    set_status(widen(g.state.mods[idx].name) + (on ? L" enabled" : L" disabled"));
                } catch (const std::exception& e) {
                    set_status(L"Could not change mod: " + widen(e.what()));
                }
            }
            if (v->uChanged & LVIF_STATE) update_buttons();
        }
        return 0;
    }
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
        if (!files.empty()) install_paths(files);
        return 0;
    }
    case WM_APP_STATE: {
        std::unique_ptr<State> s((State*)lp);
        g.state = std::move(*s);
        update_tab_titles();
        populate();
        if (!g.state.layout.ok() && !g.asked_folder) {
            g.asked_folder = true;
            if (MessageBoxW(hwnd, L"Bedrock Deck could not find Minecraft's data folder automatically.\n\nDo you want to choose it now? (It is called \"com.mojang\".)",
                            L"Bedrock Deck", MB_YESNO | MB_ICONQUESTION) == IDYES)
                set_dir_dialog();
        }
        return 0;
    }
    case WM_APP_DONE: {
        std::unique_ptr<std::string> m((std::string*)lp);
        g.busy = false;
        bool bad = m->compare(0, 5, "Error") == 0 || m->find("Could not") != std::string::npos || m->find("not found") != std::string::npos ||
                   m->find("Skipped") != std::string::npos || m->find("No Minecraft") != std::string::npos;
        bool multi = m->find('\n') != std::string::npos;
        std::wstring text = widen(*m);
        if (wp == 1 || bad || multi) MessageBoxW(hwnd, text.c_str(), L"Bedrock Deck", MB_OK | (bad ? MB_ICONWARNING : MB_ICONINFORMATION));
        else set_status(text);
        update_buttons();
        refresh();
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_GAME);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- entry point
inline int run(Settings st) {
    g.st = std::move(st);
    OleInitialize(nullptr);
    SetProcessDPIAware();
    HDC dc = GetDC(nullptr);
    g.scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0;
    ReleaseDC(nullptr, dc);

    INITCOMMONCONTROLSEX ic{sizeof ic, ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&ic);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))  /* IDC_ARROW */;
    wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512))  /* IDI_APPLICATION */;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"BedrockDeckWindow";
    if (!RegisterClassExW(&wc)) return 1;

    HMENU bar = CreateMenu(), file = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, CMD_ADD, L"&Add files...");
    AppendMenuW(file, MF_STRING, CMD_SETDIR, L"Set &Minecraft folder...");
    AppendMenuW(file, MF_STRING, CMD_OPENMC, L"&Open Minecraft folder");
    AppendMenuW(file, MF_STRING, CMD_OPENBK, L"Open &backups folder");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, CMD_EXIT, L"E&xit");
    AppendMenuW(help, MF_STRING, CMD_ABOUT, L"&About");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Bedrock Deck", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, px(1000), px(680),
                                nullptr, bar, inst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    OleUninitialize();
    return (int)m.wParam;
}

}  // namespace gui
}  // namespace deck

#endif  // _WIN32
