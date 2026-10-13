// Randomise.h — a new distortion, everything else left where it was.
//
// The Drive panel's dice: each band gets a fresh Mode A and drive, a second
// stage about half the time (with its own mode and a gentler drive), a tone
// tilt and a band mix, and the Crush section is switched in about a third of
// the time. Nothing else moves: the split, the filter, the rhythm, the LFOs,
// the envelope, the matrix, the feedback loop, the tremolo, the output, each
// band's mute, solo and stereo, and the drawn table all stay as they are, so
// a patch's movement survives and only its character changes.
//
// A new distortion is a new loudness too: unlevelled, rolls on Init ran from
// 3 dB quieter to 10 dB louder than the patch they came from. So a roll is
// levelled: the patch before and after are both played a short probe (a kick,
// a snare, a bass and a chord), measured K-weighted as the presets are, and
// every band's level moves by the difference. A roll changes the character,
// not how loud it is.
//
// It works on the plain parameter values (the table's own units, in table
// order) and takes a seed, so it is the same in the plugin and in the tests.
// The plugin applies the result as one undo step.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include "FractureCore.h"

namespace fracture {

// the parameters a roll may change, by id: the per-band drive section (each
// band's level only to keep the loudness where it was) and crush
inline bool randomisedByDice(const std::string& id){
    static const char* const band[] = { "m#a", "d#a", "sb#", "m#b", "d#b", "t#", "mx#", "lv#" };
    for (int b = 0; b < numBands; ++b)
        for (const char* pattern : band){
            std::string s(pattern);
            s.replace(s.find('#'), 1, std::to_string(b));
            if (id == s) return true;
        }
    return id == "bits" || id == "redux" || id == "crMix";
}

class Dice {
public:
    explicit Dice(std::uint64_t seed) : s(seed ^ 0x9E3779B97F4A7C15ull) {}
    // splitmix64: small, and the same on every compiler
    std::uint64_t next(){
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float uniform(){ return static_cast<float>(next() >> 40) / static_cast<float>(1u << 24); }   // [0, 1)
    float between(float lo, float hi){ return lo + (hi - lo) * uniform(); }
    float logBetween(float lo, float hi){ return lo * std::pow(hi / lo, uniform()); }
    int   below(int n){ return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    bool  chance(float p){ return uniform() < p; }
private:
    std::uint64_t s;
};

// How loud a patch is, K-weighted (ITU BS.1770, as audition::loudness in the
// tests), over a probe that has what a distortion reacts to: a kick, a snare,
// a bass note and a chord, at -6 dBFS peak. Transport stopped; the first
// 50 ms, while the engine's ramps settle, are not counted. Half a second, and
// without the oversampling, which barely moves the level: a roll is three of
// these, on the message thread, and has to feel instant
inline double probeLoudness(const std::vector<float>& values){
    const double sr = 48000.0;
    const int n = 24000, block = 512, skip = 2400;
    static const std::vector<float> probe = []{
        std::vector<float> x(24000, 0.0f);
        const double pi = 3.14159265358979323846;
        std::uint32_t r = 12345u;
        auto noise = [&]{ r = r * 1664525u + 1013904223u; return static_cast<double>(r >> 8) / 8388608.0 - 1.0; };
        double kick = 0.0, bass = 0.0, chord[3] = {};
        for (int i = 0; i < 24000; ++i){
            const double t = i / 48000.0;
            const double tk = std::fmod(t, 0.25), ts = std::fmod(t + 0.125, 0.25);
            kick += 2.0 * pi * (48.0 + 70.0 * std::exp(-tk / 0.03)) / 48000.0;
            if (tk < 1.0 / 48000.0) kick = 0.0;
            bass += 2.0 * pi * 65.41 / 48000.0;
            double v = std::sin(kick) * std::exp(-tk / 0.15) * 0.9
                     + noise() * std::exp(-ts / 0.08) * 0.35
                     + (std::sin(bass) + 0.3 * std::sin(2.0 * bass)) * 0.25;
            const double f[3] = { 261.63, 311.13, 392.0 };
            for (int c = 0; c < 3; ++c){
                chord[c] += 2.0 * pi * f[c] / 48000.0;
                for (int h = 1; h <= 4; ++h) v += std::sin(h * chord[c]) / h * 0.035;
            }
            x[static_cast<size_t>(i)] = static_cast<float>(v);
        }
        float pk = 0.0f;
        for (float v : x) pk = std::max(pk, std::fabs(v));
        for (float& v : x) v *= 0.5f / pk;
        return x;
    }();
    Engine e;
    e.prepare(sr, block);
    for (int i = 0; i < static_cast<int>(values.size()); ++i) e.setParam(i, values[static_cast<size_t>(i)]);
    e.setParam(Ids::get().osFactor, 0.0f);
    e.setOversampling(0);
    e.seedFrom(0);
    std::vector<float> L(probe), R(probe);
    for (int i = 0; i < n; i += block){
        float* io[2] = { L.data() + i, R.data() + i };
        e.process(io, 2, std::min(block, n - i));
    }
    struct BQ { double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
                double p(double x){ const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    // the standard's own 48 kHz coefficients: the shelf, then the high pass
    const BQ shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
    const BQ high  { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
    double acc = 0.0;
    for (const auto* ch : { &L, &R }){
        BQ a = shelf, b = high;
        for (int i = 0; i < n; ++i){
            const double y = b.p(a.p((*ch)[static_cast<size_t>(i)]));
            if (i >= skip) acc += y * y;
        }
    }
    return std::sqrt(acc / (2.0 * (n - skip)));
}

// values: every parameter, in table order. Returns them with a new distortion.
// The ranges stay out of the extremes (drive up to 26 dB, not 32; crush down to
// 4 bits, not 2) so a roll is somewhere to start from rather than a wall of
// noise; anything past them is a turn of the knob away
inline std::vector<float> randomiseDrive(std::vector<float> values, std::uint64_t seed, bool level = true){
    const Params& P = Params::get();
    if (static_cast<int>(values.size()) != P.count()) return values;
    const double before = level ? probeLoudness(values) : 0.0;
    Dice dice(seed);
    auto set = [&](const std::string& id, float v){
        const int i = P.index(id);
        if (i >= 0) values[static_cast<size_t>(i)] = std::clamp(v, P[i].min, P[i].max);
    };
    for (int b = 0; b < numBands; ++b){
        const std::string s = std::to_string(b);
        const int modeA = dice.below(numModes);
        set("m" + s + "a", static_cast<float>(modeA));
        set("d" + s + "a", dice.logBetween(1.5f, 20.0f));
        const bool second = dice.chance(0.5f);
        set("sb" + s, second ? 1.0f : 0.0f);
        int modeB = dice.below(numModes - 1);                 // never the same mode twice
        if (modeB >= modeA) ++modeB;
        set("m" + s + "b", static_cast<float>(modeB));
        set("d" + s + "b", dice.logBetween(1.0f, 6.0f));
        set("t" + s, std::round(dice.between(-6.0f, 6.0f) * 2.0f) / 2.0f);
        set("mx" + s, dice.chance(0.6f) ? 100.0f : std::round(dice.between(40.0f, 100.0f)));
    }
    const bool crush = dice.chance(0.35f);
    set("bits", std::round(dice.between(4.0f, 12.0f)));
    set("redux", std::round(dice.logBetween(1.0f, 8.0f)));
    set("crMix", crush ? std::round(dice.between(25.0f, 80.0f)) : 0.0f);
    // level it: every band's level by the same amount, so the balance between
    // them stays as it was. Twice, because the safety clip and a loop back
    // through the drive make level after the drive not quite linear
    for (int pass = 0; level && pass < 2 && before > 1.0e-6; ++pass){
        const double now = probeLoudness(values);
        if (now < 1.0e-6) break;
        const float dB = static_cast<float>(20.0 * std::log10(before / now));
        if (std::fabs(dB) < 0.25f) break;
        for (int b = 0; b < numBands; ++b){
            const std::string id = "lv" + std::to_string(b);
            set(id, values[static_cast<size_t>(P.index(id))] + dB);
        }
    }
    return values;
}

} // namespace fracture
