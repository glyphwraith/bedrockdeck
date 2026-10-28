// zip.hpp - ZIP reader (stored + deflate) and a simple ZIP writer. Minecraft packs and worlds are zip files.
#pragma once
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "inflate.hpp"

namespace deck {

inline uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
    static const std::vector<uint32_t> table = [] {
        std::vector<uint32_t> t(256);
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

struct ZipEntry {
    std::string name;
    uint16_t flags = 0, method = 0;
    uint32_t crc = 0, csize = 0, usize = 0, offset = 0;
    bool is_dir() const { return !name.empty() && name.back() == '/'; }
};

class ZipReader {
public:
    explicit ZipReader(std::vector<uint8_t> data) : d_(std::move(data)) { parse(); }
    const std::vector<ZipEntry>& entries() const { return entries_; }

    const ZipEntry* find(const std::string& name) const {
        for (auto& e : entries_) if (e.name == name) return &e;
        return nullptr;
    }

    std::vector<uint8_t> read(const ZipEntry& e) const {
        if (e.flags & 1) throw std::runtime_error("zip: encrypted entries are not supported");
        if (e.usize > 0x7FFFFFFFu) throw std::runtime_error("zip: entry too large");
        size_t loc = e.offset;
        if (loc + 30 > d_.size() || u32(loc) != 0x04034b50) throw std::runtime_error("zip: bad local header");
        size_t start = loc + 30 + u16(loc + 26) + u16(loc + 28);
        if (start + e.csize > d_.size()) throw std::runtime_error("zip: entry data out of range");
        std::vector<uint8_t> out;
        if (e.method == 0) {
            out.assign(d_.begin() + start, d_.begin() + start + e.csize);
        } else if (e.method == 8) {
            out = Inflater(d_.data() + start, e.csize, (size_t)e.usize + 16).run(e.usize);
        } else {
            throw std::runtime_error("zip: unsupported compression method " + std::to_string(e.method));
        }
        if (out.size() != e.usize) throw std::runtime_error("zip: size mismatch in " + e.name);
        if (crc32(out.data(), out.size()) != e.crc) throw std::runtime_error("zip: checksum mismatch in " + e.name);
        return out;
    }

private:
    uint16_t u16(size_t p) const { return (uint16_t)(d_[p] | (d_[p + 1] << 8)); }
    uint32_t u32(size_t p) const { return (uint32_t)u16(p) | ((uint32_t)u16(p + 2) << 16); }

    void parse() {
        if (d_.size() < 22) throw std::runtime_error("not a zip file");
        long long min = d_.size() > 65557 ? (long long)d_.size() - 65557 : 0;
        long long eocd = -1;
        for (long long i = (long long)d_.size() - 22; i >= min; --i)
            if (u32((size_t)i) == 0x06054b50) { eocd = i; break; }
        if (eocd < 0) throw std::runtime_error("not a zip file (no end-of-directory record)");
        size_t e = (size_t)eocd;
        size_t count = u16(e + 10), cdsize = u32(e + 12), cdoff = u32(e + 16);
        if (count == 0xFFFF || cdoff == 0xFFFFFFFFu) throw std::runtime_error("zip64 archives are not supported");
        if (cdoff + cdsize > d_.size()) throw std::runtime_error("zip: corrupt directory");
        size_t pos = cdoff;
        for (size_t i = 0; i < count; ++i) {
            if (pos + 46 > d_.size() || u32(pos) != 0x02014b50) throw std::runtime_error("zip: corrupt directory entry");
            ZipEntry z;
            z.flags = u16(pos + 8); z.method = u16(pos + 10);
            z.crc = u32(pos + 16); z.csize = u32(pos + 20); z.usize = u32(pos + 24);
            size_t nlen = u16(pos + 28), elen = u16(pos + 30), clen = u16(pos + 32);
            z.offset = u32(pos + 42);
            if (pos + 46 + nlen > d_.size()) throw std::runtime_error("zip: corrupt directory entry");
            z.name.assign((const char*)&d_[pos + 46], nlen);
            for (auto& c : z.name) if (c == '\\') c = '/';
            entries_.push_back(std::move(z));
            pos += 46 + nlen + elen + clen;
        }
    }

    std::vector<uint8_t> d_;
    std::vector<ZipEntry> entries_;
};

// Writes an uncompressed (stored) zip. Good enough for world backups and exports.
class ZipWriter {
public:
    explicit ZipWriter(const std::filesystem::path& path) : f_(path, std::ios::binary) {
        if (!f_) throw std::runtime_error("cannot create " + path.string());
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        time_ = (uint16_t)((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
        date_ = (uint16_t)(((tm.tm_year + 1900 - 1980) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
    }

    void add(const std::string& name, const std::vector<uint8_t>& data) {
        if (data.size() >= 0xFFFFFFFFull || recs_.size() >= 65534) throw std::runtime_error("zip: archive too large");
        Rec r{name, crc32(data.data(), data.size()), (uint32_t)data.size(), (uint32_t)pos_};
        std::vector<uint8_t> h;
        put32(h, 0x04034b50); put16(h, 20); put16(h, 0x0800); put16(h, 0);
        put16(h, time_); put16(h, date_);
        put32(h, r.crc); put32(h, r.size); put32(h, r.size);
        put16(h, (uint16_t)name.size()); put16(h, 0);
        h.insert(h.end(), name.begin(), name.end());
        write(h);
        write(data);
        recs_.push_back(r);
    }

    void finish() {
        size_t cd_start = pos_;
        for (auto& r : recs_) {
            std::vector<uint8_t> h;
            put32(h, 0x02014b50); put16(h, 20); put16(h, 20); put16(h, 0x0800); put16(h, 0);
            put16(h, time_); put16(h, date_);
            put32(h, r.crc); put32(h, r.size); put32(h, r.size);
            put16(h, (uint16_t)r.name.size()); put16(h, 0); put16(h, 0); put16(h, 0); put16(h, 0);
            put32(h, 0); put32(h, r.off);
            h.insert(h.end(), r.name.begin(), r.name.end());
            write(h);
        }
        size_t cd_size = pos_ - cd_start;
        std::vector<uint8_t> e;
        put32(e, 0x06054b50); put16(e, 0); put16(e, 0);
        put16(e, (uint16_t)recs_.size()); put16(e, (uint16_t)recs_.size());
        put32(e, (uint32_t)cd_size); put32(e, (uint32_t)cd_start); put16(e, 0);
        write(e);
        f_.close();
    }

private:
    struct Rec { std::string name; uint32_t crc, size, off; };
    static void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
    static void put32(std::vector<uint8_t>& v, uint32_t x) { put16(v, (uint16_t)x); put16(v, (uint16_t)(x >> 16)); }
    void write(const std::vector<uint8_t>& v) {
        f_.write((const char*)v.data(), (std::streamsize)v.size());
        pos_ += v.size();
        if (!f_) throw std::runtime_error("write failed (disk full?)");
    }
    std::ofstream f_;
    std::vector<Rec> recs_;
    size_t pos_ = 0;
    uint16_t time_ = 0, date_ = 0;
};

}  // namespace deck
