// inflate.hpp - small DEFLATE (RFC 1951) decompressor, used to read .mcpack/.mcworld zip files.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace deck {

class Inflater {
public:
    Inflater(const uint8_t* in, size_t n, size_t max_out) : in_(in), n_(n), max_(max_out) {}

    std::vector<uint8_t> run(size_t reserve) {
        out_.reserve(reserve < max_ ? reserve : max_);
        int last;
        do {
            last = bits(1);
            int type = bits(2);
            if (type == 0) stored();
            else if (type == 1) fixed();
            else if (type == 2) dynamic();
            else throw std::runtime_error("inflate: invalid block type");
        } while (!last);
        return std::move(out_);
    }

private:
    struct Huff { uint16_t count[16]; uint16_t symbol[288]; };

    int bits(int need) {
        uint32_t val = (uint32_t)bitbuf_;
        while (bitcnt_ < need) {
            if (pos_ == n_) throw std::runtime_error("inflate: unexpected end of data");
            val |= (uint32_t)in_[pos_++] << bitcnt_;
            bitcnt_ += 8;
        }
        bitbuf_ = (int)(val >> need);
        bitcnt_ -= need;
        return (int)(val & ((1u << need) - 1));
    }

    void stored() {
        bitbuf_ = 0; bitcnt_ = 0;
        if (pos_ + 4 > n_) throw std::runtime_error("inflate: truncated stored block");
        unsigned len = in_[pos_] | (in_[pos_ + 1] << 8), nlen = in_[pos_ + 2] | (in_[pos_ + 3] << 8);
        pos_ += 4;
        if (len != (~nlen & 0xFFFF)) throw std::runtime_error("inflate: bad stored block");
        if (pos_ + len > n_) throw std::runtime_error("inflate: truncated stored block");
        check_room(len);
        out_.insert(out_.end(), in_ + pos_, in_ + pos_ + len);
        pos_ += len;
    }

    void check_room(size_t add) const {
        if (out_.size() + add > max_) throw std::runtime_error("inflate: output too large");
    }

    static int construct(Huff& h, const uint16_t* length, int n) {
        for (int len = 0; len <= 15; ++len) h.count[len] = 0;
        for (int s = 0; s < n; ++s) h.count[length[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = (uint16_t)(offs[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (length[s] != 0) h.symbol[offs[length[s]]++] = (uint16_t)s;
        return left;
    }

    int decode(const Huff& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= bits(1);
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw std::runtime_error("inflate: invalid code");
    }

    void codes(const Huff& lencode, const Huff& distcode) {
        static const uint16_t lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint16_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint16_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lencode);
            if (sym < 256) {
                check_room(1);
                out_.push_back((uint8_t)sym);
            } else if (sym == 256) {
                return;
            } else {
                sym -= 257;
                if (sym >= 29) throw std::runtime_error("inflate: invalid length symbol");
                size_t len = lens[sym] + (size_t)bits(lext[sym]);
                int ds = decode(distcode);
                if (ds >= 30) throw std::runtime_error("inflate: invalid distance symbol");
                size_t dist = dists[ds] + (size_t)bits(dext[ds]);
                if (dist > out_.size()) throw std::runtime_error("inflate: distance too far back");
                check_room(len);
                size_t from = out_.size() - dist;
                for (size_t k = 0; k < len; ++k) out_.push_back(out_[from + k]);
            }
        }
    }

    void fixed() {
        Huff lencode, distcode;
        uint16_t lengths[288];
        int s = 0;
        for (; s < 144; ++s) lengths[s] = 8;
        for (; s < 256; ++s) lengths[s] = 9;
        for (; s < 280; ++s) lengths[s] = 7;
        for (; s < 288; ++s) lengths[s] = 8;
        construct(lencode, lengths, 288);
        for (s = 0; s < 30; ++s) lengths[s] = 5;
        construct(distcode, lengths, 30);
        codes(lencode, distcode);
    }

    void dynamic() {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        uint16_t lengths[320];
        int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) throw std::runtime_error("inflate: bad counts");
        int index = 0;
        for (; index < ncode; ++index) lengths[order[index]] = (uint16_t)bits(3);
        for (; index < 19; ++index) lengths[order[index]] = 0;
        Huff lencode, distcode;
        if (construct(lencode, lengths, 19) != 0) throw std::runtime_error("inflate: bad code lengths code");
        index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 16) {
                lengths[index++] = (uint16_t)sym;
            } else {
                uint16_t len = 0;
                if (sym == 16) {
                    if (index == 0) throw std::runtime_error("inflate: repeat with no previous length");
                    len = lengths[index - 1];
                    sym = 3 + bits(2);
                } else if (sym == 17) sym = 3 + bits(3);
                else sym = 11 + bits(7);
                if (index + sym > nlen + ndist) throw std::runtime_error("inflate: too many lengths");
                while (sym--) lengths[index++] = len;
            }
        }
        if (lengths[256] == 0) throw std::runtime_error("inflate: missing end-of-block code");
        if (construct(lencode, lengths, nlen) < 0) throw std::runtime_error("inflate: bad literal/length code");
        if (construct(distcode, lengths + nlen, ndist) < 0) throw std::runtime_error("inflate: bad distance code");
        codes(lencode, distcode);
    }

    const uint8_t* in_;
    size_t n_, pos_ = 0, max_;
    int bitbuf_ = 0, bitcnt_ = 0;
    std::vector<uint8_t> out_;
};

}  // namespace deck
