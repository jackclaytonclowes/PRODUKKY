// common.h — what both audition renderers share: WAV in and out, the test
// loops, and a reader for FRACTURE's flat patch JSON.
//
// The point of the audition tools is that nothing in this repository has been
// heard. They run a loop through every factory preset of each plugin, with the
// real DSP cores (no JUCE, no host), and write WAV files plus a listening page,
// so anyone with headphones can hear what the tests can only measure.
//
// No dependencies: a 16-bit WAV writer, a reader for 16/24/32-bit PCM and
// 32-bit float, and loops synthesised from scratch so there is no sample in the
// repository to license.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

namespace audition {

struct Stereo { std::vector<float> l, r; double sr = 48000.0; };

// ------------------------------------------------------------------ WAV out
inline bool writeWav16(const std::string& path, const Stereo& s){
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t n = static_cast<uint32_t>(s.l.size());
    const uint32_t sr = static_cast<uint32_t>(s.sr), bytes = n * 4;
    auto u32 = [&](uint32_t v){ f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v){ f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(2); u32(sr); u32(sr * 4); u16(4); u16(16);
    f.write("data", 4); u32(bytes);
    // TPDF dither, so quiet tails do not turn into 16-bit crackle of our own
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    for (uint32_t i = 0; i < n; ++i)
        for (const float v : { s.l[i], s.r[i] }){
            const float x = std::clamp(v, -1.0f, 1.0f) * 32767.0f + d(rng) + d(rng);
            u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(x, -32768.0f, 32767.0f)))));
        }
    return true;
}

// ------------------------------------------------------------------- WAV in
inline bool readWav(const std::string& path, Stereo& out, std::string& err){
    std::ifstream f(path, std::ios::binary);
    if (!f){ err = "cannot open " + path; return false; }
    std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)){
        err = "not a WAV file"; return false;
    }
    auto rd16 = [&](size_t p){ uint16_t v; std::memcpy(&v, b.data() + p, 2); return v; };
    auto rd32 = [&](size_t p){ uint32_t v; std::memcpy(&v, b.data() + p, 4); return v; };
    int fmt = 0, ch = 0, bits = 0; uint32_t sr = 0; size_t data = 0, len = 0;
    for (size_t p = 12; p + 8 <= b.size();){
        const uint32_t sz = rd32(p + 4);
        if (!std::memcmp(b.data() + p, "fmt ", 4)){
            fmt = rd16(p + 8); ch = rd16(p + 10); sr = rd32(p + 12); bits = rd16(p + 22);
            if (fmt == 0xFFFE && sz >= 26) fmt = rd16(p + 32);          // WAVE_FORMAT_EXTENSIBLE
        } else if (!std::memcmp(b.data() + p, "data", 4)){ data = p + 8; len = std::min<size_t>(sz, b.size() - data); }
        p += 8 + sz + (sz & 1);
    }
    if (!data || ch < 1 || !((fmt == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))){
        err = "unsupported WAV: needs 16, 24 or 32-bit PCM, or 32-bit float"; return false;
    }
    const size_t frame = static_cast<size_t>(ch) * static_cast<size_t>(bits / 8), n = len / frame;
    out.sr = sr; out.l.resize(n); out.r.resize(n);
    for (size_t i = 0; i < n; ++i){
        float v[2] = { 0, 0 };
        for (int c = 0; c < std::min(ch, 2); ++c){
            const char* q = b.data() + data + i * frame + static_cast<size_t>(c) * static_cast<size_t>(bits / 8);
            if (fmt == 3){ float x; std::memcpy(&x, q, 4); v[c] = x; }
            else if (bits == 16){ int16_t x; std::memcpy(&x, q, 2); v[c] = x / 32768.0f; }
            else if (bits == 24){
                int32_t x = (static_cast<uint8_t>(q[0]) | (static_cast<uint8_t>(q[1]) << 8) | (static_cast<int8_t>(q[2]) << 16));
                v[c] = x / 8388608.0f;
            } else { int32_t x; std::memcpy(&x, q, 4); v[c] = static_cast<float>(x / 2147483648.0); }
        }
        out.l[i] = v[0]; out.r[i] = ch > 1 ? v[1] : v[0];
    }
    return true;
}

