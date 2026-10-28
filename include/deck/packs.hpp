// packs.hpp - find the Minecraft data folders; list, install, remove and back up packs, skins and worlds.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include "json.hpp"
#include "zip.hpp"

namespace deck {

namespace fs = std::filesystem;

inline fs::path P(const std::string& s) { return fs::u8path(s); }
inline std::string S(const fs::path& p) { return p.u8string(); }
inline std::string env(const char* k) { const char* v = std::getenv(k); return v ? v : ""; }

inline std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline bool ends_with(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
inline bool starts_with(const std::string& s, const std::string& pre) { return s.compare(0, pre.size(), pre) == 0; }

inline std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + S(p));
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
inline std::string read_text(const fs::path& p) {
    auto v = read_file(p);
    return std::string(v.begin(), v.end());
}
inline void write_file(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + S(p));
    f.write((const char*)data.data(), (std::streamsize)data.size());
    if (!f) throw std::runtime_error("write failed (disk full?): " + S(p));
}

// A folder name taken from user/file input must be a single, harmless path component.
inline bool safe_name(const std::string& n) {
    if (n.empty() || n == "." || n == ".." || n.size() > 200) return false;
    for (char c : n) if (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 32) return false;
    return true;
}
// A relative path inside an archive must not escape the destination ("zip slip").
inline bool safe_rel(const std::string& rel) {
    if (rel.empty() || rel[0] == '/' ) return false;
    size_t pos = 0;
    while (pos <= rel.size()) {
        size_t sl = rel.find('/', pos);
        std::string part = rel.substr(pos, sl == std::string::npos ? std::string::npos : sl - pos);
        if (part == ".." || part.find(':') != std::string::npos) return false;
        if (sl == std::string::npos) break;
        pos = sl + 1;
    }
    return true;
}

inline std::string strip_codes(const std::string& s) {
    std::string o;
    for (size_t k = 0; k < s.size(); ++k) {
        if ((unsigned char)s[k] == 0xC2 && k + 2 < s.size() + 0 && (unsigned char)s[k + 1] == 0xA7) { k += 2; continue; }
        o += s[k];
    }
    return o;
}

inline std::string sanitize(std::string s) {
    s = strip_codes(s);
    for (auto& c : s) if ((unsigned char)c < 32 || std::strchr("<>:\"/\\|?*", c)) c = '_';
    size_t st = 0;
    while (st < s.size() && s[st] == ' ') ++st;
    s = s.substr(st);
    if (s.size() > 64) {
        s.resize(64);
        while (!s.empty() && (((unsigned char)s.back() & 0xC0) == 0x80 || (unsigned char)s.back() >= 0xC0)) s.pop_back();
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    if (s.empty()) s = "pack";
    std::string u = s;
    for (auto& c : u) c = (char)std::toupper((unsigned char)c);
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "LPT1", "LPT2", "LPT3"};
    for (auto r : reserved) if (u == r) s += "_";
    return s;
}

inline fs::path unique_dir(const fs::path& base, const std::string& name) {
    std::error_code ec;
    if (!fs::exists(base / P(name), ec)) return base / P(name);
    for (int i = 2; i < 1000; ++i) {
        fs::path p = base / P(name + " (" + std::to_string(i) + ")");
        if (!fs::exists(p, ec)) return p;
    }
    throw std::runtime_error("too many copies of " + name);
}

inline uint64_t dir_size(const fs::path& p) {
    uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += it->file_size(e2);
    }
    return total;
}

// ---------------------------------------------------------------- locating Minecraft
struct Layout {
    fs::path com;                      // .../com.mojang (packs live here)
    std::vector<fs::path> world_roots; // folders that contain worlds (newest first)
    bool ok() const { return !com.empty(); }
    fs::path worlds_dir() const { return world_roots.empty() ? com / "minecraftWorlds" : world_roots.front(); }
};

inline fs::path detect_com_mojang() {
    std::vector<fs::path> c;
    std::string appdata = env("APPDATA"), local = env("LOCALAPPDATA");
    if (!appdata.empty()) {
        c.push_back(P(appdata) / "Minecraft Bedrock" / "Users" / "Shared" / "games" / "com.mojang");
        c.push_back(P(appdata) / "Minecraft Bedrock Preview" / "Users" / "Shared" / "games" / "com.mojang");
    }
    if (!local.empty()) {
        c.push_back(P(local) / "Packages" / "Microsoft.MinecraftUWP_8wekyb3d8bbwe" / "LocalState" / "games" / "com.mojang");
        c.push_back(P(local) / "Packages" / "Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe" / "LocalState" / "games" / "com.mojang");
    }
    std::error_code ec;
    for (auto& p : c) if (fs::is_directory(p, ec)) return p;
    return {};
}

