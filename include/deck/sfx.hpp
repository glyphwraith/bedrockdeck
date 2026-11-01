// sfx.hpp - tiny synthesizer: builds the click / blip / meow sounds as in-memory WAV files (no audio files needed).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace deck {
namespace sfx {

enum class Id { Click, Tick, Tab, On, Off, Success, Error, Meow, Pop, Sun, Moon, Count };

class Synth {
public:
    static constexpr int kRate = 22050;
    std::vector<float> s;

    // one note: frequency glides f0 -> f1; wave 0 = sine, 1 = triangle, 2 = soft square
    void tone(float f0, float f1, float ms, float vol, int wave = 0, float decay = 5.f, float vib = 0.f) {
        int n = (int)(kRate * ms / 1000.f);
        for (int i = 0; i < n; ++i) {
            float t = (float)i / (float)n;
            float f = f0 + (f1 - f0) * t + (vib > 0 ? vib * std::sin(2.f * 3.14159265f * 9.f * (float)i / kRate) : 0.f);
            phase_ += 2.f * 3.14159265f * f / kRate;
            float p = phase_ - 6.2831853f * std::floor(phase_ / 6.2831853f);
            float v = wave == 0 ? std::sin(p) : wave == 1 ? (p < 3.14159265f ? -1.f + 2.f * p / 3.14159265f : 3.f - 2.f * p / 3.14159265f) : (std::sin(p) > 0 ? 0.6f : -0.6f);
            float attack = std::min(1.f, (float)i / (kRate * 0.002f));
            s.push_back(v * vol * attack * std::exp(-decay * t));
        }
    }
    void noise(float ms, float vol, float decay) {
        int n = (int)(kRate * ms / 1000.f);
        for (int i = 0; i < n; ++i) {
            float t = (float)i / (float)n;
            s.push_back(((std::rand() % 2000) / 1000.f - 1.f) * vol * std::exp(-decay * t));
        }
    }
    void rest(float ms) { s.insert(s.end(), (size_t)(kRate * ms / 1000.f), 0.f); }
    // mixes a noise burst on top of what is already there (for the "click" transient)
    void burst(float ms, float vol, float decay) {
        int n = (int)(kRate * ms / 1000.f);
        for (int i = 0; i < n && i < (int)s.size(); ++i) {
            float t = (float)i / (float)n;
            s[(size_t)i] += ((std::rand() % 2000) / 1000.f - 1.f) * vol * std::exp(-decay * t);
        }
    }

    std::vector<uint8_t> wav() const {
        std::vector<uint8_t> o;
        auto p32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) o.push_back((uint8_t)(v >> (8 * k))); };
        auto p16 = [&](uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); };
        uint32_t bytes = (uint32_t)s.size() * 2;
        o.insert(o.end(), {'R', 'I', 'F', 'F'}); p32(36 + bytes);
        o.insert(o.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); p32(16); p16(1); p16(1); p32(kRate); p32(kRate * 2); p16(2); p16(16);
        o.insert(o.end(), {'d', 'a', 't', 'a'}); p32(bytes);
        for (float v : s) {
            int x = (int)(std::max(-1.f, std::min(1.f, v)) * 30000.f);
            p16((uint16_t)(int16_t)x);
        }
        return o;
    }

private:
    float phase_ = 0.f;
};

inline std::vector<uint8_t> build(Id id) {
    Synth y;
    switch (id) {
        case Id::Click:   y.tone(1700, 900, 28, 0.55f, 0, 7.f); y.burst(6, 0.35f, 6.f); break;
        case Id::Tick:    y.tone(2600, 2400, 9, 0.16f, 0, 6.f); break;
        case Id::Tab:     y.tone(520, 640, 55, 0.42f, 1, 5.f); y.burst(5, 0.2f, 6.f); break;
        case Id::On:      y.tone(660, 660, 55, 0.4f, 1, 4.f); y.tone(990, 990, 80, 0.4f, 1, 5.f); break;
        case Id::Off:     y.tone(990, 990, 55, 0.4f, 1, 4.f); y.tone(660, 660, 80, 0.4f, 1, 5.f); break;
        case Id::Success: y.tone(523, 523, 60, 0.4f, 1, 3.f); y.tone(659, 659, 60, 0.4f, 1, 3.f); y.tone(784, 784, 60, 0.4f, 1, 3.f); y.tone(1047, 1047, 160, 0.42f, 1, 5.f); break;
        case Id::Error:   y.tone(190, 140, 150, 0.3f, 2, 2.f); y.rest(30); y.tone(150, 105, 200, 0.3f, 2, 3.f); break;
        case Id::Meow:    y.tone(480, 880, 90, 0.36f, 1, 1.f, 12.f); y.tone(880, 560, 150, 0.36f, 1, 3.f, 12.f); break;
        case Id::Pop:     y.tone(300, 950, 55, 0.5f, 0, 8.f); break;
        case Id::Sun:     y.tone(392, 392, 55, 0.36f, 0, 4.f); y.tone(523, 523, 55, 0.36f, 0, 4.f); y.tone(659, 659, 120, 0.36f, 0, 5.f); break;
        case Id::Moon:    y.tone(659, 659, 55, 0.36f, 0, 4.f); y.tone(494, 494, 55, 0.36f, 0, 4.f); y.tone(392, 392, 140, 0.34f, 0, 5.f); break;
        default: break;
    }
    return y.wav();
}

// Built once on first use; the buffers stay alive for the whole run (Windows plays them straight from memory).
inline const std::vector<uint8_t>& get(Id id) {
    static std::vector<uint8_t> cache[(int)Id::Count];
    auto& c = cache[(int)id];
    if (c.empty()) c = build(id);
    return c;
}

}  // namespace sfx
}  // namespace deck