// ---------------------------------------------------------------- the loops
// A two-bar boom-bap pattern at 90 BPM, synthesised: a pitch-swept kick, a
// snare of tone and noise, sixteenth hats with accents, played straight so
// CRATE's swing has something to do. `withMusic` adds a bass line and a
// sustained minor-seventh chord, because distortion on drums alone says little
// about what it does to anything tonal.
inline Stereo makeLoop(double sr, bool withMusic, double bpm = 90.0){
    const double beat = 60.0 / bpm;
    const int n = static_cast<int>(std::lround(8.0 * beat * sr));    // two bars of 4/4
    Stereo s; s.sr = sr; s.l.assign(static_cast<size_t>(n), 0.0f); s.r = s.l;
    std::mt19937 rng(90);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    auto add = [&](int at, float l, float r){
        if (at >= 0 && at < n){ s.l[static_cast<size_t>(at)] += l; s.r[static_cast<size_t>(at)] += r; }
    };
    auto kick = [&](double t0, float vel){
        const int a = static_cast<int>(t0 * sr);
        double ph = 0.0;
        for (int i = 0; i < static_cast<int>(0.45 * sr); ++i){
            const double t = i / sr;
            const double f = 46.0 + 74.0 * std::exp(-t / 0.035);
            ph += 2.0 * M_PI * f / sr;
            const double env = std::exp(-t / 0.20) * std::min(1.0, t / 0.0015);
            const double click = std::exp(-t / 0.002) * 0.4;
            const float v = static_cast<float>((std::sin(ph) * env + click * u(rng) * 0.3) * 0.85 * vel);
            add(a + i, v, v);
        }
    };
    auto snare = [&](double t0, float vel){
        const int a = static_cast<int>(t0 * sr);
        double ph = 0.0, bp1 = 0.0, bp2 = 0.0;
        for (int i = 0; i < static_cast<int>(0.35 * sr); ++i){
            const double t = i / sr;
            ph += 2.0 * M_PI * (185.0 + 40.0 * std::exp(-t / 0.01)) / sr;
            const double body = std::sin(ph) * std::exp(-t / 0.06) * 0.5;
            // noise through a rough band pass around 3-5 kHz
            const double w = u(rng);
            bp1 += 0.35 * (w - bp1); bp2 += 0.35 * (bp1 - bp2);
            const double noise = (w - bp2) * std::exp(-t / 0.11) * 0.7;
            const double env = std::min(1.0, t / 0.001);
            const float v = static_cast<float>((body + noise) * env * 0.55 * vel);
            add(a + i, v * 0.95f, v);
        }
    };
    auto hat = [&](double t0, float vel, bool open){
        const int a = static_cast<int>(t0 * sr);
        double prev = 0.0;
        const double decay = open ? 0.12 : 0.028;
        for (int i = 0; i < static_cast<int>((open ? 0.3 : 0.08) * sr); ++i){
            const double t = i / sr;
            const double w = u(rng);
            const double hp = w - prev; prev = w;              // first difference: a crude high pass
            const float v = static_cast<float>(hp * std::exp(-t / decay) * 0.16 * vel);
            add(a + i, v, v * 0.8f);
        }
    };
    // kick pattern across two bars, in sixteenths: the classic lazy boom-bap
    const int kicks[] = { 0, 7, 10, 16, 23, 26, 30 };
    for (int k : kicks) kick(k * beat / 4.0, k % 4 == 0 ? 1.0f : 0.8f);
    for (int b = 1; b < 8; b += 2) snare(b * beat, 1.0f);
    snare(7.75 * beat, 0.35f);                          // a ghost note on the last sixteenth, into the loop
    for (int k = 0; k < 32; ++k){
        const bool open = k == 14 || k == 30;
        hat(k * beat / 4.0, (k % 4 == 2) ? 1.0f : (k % 2 ? 0.45f : 0.75f), open);
    }

    if (withMusic){
        // a bass line and a sustained Cm7 pad, both a little detuned
        const double bassNotes[] = { 36, 36, 39, 34 };               // MIDI: C2 C2 Eb2 Bb1, two beats each
        for (int k = 0; k < 4; ++k){
            const double f = 440.0 * std::pow(2.0, (bassNotes[k] - 69) / 12.0);
            const int a = static_cast<int>(k * 2.0 * beat * sr);
            double ph = 0.0;
            for (int i = 0; i < static_cast<int>(1.9 * beat * sr); ++i){
                const double t = i / sr;
                ph += 2.0 * M_PI * f / sr;
                const double tone = std::sin(ph) + 0.25 * std::sin(2.0 * ph);
                const double env = std::min(1.0, t / 0.01) * std::exp(-t / 0.9);
                const float v = static_cast<float>(tone * env * 0.30);
                add(a + i, v, v);
            }
        }
        const double chord[] = { 60, 63, 67, 70 };                    // C4 Eb4 G4 Bb4
        for (int c = 0; c < 4; ++c){
            const double f = 440.0 * std::pow(2.0, (chord[c] - 69) / 12.0);
            double pl = 0.0, pr = 0.0, lp = 0.0;
            for (int i = 0; i < n; ++i){
                pl += 2.0 * M_PI * f * 1.002 / sr; pr += 2.0 * M_PI * f * 0.998 / sr;
                // a soft saw: sum of a few harmonics
                double sl = 0, sr2 = 0;
                for (int h = 1; h <= 6; ++h){ sl += std::sin(h * pl) / h; sr2 += std::sin(h * pr) / h; }
                lp += 0.2 * ((sl + sr2) * 0.5 - lp);
                const double swell = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / n);
                add(i, static_cast<float>(sl * 0.03 * (0.6 + 0.4 * swell)),
                       static_cast<float>(sr2 * 0.03 * (0.6 + 0.4 * swell)));
            }
        }
    }
    // headroom, as a recorded loop would have
    float peak = 0.0f;
    for (size_t i = 0; i < s.l.size(); ++i) peak = std::max({ peak, std::fabs(s.l[i]), std::fabs(s.r[i]) });
    const float g = peak > 0.0f ? 0.5f / peak : 1.0f;                 // -6 dBFS peak
    for (auto* ch : { &s.l, &s.r }) for (auto& v : *ch) v *= g;
    return s;
}