inline Layout make_layout(const fs::path& com) {
    Layout L;
    L.com = com;
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> found;
    auto consider = [&](const fs::path& w) {
        if (fs::is_directory(w, ec)) found.push_back({fs::last_write_time(w, ec), w});
    };
    // Newer (GDK) builds keep worlds per user: Users/<id>/games/com.mojang/minecraftWorlds
    fs::path games = com.parent_path();
    if (games.filename() == "games" && games.parent_path().filename() == "Shared") {
        fs::path users = games.parent_path().parent_path();
        for (fs::directory_iterator it(users, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec)) consider(it->path() / "games" / "com.mojang" / "minecraftWorlds");
    }
    consider(com / "minecraftWorlds");
    std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& f : found) L.world_roots.push_back(f.second);
    return L;
}

// ---------------------------------------------------------------- manifests
struct Manifest {
    bool ok = false;
    std::string name, desc, uuid, version, kind;  // kind: resource | behavior | skin | template | ""
};

inline std::string lang_lookup(const std::string& lang, const std::string& key) {
    size_t pos = 0;
    std::string pre = key + "=";
    while (pos < lang.size()) {
        size_t nl = lang.find('\n', pos);
        std::string line = lang.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? lang.size() : nl + 1;
        if (starts_with(line, pre)) {
            std::string v = line.substr(pre.size());
            size_t h = v.find("\t#");
            if (h != std::string::npos) v = v.substr(0, h);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ' || v.back() == '\t')) v.pop_back();
            return v;
        }
    }
    return "";
}

inline Manifest parse_manifest(const std::string& text, const std::string& lang) {
    Manifest m;
    try {
        Json j = parse_json(text);
        const Json* h = j.get("header");
        if (!h) return m;
        auto resolve = [&](const std::string& v) {
            if (!lang.empty() && v.find(' ') == std::string::npos && v.find('.') != std::string::npos) {
                std::string r = lang_lookup(lang, v);
                if (!r.empty()) return r;
            }
            return v;
        };
        if (auto n = h->get("name")) m.name = resolve(n->str());
        if (auto d = h->get("description")) m.desc = resolve(d->str());
        if (auto u = h->get("uuid")) m.uuid = lower(u->str());
        if (auto v = h->get("version"); v && v->t == Json::T::Arr) {
            for (auto& x : v->a) m.version += (m.version.empty() ? "" : ".") + std::to_string((long long)x.n);
        }
        if (m.uuid.empty()) return m;
        std::set<std::string> types;
        if (auto mods = j.get("modules"); mods && mods->t == Json::T::Arr)
            for (auto& x : mods->a) if (auto t = x.get("type")) types.insert(lower(t->str()));
        if (types.count("skin_pack")) m.kind = "skin";
        else if (types.count("world_template")) m.kind = "template";
        else if (types.count("resources")) m.kind = "resource";
        else if (types.count("data") || types.count("script") || types.count("client_data")) m.kind = "behavior";
        m.ok = true;
    } catch (...) {}
    return m;
}

inline Manifest read_manifest_dir(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_regular_file(dir / "manifest.json", ec)) return {};
    std::string lang;
    try { lang = read_text(dir / "texts" / "en_US.lang"); } catch (...) {}
    try { return parse_manifest(read_text(dir / "manifest.json"), lang); } catch (...) { return {}; }
}

// ---------------------------------------------------------------- listing
struct Item {
    std::string kind, folder, name, version, uuid, desc;
    uint64_t size = 0;
};

inline std::vector<Item> list_items(const Layout& L) {
    std::vector<Item> out;
    std::error_code ec;
    struct Src { const char* kind; const char* dir; };
    for (Src s : {Src{"resource", "resource_packs"}, Src{"behavior", "behavior_packs"}, Src{"skin", "skin_packs"}}) {
        fs::path base = L.com / s.dir;
        for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            Manifest m = read_manifest_dir(it->path());
            Item i;
            i.kind = s.kind;
            i.folder = S(it->path().filename());
            i.name = m.ok && !m.name.empty() ? strip_codes(m.name) : i.folder;
            i.version = m.version;
            i.uuid = m.uuid;
            i.desc = strip_codes(m.desc);
            i.size = dir_size(it->path());
            out.push_back(std::move(i));
        }
        ec.clear();
    }
    for (auto& root : L.world_roots) {
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec) || !fs::exists(it->path() / "level.dat", ec)) continue;
            Item i;
            i.kind = "world";
            i.folder = S(it->path().filename());
            try { i.name = strip_codes(read_text(it->path() / "levelname.txt")); } catch (...) {}
            while (!i.name.empty() && (i.name.back() == '\n' || i.name.back() == '\r')) i.name.pop_back();
            if (i.name.empty()) i.name = i.folder;
            i.size = dir_size(it->path());
            out.push_back(std::move(i));
        }
        ec.clear();
    }
    return out;
}

