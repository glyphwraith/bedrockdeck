// app.hpp - settings and state shared by the command line and the window.
#pragma once
#include <cstdio>
#include "inject_win.hpp"
#include "mods.hpp"
#include "packs.hpp"

namespace deck {

struct Settings {
    fs::path data;         // bedrockdeck's own folder (mods, backups, saved settings)
    std::string dir_flag;  // --dir override
};

inline fs::path default_data_dir() {
    std::string appdata = env("APPDATA"), home = env("HOME");
    if (!appdata.empty()) return P(appdata) / "bedrockdeck";
    if (!home.empty()) return P(home) / ".bedrockdeck";
    return fs::current_path() / "bedrockdeck-data";
}

inline Layout current_layout(const Settings& st) {
    fs::path com;
    if (!st.dir_flag.empty()) com = P(st.dir_flag);
    else {
        try {
            std::string t = read_text(st.data / "dir.txt");
            while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back();
            if (!t.empty()) com = P(t);
        } catch (...) {}
    }
    std::error_code ec;
    if (com.empty() || !fs::is_directory(com, ec)) com = detect_com_mojang();
    return com.empty() ? Layout{} : make_layout(com);
}

inline void save_dir(const Settings& st, const fs::path& dir) {
    fs::create_directories(st.data);
    std::string d = S(dir);
    write_file(st.data / "dir.txt", std::vector<uint8_t>(d.begin(), d.end()));
}

struct State {
    Layout layout;
    std::vector<Item> items;
    std::vector<Mod> mods;
};

inline State load_state(const Settings& st) {
    State s;
    s.layout = current_layout(st);
    if (s.layout.ok()) s.items = list_items(s.layout);
    s.mods = list_mods(st.data);
    return s;
}

inline std::string join_lines(const std::vector<std::string>& v) {
    std::string s;
    for (auto& l : v) s += (s.empty() ? "" : "\n") + l;
    return s;
}

inline std::string fmt_size(uint64_t b) {
    char buf[32];
    if (b < 1024) std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
    else if (b < 1048576) std::snprintf(buf, sizeof buf, "%.0f KB", (double)b / 1024);
    else if (b < 1073741824ull) std::snprintf(buf, sizeof buf, "%.1f MB", (double)b / 1048576);
    else std::snprintf(buf, sizeof buf, "%.2f GB", (double)b / 1073741824);
    return buf;
}

}  // namespace deck