inline double rms(const Stereo& s){
    double acc = 0.0;
    for (size_t i = 0; i < s.l.size(); ++i) acc += s.l[i] * s.l[i] + s.r[i] * s.r[i];
    return std::sqrt(acc / std::max<size_t>(1, 2 * s.l.size()));
}
// Loudness as people hear it, not as RMS reads it: the K-weighting of ITU-R
// BS.1770 (a high shelf of about +4 dB above 1.5 kHz, and a high pass around
// 38 Hz), then the mean square over both channels, as an amplitude. RMS counts
// a bright, distorted sound as quieter than it sounds, which is exactly the
// error a distortion's presets would be levelled with. The filters are built
// from the standard's own analogue prototypes, so any sample rate works; at
// 48 kHz they reproduce its published coefficients. No gating: these loops
// have no silence to gate.
inline double loudness(const Stereo& s){
    struct BQ { double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
                double p(double x){ const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    const double fs = s.sr, pi = 3.14159265358979323846;
    // stage 1, the shelf
    double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    double K = std::tan(pi * f0 / fs), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
    double a0 = 1.0 + K / Q + K * K;
    const BQ shelf { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                     2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    // stage 2, the high pass
    f0 = 38.13547087602444; Q = 0.5003270373238773; K = std::tan(pi * f0 / fs);
    a0 = 1.0 + K / Q + K * K;
    const BQ high { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    double acc = 0.0;
    for (const auto* ch : { &s.l, &s.r }){
        BQ a = shelf, b = high;
        for (float v : *ch){ const double y = b.p(a.p(v)); acc += y * y; }
    }
    return std::sqrt(acc / std::max<size_t>(1, 2 * s.l.size()));
}
inline double peak(const Stereo& s){
    double p = 0.0;
    for (size_t i = 0; i < s.l.size(); ++i) p = std::max({ p, static_cast<double>(std::fabs(s.l[i])), static_cast<double>(std::fabs(s.r[i])) });
    return p;
}

// a file name from a preset name: lower case, letters and digits, hyphens
inline std::string slug(const std::string& name){
    std::string out;
    for (unsigned char c : name){
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

// ---------------------------------------------------- FRACTURE's patch JSON
// The factory presets are the browser's flat {id: value} objects, where a value
// is a number, a string or a boolean. That is all this reads.
struct JsonValue { bool isNumber = false, isBool = false, boolean = false; double number = 0.0; std::string text; };
inline std::map<std::string, JsonValue> parseFlatJson(const std::string& s){
    std::map<std::string, JsonValue> out;
    size_t i = 0;
    auto ws = [&]{ while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; };
    auto str = [&]{
        std::string r; ++i;
        while (i < s.size() && s[i] != '"'){ if (s[i] == '\\' && i + 1 < s.size()) ++i; r += s[i++]; }
        ++i; return r;
    };
    ws(); if (i >= s.size() || s[i] != '{') return out; ++i;
    while (true){
        ws(); if (i >= s.size() || s[i] == '}') break;
        if (s[i] != '"') break;
        const std::string key = str();
        ws(); if (i >= s.size() || s[i] != ':') break; ++i; ws();
        JsonValue v;
        if (s[i] == '"'){ v.text = str(); }
        else if (!s.compare(i, 4, "true")){ v.isBool = true; v.boolean = true; v.text = "true"; i += 4; }
        else if (!s.compare(i, 5, "false")){ v.isBool = true; v.text = "false"; i += 5; }
        else {
            size_t used = 0;
            v.number = std::stod(s.substr(i), &used);
            v.isNumber = true; v.text = s.substr(i, used); i += used;
        }
        out[key] = v;
        ws(); if (i < s.size() && s[i] == ',') ++i;
    }
    return out;
}

} // namespace audition