// ---------------------------------------------------------------- installing
struct Report {
    std::vector<std::string> lines;
    int installed = 0;
    void say(const std::string& s) { lines.push_back(s); }
};

// Extracts into a temporary folder first and swaps it in only if everything succeeded,
// so a bad archive never leaves a half-installed pack (and never touches paths outside the destination).
inline void extract_prefix(const ZipReader& z, const std::string& prefix, const fs::path& dest) {
    for (auto& e : z.entries()) {
        if (!starts_with(e.name, prefix) || starts_with(e.name, "__MACOSX/")) continue;
        std::string rel = e.name.substr(prefix.size());
        if (!rel.empty() && !safe_rel(rel)) throw std::runtime_error("unsafe path in archive: " + e.name);
    }
    fs::path tmp = dest;
    tmp += ".part";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    try {
        fs::create_directories(tmp);
        for (auto& e : z.entries()) {
            if (!starts_with(e.name, prefix) || starts_with(e.name, "__MACOSX/")) continue;
            std::string rel = e.name.substr(prefix.size());
            if (rel.empty()) continue;
            fs::path target = tmp / P(rel);
            if (e.is_dir()) { fs::create_directories(target); continue; }
            fs::create_directories(target.parent_path());
            write_file(target, z.read(e));
        }
        fs::remove_all(dest, ec);
        fs::rename(tmp, dest);
    } catch (...) {
        fs::remove_all(tmp, ec);
        throw;
    }
}

// Shallowest, non-nested directories from a list of directory prefixes.
inline std::vector<std::string> top_roots(std::vector<std::string> dirs) {
    std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
    std::vector<std::string> acc;
    for (auto& d : dirs) {
        bool nested = false;
        for (auto& a : acc) if (starts_with(d, a)) { nested = true; break; }
        if (!nested) acc.push_back(d);
    }
    return acc;
}

inline std::string dir_of(const std::string& name, const std::string& file) {
    return name.substr(0, name.size() - file.size());
}

inline void install_archive(const Layout& L, const std::string& filename, std::vector<uint8_t> data, Report& R, int depth = 0) {
    ZipReader z(std::move(data));
    std::string stem = filename.substr(0, filename.find_last_of('.') == std::string::npos ? filename.size() : filename.find_last_of('.'));

    auto lang_for = [&](const std::string& root) {
        if (auto e = z.find(root + "texts/en_US.lang")) { try { auto b = z.read(*e); return std::string(b.begin(), b.end()); } catch (...) {} }
        return std::string();
    };

    // 1. manifests
    struct Found { std::string root; Manifest m; };
    std::vector<Found> manifests;
    std::vector<std::string> mdirs, ldirs;
    for (auto& e : z.entries()) {
        if (e.name == "manifest.json" || ends_with(e.name, "/manifest.json")) mdirs.push_back(dir_of(e.name, "manifest.json"));
        if (e.name == "level.dat" || ends_with(e.name, "/level.dat")) ldirs.push_back(dir_of(e.name, "level.dat"));
    }
    for (auto& d : mdirs) {
        auto b = z.read(*z.find(d + "manifest.json"));
        Manifest m = parse_manifest(std::string(b.begin(), b.end()), lang_for(d));
        if (m.ok) manifests.push_back({d, m});
        else R.say("Skipped an unreadable manifest in " + filename + (d.empty() ? "" : " (" + d + ")"));
    }

    // 2. world templates
    bool did_template = false;
    for (auto& f : manifests) {
        if (f.m.kind != "template") continue;
        fs::path dest = unique_dir(L.com / "world_templates", sanitize(f.m.name));
        extract_prefix(z, f.root, dest);
        R.say("Installed world template: " + (f.m.name.empty() ? stem : f.m.name));
        R.installed++;
        did_template = true;
    }
    if (did_template) return;

    // 3. worlds
    if (!ldirs.empty()) {
        for (auto& root : top_roots(ldirs)) {
            std::string name = stem;
            if (auto e = z.find(root + "levelname.txt")) {
                auto b = z.read(*e);
                std::string t(b.begin(), b.end());
                while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
                if (!t.empty()) name = t;
            }
            fs::path dest = unique_dir(L.worlds_dir(), sanitize(name));  // never overwrites an existing world
            extract_prefix(z, root, dest);
            R.say("Installed world: " + strip_codes(name));
            R.installed++;
        }
        return;
    }

    // 4. packs
    std::vector<std::string> pack_dirs;
    for (auto& f : manifests) pack_dirs.push_back(f.root);
    if (!pack_dirs.empty()) {
        for (auto& root : top_roots(pack_dirs)) {
            const Manifest* m = nullptr;
            for (auto& f : manifests) if (f.root == root) m = &f.m;
            if (!m || m->kind.empty()) { R.say("Skipped a pack with an unknown type in " + filename); continue; }
            const char* sub = m->kind == "resource" ? "resource_packs" : m->kind == "behavior" ? "behavior_packs" : "skin_packs";
            fs::path base = L.com / sub;
            std::error_code ec;
            fs::create_directories(base, ec);
            fs::path dest;
            for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec))   // same pack already installed? update it
                if (it->is_directory(ec) && read_manifest_dir(it->path()).uuid == m->uuid) { dest = it->path(); break; }
            bool update = !dest.empty();
            if (!update) dest = unique_dir(base, sanitize(m->name.empty() ? stem : m->name));
            extract_prefix(z, root, dest);
            R.say(std::string(update ? "Updated " : "Installed ") + m->kind + " pack: " + strip_codes(m->name.empty() ? stem : m->name) +
                  (m->version.empty() ? "" : " v" + m->version));
            R.installed++;
        }
        return;
    }

    // 5. archives inside archives (.mcaddon bundles)
    int nested = 0;
    if (depth < 2) {
        for (auto& e : z.entries()) {
            std::string n = lower(e.name);
            if (e.is_dir() || !(ends_with(n, ".mcpack") || ends_with(n, ".mcworld") || ends_with(n, ".mctemplate") || ends_with(n, ".zip"))) continue;
            std::string base = e.name.substr(e.name.find_last_of('/') == std::string::npos ? 0 : e.name.find_last_of('/') + 1);
            install_archive(L, base, z.read(e), R, depth + 1);
            nested++;
        }
    }
    if (!nested) R.say("No Minecraft packs or worlds found inside " + filename);
}

