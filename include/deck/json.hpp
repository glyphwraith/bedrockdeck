// json.hpp - tolerant JSON parser (accepts comments, trailing commas and a BOM, like Minecraft's own parser).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace deck {

struct Json {
    enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;            // array items
    std::vector<std::string> keys;  // object keys
    std::vector<Json> vals;         // object values

    const Json* get(const std::string& k) const {
        if (t != T::Obj) return nullptr;
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == k) return &vals[i];
        return nullptr;
    }
    std::string str(const std::string& def = "") const { return t == T::Str ? s : def; }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {
        if (s_.compare(0, 3, "\xEF\xBB\xBF") == 0) i_ = 3;
    }
    Json parse() {
        Json v = value(0);
        ws();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const char* m) const { throw std::runtime_error(std::string("JSON: ") + m + " at offset " + std::to_string(i_)); }

    void ws() {
        for (;;) {
            while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
            if (s_.compare(i_, 2, "//") == 0) { while (i_ < s_.size() && s_[i_] != '\n') ++i_; }
            else if (s_.compare(i_, 2, "/*") == 0) {
                size_t e = s_.find("*/", i_ + 2);
                if (e == std::string::npos) fail("unterminated comment");
                i_ = e + 2;
            } else return;
        }
    }

    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }

    unsigned hex4() {
        if (i_ + 4 > s_.size()) fail("bad \\u escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }

    std::string string_lit() {
        ++i_;  // opening quote
        std::string o;
        for (;;) {
            if (i_ >= s_.size()) fail("unterminated string");
            char c = s_[i_++];
            if (c == '"') return o;
            if (c != '\\') { o += c; continue; }
            if (i_ >= s_.size()) fail("unterminated string");
            char e = s_[i_++];
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        i_ += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }

    Json value(int depth) {
        if (depth > 200) fail("nesting too deep");
        ws();
        if (i_ >= s_.size()) fail("unexpected end");
        Json v;
        char c = s_[i_];
        if (c == '{') {
            v.t = Json::T::Obj;
            ++i_;
            for (;;) {
                ws();
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; break; }
                if (i_ >= s_.size() || s_[i_] != '"') fail("expected string key");
                std::string k = string_lit();
                ws();
                if (i_ >= s_.size() || s_[i_] != ':') fail("expected ':'");
                ++i_;
                v.keys.push_back(std::move(k));
                v.vals.push_back(value(depth + 1));
                ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; break; }
                fail("expected ',' or '}'");
            }
        } else if (c == '[') {
            v.t = Json::T::Arr;
            ++i_;
            for (;;) {
                ws();
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; break; }
                v.a.push_back(value(depth + 1));
                ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; break; }
                fail("expected ',' or ']'");
            }
        } else if (c == '"') {
            v.t = Json::T::Str;
            v.s = string_lit();
        } else if (s_.compare(i_, 4, "true") == 0) { v.t = Json::T::Bool; v.b = true; i_ += 4; }
        else if (s_.compare(i_, 5, "false") == 0) { v.t = Json::T::Bool; i_ += 5; }
        else if (s_.compare(i_, 4, "null") == 0) { i_ += 4; }
        else {
            const char* st = s_.c_str() + i_;
            char* end = nullptr;
            double d = std::strtod(st, &end);
            if (end == st) fail("unexpected character");
            v.t = Json::T::Num;
            v.n = d;
            i_ += (size_t)(end - st);
        }
        return v;
    }

    const std::string& s_;
    size_t i_ = 0;
};

inline Json parse_json(const std::string& text) { return JsonParser(text).parse(); }

inline std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o + "\"";
}

}  // namespace deck
