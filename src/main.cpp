// bedrockdeck - manage Minecraft Bedrock packs, skins, worlds and mods, with a built-in mod loader.
//   Windows:  double-click bedrockdeck.exe for the application window.
//   Any OS:   command line (bedrockdeck list / install / export / ...), handy for scripts.
#include <iostream>
#include <map>
#include <set>
#include "deck/app.hpp"
#ifdef _WIN32
#include "deck/gui_win.hpp"
#endif

using namespace deck;

#ifdef _WIN32
static bool g_console = false;
static void init_console() {   // the exe is a windowed app; reuse the terminal it was started from, if there is one
    if (GetConsoleWindow()) { g_console = true; return; }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        g_console = true;
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}
#endif

static void show_error(const std::string& m) {
#ifdef _WIN32
    if (!g_console) { MessageBoxW(nullptr, widen(m).c_str(), L"Bedrock Deck", MB_OK | MB_ICONERROR); return; }
#endif
    std::cerr << "error: " << m << "\n";
}

static void usage() {
    std::cout <<
        "bedrockdeck - packs, skins, worlds and mods for Minecraft Bedrock\n\n"
#ifdef _WIN32
        "  bedrockdeck                      open the application window\n"
#endif
        "  bedrockdeck list                 show installed packs, skins and worlds\n"
        "  bedrockdeck install <file>...    install .mcpack .mcaddon .mcworld .mctemplate\n"
        "  bedrockdeck remove <resource|behavior|skin|world> <folder>\n"
        "  bedrockdeck export <world-folder>  save a world as a .mcworld backup\n"
        "  bedrockdeck mods                 list mods;  mods add|on|off|remove <name or file>\n"
        "  bedrockdeck launch               start Minecraft (Windows)\n"
        "  bedrockdeck inject               load enabled mods into the running game (Windows)\n"
        "  bedrockdeck paths                show detected folders\n\n"
        "Options:  --dir <com.mojang folder>   --data <bedrockdeck folder>\n";
}

int main(int argc, char** argv) {
#ifdef _WIN32
    init_console();
#endif
    std::vector<std::string> pos;
    std::map<std::string, std::string> opt;
    std::set<std::string> flags;
    static const std::set<std::string> valued = {"--dir", "--data"};
    for (int k = 1; k < argc; ++k) {
        std::string a = argv[k];
        if (a.rfind("--", 0) != 0) { pos.push_back(a); continue; }
        if (valued.count(a) && k + 1 < argc) opt[a] = argv[++k];
        else flags.insert(a);
    }
    if (flags.count("--help") || (!pos.empty() && pos[0] == "help")) { usage(); return 0; }

    try {
        Settings st;
        st.data = opt.count("--data") ? P(opt["--data"]) : default_data_dir();
        st.dir_flag = opt.count("--dir") ? opt["--dir"] : "";

        if (pos.empty()) {
#ifdef _WIN32
            return gui::run(st);
#else
            usage();
            return 0;
#endif
        }
        std::string cmd = pos[0];

        if (cmd == "paths") {
            Layout L = current_layout(st);
            std::cout << "Minecraft data : " << (L.ok() ? S(L.com) : "(not found, use --dir)") << "\n";
            for (auto& w : L.world_roots) std::cout << "Worlds         : " << S(w) << "\n";
            std::cout << "bedrockdeck    : " << S(st.data) << "\n";
            return 0;
        }
        if (cmd == "mods") {
            std::string sub = pos.size() > 1 ? pos[1] : "";
            if (sub.empty()) {
                auto mods = list_mods(st.data);
                if (mods.empty()) std::cout << "No mods yet. Add one with: bedrockdeck mods add <file.dll>\n";
                for (auto& m : mods) std::cout << (m.enabled ? "[x] " : "[ ] ") << m.name << "  (" << fmt_size(m.size) << ")\n";
                return 0;
            }
            if (pos.size() < 3) { std::cerr << "usage: bedrockdeck mods add|on|off|remove <name>\n"; return 1; }
            if (sub == "add") {
                fs::path f = P(pos[2]);
                add_mod(st.data, S(f.filename()), read_file(f));
                std::cout << "Added " << S(f.filename()) << " (enable it with: bedrockdeck mods on " << S(f.filename()) << ")\n";
            } else if (sub == "on" || sub == "off") {
                set_mod_enabled(st.data, pos[2], sub == "on");
                std::cout << pos[2] << (sub == "on" ? " enabled\n" : " disabled\n");
            } else if (sub == "remove") {
                remove_mod(st.data, pos[2]);
                std::cout << "Removed " << pos[2] << "\n";
            } else { std::cerr << "unknown mods command\n"; return 1; }
            return 0;
        }
        if (cmd == "launch") { std::cout << launch_minecraft() << "\n"; return 0; }
        if (cmd == "inject") { for (auto& l : inject_enabled(st.data)) std::cout << l << "\n"; return 0; }

        Layout L = current_layout(st);
        if (!L.ok()) { std::cerr << "Could not find your Minecraft data folder. Pass it with --dir <path to com.mojang>\n"; return 1; }

        if (cmd == "list") {
            auto items = list_items(L);
            const char* titles[4][2] = {{"resource", "Resource packs"}, {"behavior", "Behavior packs"}, {"skin", "Skin packs"}, {"world", "Worlds"}};
            for (auto& t : titles) {
                std::cout << t[1] << ":\n";
                int n = 0;
                for (auto& i : items) if (i.kind == t[0]) {
                    std::cout << "  " << i.name << (i.version.empty() ? "" : "  v" + i.version) << "  [" << fmt_size(i.size) << "]  folder: " << i.folder << "\n";
                    ++n;
                }
                if (!n) std::cout << "  (none)\n";
            }
            return 0;
        }
        if (cmd == "install") {
            if (pos.size() < 2) { std::cerr << "usage: bedrockdeck install <file>...\n"; return 1; }
            int bad = 0;
            for (size_t k = 1; k < pos.size(); ++k) {
                fs::path f = P(pos[k]);
                try {
                    Report r = install_file(L, S(f.filename()), read_file(f));
                    for (auto& l : r.lines) std::cout << l << "\n";
                } catch (const std::exception& e) { std::cerr << pos[k] << ": " << e.what() << "\n"; ++bad; }
            }
            return bad ? 1 : 0;
        }
        if (cmd == "remove") {
            if (pos.size() < 3) { std::cerr << "usage: bedrockdeck remove <resource|behavior|skin|world> <folder>\n"; return 1; }
            std::cout << remove_item(L, st.data, pos[1], pos[2]) << "\n";
            return 0;
        }
        if (cmd == "export") {
            if (pos.size() < 2) { std::cerr << "usage: bedrockdeck export <world-folder>\n"; return 1; }
            std::cout << "Saved " << S(backup_world(L, st.data, pos[1])) << "\n";
            return 0;
        }
        std::cerr << "unknown command: " << cmd << "\n\n";
        usage();
        return 1;
    } catch (const std::exception& e) {
        show_error(e.what());
        return 1;
    }
}