inline Report install_file(const Layout& L, const std::string& filename, std::vector<uint8_t> data) {
    Report R;
    try {
        install_archive(L, filename, std::move(data), R);
    } catch (const std::exception& e) {
        R.say("Could not install " + filename + ": " + e.what());
    }
    return R;
}

// ---------------------------------------------------------------- backup / remove
inline fs::path find_world(const Layout& L, const std::string& folder) {
    if (!safe_name(folder)) throw std::runtime_error("invalid folder name");
    std::error_code ec;
    for (auto& r : L.world_roots) if (fs::is_directory(r / P(folder), ec)) return r / P(folder);
    throw std::runtime_error("world not found: " + folder);
}

inline std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm);
    return b;
}

// Exports a world as a .mcworld file (a zip that Minecraft can re-import).
inline fs::path backup_world(const Layout& L, const fs::path& data_dir, const std::string& folder) {
    fs::path w = find_world(L, folder);
    std::string name = folder;
    try { std::string t = read_text(w / "levelname.txt"); while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back(); if (!t.empty()) name = t; } catch (...) {}
    fs::create_directories(data_dir / "backups");
    fs::path out = data_dir / "backups" / P(sanitize(name) + "-" + timestamp() + ".mcworld");
    ZipWriter zw(out);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(w, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        std::string rel = S(fs::relative(it->path(), w, e2));
        for (auto& c : rel) if (c == '\\') c = '/';
        zw.add(rel, read_file(it->path()));
    }
    zw.finish();
    return out;
}

inline std::string remove_item(const Layout& L, const fs::path& data_dir, const std::string& kind, const std::string& folder) {
    std::error_code ec;
    if (kind == "world") {
        fs::path backup = backup_world(L, data_dir, folder);   // always keep a copy before deleting a world
        fs::remove_all(find_world(L, folder), ec);
        return "Deleted world (backup saved to " + S(backup) + ")";
    }
    const char* sub = kind == "resource" ? "resource_packs" : kind == "behavior" ? "behavior_packs" : kind == "skin" ? "skin_packs" : nullptr;
    if (!sub || !safe_name(folder)) throw std::runtime_error("invalid request");
    fs::path p = L.com / sub / P(folder);
    if (!fs::is_directory(p, ec)) throw std::runtime_error("not found: " + folder);
    fs::remove_all(p, ec);
    if (ec) throw std::runtime_error("could not delete (is Minecraft running?): " + ec.message());
    return "Removed " + folder;
}

}  // namespace deck
