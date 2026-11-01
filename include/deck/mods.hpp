// mods.hpp - the mods folder: DLL files you chose to use, plus which ones are switched on.
#pragma once
#include <set>
#include "packs.hpp"

namespace deck {

struct Mod {
    std::string name;
    uint64_t size = 0;
    bool enabled = false;
};

inline fs::path mods_dir(const fs::path& data) { return data / "mods"; }

inline std::set<std::string> read_enabled(const fs::path& data) {
    std::set<std::string> s;
    try {
        std::string t = read_text(data / "enabled.txt");
        size_t pos = 0;
        while (pos < t.size()) {
            size_t nl = t.find('\n', pos);
            std::string line = t.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? t.size() : nl + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) s.insert(line);
        }
    } catch (...) {}
    return s;
}

inline void write_enabled(const fs::path& data, const std::set<std::string>& s) {
    std::string t;
    for (auto& n : s) t += n + "\n";
    fs::create_directories(data);
    write_file(data / "enabled.txt", std::vector<uint8_t>(t.begin(), t.end()));
}

inline bool is_dll_name(const std::string& n) { return safe_name(n) && ends_with(lower(n), ".dll"); }

inline std::vector<Mod> list_mods(const fs::path& data) {
    std::vector<Mod> out;
    auto enabled = read_enabled(data);
    std::error_code ec;
    for (fs::directory_iterator it(mods_dir(data), ec), end; !ec && it != end; it.increment(ec)) {
        std::string n = S(it->path().filename());
        if (!it->is_regular_file(ec) || !is_dll_name(n)) continue;
        out.push_back({n, (uint64_t)it->file_size(ec), enabled.count(n) > 0});
    }
    std::sort(out.begin(), out.end(), [](const Mod& a, const Mod& b) { return lower(a.name) < lower(b.name); });
    return out;
}

inline void add_mod(const fs::path& data, const std::string& name, const std::vector<uint8_t>& bytes) {
    if (!is_dll_name(name)) throw std::runtime_error("a mod must be a .dll file");
    if (bytes.size() < 64 || bytes[0] != 'M' || bytes[1] != 'Z') throw std::runtime_error("that file is not a Windows DLL");
    fs::create_directories(mods_dir(data));
    write_file(mods_dir(data) / P(name), bytes);
}

inline void set_mod_enabled(const fs::path& data, const std::string& name, bool on) {
    if (!is_dll_name(name)) throw std::runtime_error("invalid mod name");
    std::error_code ec;
    if (!fs::is_regular_file(mods_dir(data) / P(name), ec)) throw std::runtime_error("mod not found: " + name);
    auto s = read_enabled(data);
    if (on) s.insert(name); else s.erase(name);
    write_enabled(data, s);
}

inline void remove_mod(const fs::path& data, const std::string& name) {
    if (!is_dll_name(name)) throw std::runtime_error("invalid mod name");
    std::error_code ec;
    fs::remove(mods_dir(data) / P(name), ec);
    auto s = read_enabled(data);
    s.erase(name);
    write_enabled(data, s);
}

}  // namespace deck
