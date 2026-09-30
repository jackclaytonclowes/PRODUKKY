// test_core.cpp — measurements, not vibes.
//
// Nobody involved in writing this has heard it: there is no audio device where
// it was built. So every claim the panel makes gets measured instead —
// the zero-order-hold droop, where the fold-back lands and that the anti-alias
// control really moves it, six decibels per bit, which way companding trades
// resolution, the filter's marked cutoff and slope, and the swing offset in
// milliseconds against what the grid says it should be.
//
//   npm run test:crate
#include "CrateCore.h"
#include "Relevance.h"
#include "History.h"
#include "../Source/Presets.h"
#include "../../tools/audition/common.h"
#include <cstdio>
#include <complex>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <random>
#include <map>

using namespace crate;

static int passed = 0;
static std::vector<std::string> failures;
static void check(const std::string& name, bool ok, const std::string& detail = ""){
    if (ok){ ++passed; std::printf("  ok    %s%s\n", name.c_str(), std::getenv("VERBOSE") && !detail.empty() ? (" — " + detail).c_str() : ""); }
    else {
        failures.push_back(name + (detail.empty() ? "" : " — " + detail));
        std::printf("  FAIL  %s%s\n", name.c_str(), detail.empty() ? "" : (" — " + detail).c_str());
    }
}
static std::string f2s(double v, int p = 2){
    char b[64]; std::snprintf(b, sizeof b, "%.*f", p, v); return b;
}
static double dBOf(double x){ return 20.0 * std::log10(std::max(1e-12, x)); }

// ------------------------------------------------------------------- helpers
// ------------------------------------------------------------- zipper noise
// A host moves a knob once per block. A control whose coefficients jump at
// each block edge puts energy between the harmonics of whatever passes
// through; one that glides does not. The yardstick is the same sweep sent in
// 16-sample blocks, the smoothest a host can manage: a moving filter always
// modulates what passes through it, and that part is not zipper.
static void fftInPlace(std::vector<std::complex<double>>& a){
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i){ size_t b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1){
        const std::complex<double> wl = std::polar(1.0, -2.0 * M_PI / static_cast<double>(len));
        for (size_t i = 0; i < n; i += len){
            std::complex<double> w = 1.0;
            for (size_t k = 0; k < len / 2; ++k){ const auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; }
        }
    }
}
// energy further than 4 bins from any harmonic of f0, against the total, in dB
static double inharmonicDb(const std::vector<float>& x, double f0, double sr){
    const size_t n = 65536;
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) a[i] = x[x.size() - n + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1)));
    fftInPlace(a);
    double tot = 0.0, bad = 0.0; const double binHz = sr / n;
    for (size_t k = 1; k < n / 2; ++k){
        const double p = std::norm(a[k]); tot += p;
        const double h = k * binHz / f0;
        if (std::fabs(h - std::round(h)) * f0 / binHz > 4.0) bad += p;
    }
    return 10.0 * std::log10(std::max(bad, 1e-30) / tot);
}

using Patch = std::map<std::string, float>;

static void applyPatch(Engine& e, const Patch& patch){
    const Params& P = Params::get();
    for (int i = 0; i < P.count(); ++i) e.setParam(i, P[i].def);
    for (const auto& kv : patch){
        const int i = P.index(kv.first);
        if (i >= 0) e.setParam(i, kv.second);
    }
}

// magnitude of one frequency, by Goertzel — no FFT needed to ask "how much of
// exactly this tone came out"
static double goertzel(const std::vector<float>& x, double freq, double sr, int from){
    const int n = static_cast<int>(x.size()) - from;
    if (n <= 0) return 0.0;
    const double w = 2.0 * M_PI * freq / sr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = from; i < static_cast<int>(x.size()); ++i){
        const double s = x[static_cast<size_t>(i)] + c * s1 - s2;
        s2 = s1; s1 = s;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / n;
}
// The noise a converter adds, measured by fitting the test tone exactly and
// subtracting it. Taking (total power - tone power) instead leaves the error in
// the amplitude estimate behind, which at these levels is far larger than the
// quantisation noise being looked for — the first version of this file measured
// its own rounding and reported that 16 bits and 12 bits sound identical.
// Frequencies must divide the sample rate to a whole number of samples.
static double residualPower(const std::vector<float>& x, double f, double sr, int from){
    const double period = sr / f;
    const int avail = static_cast<int>(x.size()) - from;
    const int cycles = static_cast<int>(std::floor(avail / period));
    const int n = static_cast<int>(std::llround(cycles * period));
    if (cycles < 4 || n <= 0) return 0.0;
    const double w = 2.0 * M_PI * f / sr;
    double a = 0.0, b = 0.0;
    for (int i = 0; i < n; ++i){
        const double v = x[static_cast<size_t>(from + i)];
        a += v * std::sin(w * i);
        b += v * std::cos(w * i);
    }
    a *= 2.0 / n; b *= 2.0 / n;
    double p = 0.0;
    for (int i = 0; i < n; ++i){
        const double fit = a * std::sin(w * i) + b * std::cos(w * i);
        const double e = x[static_cast<size_t>(from + i)] - fit;
        p += e * e;
    }
    return p / n;
}

static double totalPower(const std::vector<float>& x, int from){
    double p = 0.0; int n = 0;
    for (int i = from; i < static_cast<int>(x.size()); ++i){ p += static_cast<double>(x[static_cast<size_t>(i)]) * x[static_cast<size_t>(i)]; ++n; }
    return n > 0 ? p / n : 0.0;
}

struct Render { std::vector<float> l, r; long bad = 0; double peak = 0.0; };

static Render run(const Patch& patch, const std::vector<float>& inL,
                  double sr = 48000.0, int block = 128,
                  bool playing = false, double bpm = 120.0){
    Engine e;
    e.prepare(sr, block);
    applyPatch(e, patch);
    e.seedFrom(0);
    Render out;
    out.l = inL; out.r = inL;
    const int n = static_cast<int>(inL.size());
    double ppq = 0.0;
    for (int i = 0; i < n; i += block){
        const int m = std::min(block, n - i);
        e.setTransport(playing, ppq, bpm);
        float* io[2] = { out.l.data() + i, out.r.data() + i };
        e.process(io, 2, m);
        ppq += static_cast<double>(m) / sr * bpm / 60.0;
    }
    for (float v : out.l){
        if (!std::isfinite(v)) ++out.bad;
        else out.peak = std::max(out.peak, static_cast<double>(std::fabs(v)));
    }
    return out;
}

static std::vector<float> sine(double freq, double amp, double seconds, double sr){
    const int n = static_cast<int>(seconds * sr);
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * i / sr));
    return v;
}
static std::vector<float> noise(double amp, double seconds, double sr, unsigned seed = 1){
    const int n = static_cast<int>(seconds * sr);
    std::vector<float> v(static_cast<size_t>(n));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-static_cast<float>(amp), static_cast<float>(amp));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = d(rng);
    return v;
}

// where the loud moments are, to the sample
static std::vector<int> peaks(const std::vector<float>& x, double threshold){
    std::vector<int> found;
    for (size_t i = 1; i + 1 < x.size(); ++i){
        const double a = std::fabs(x[i]);
        if (a > threshold && a >= std::fabs(x[i - 1]) && a > std::fabs(x[i + 1])){
            if (!found.empty() && static_cast<int>(i) - found.back() < 200) {
                if (a > std::fabs(x[static_cast<size_t>(found.back())])) found.back() = static_cast<int>(i);
            } else found.push_back(static_cast<int>(i));
        }
    }
    return found;
}

// --------------------------------------------------- undo, redo and A/B stacks
// core/History.h, identical in CRATE and FRACTURE, and tested identically in both
static void testHistory(){
    using session::Snapshot;
    const Snapshot a { 0.0f, 0.5f }, b { 0.1f, 0.5f }, c { 0.1f, 0.9f }, d { 0.7f, 0.2f };
    {
        session::History h;
        h.reset(a);
        check("history: an unchanged snapshot is not a step", !h.commit(a) && !h.canUndo());
        h.commit(b); h.commit(c);
        check("history: each change is one step", h.undoDepth() == 2);
        const Snapshot u1 = h.undo(c), u2 = h.undo(u1);
        check("history: undo walks back in order", u1 == b && u2 == a && !h.canUndo());
        check("history: redo walks forward again", h.redo(a) == b && h.redo(b) == c && !h.canRedo());
        h.undo(c);
        h.commit(d);
        check("history: a fresh edit after undo drops what could be redone", !h.canRedo() && h.redo(d) == d);
    }
    {
        session::History h;
        h.reset(a);
        check("history: undo takes back a change that was never committed (automation)",
              h.undo(b) == a && h.redo(a) == b);
    }
    {
        session::History h(3);
        h.reset(a);
        for (int i = 1; i <= 5; ++i) h.commit(Snapshot{ static_cast<float>(i), 0.0f });
        check("history: the stack is capped", h.undoDepth() == 3);
        session::History s;
        s.reset(a); s.commit(b);
        s.settle(Snapshot{ 0.1000001f, 0.5f });
        check("history: settling on a value that did not round-trip is not a step", s.undoDepth() == 1);
    }
    {
        session::Workspace w;
        w.reset(a);
        w.commit(b);                                          // A: a -> b
        check("A/B: B opens as a copy of A", w.select(1, b) == b && w.active() == 1);
        check("A/B: B starts with nothing to undo", !w.canUndo(b));
        w.commit(c);                                          // B: b -> c
        check("A/B: back to A gives A's settings", w.select(0, c) == b && w.active() == 0);
        check("A/B: A's undo carries on where it left off", w.undo(b) == a);
        check("A/B: and B still has B's settings", w.select(1, a) == c);
        check("A/B: B's undo is B's own", w.undo(c) == b && !w.canUndo(b));
        check("A/B: a switch is never an undo step", w.select(0, b) == a && w.undo(a) == a);
    }
    {
        session::Workspace w;
        w.reset(a);
        w.copyToOther(d);
        check("A/B: copying to the hidden side is what it then shows", w.select(1, d) == d);
        w.restore(1, a);
        w.reset(c);
        check("A/B: a restored session keeps which side is showing, and the other", w.active() == 1
              && w.select(0, c) == a && !w.canUndo(a));
    }
}

int main(){
    const double sr = 48000.0;
    const Params& P = Params::get();

    std::printf("\nParameters\n");
    check("the table is complete", P.count() == 40, std::to_string(P.count()) + " parameters");
    check("the clock defaults to the rate the hardware ran at",
          std::fabs(P[P.index("clock")].def - 26040.0f) < 1.0f);
    check("twelve bits by default", std::fabs(P[P.index("bits")].def - 12.0f) < 0.01f);
    // the SP and the S900 both stored linear PCM; companding is an extra, not the default
    check("and linear by default, the format the hardware stored",
          P[P.index("compand")].def == 0.0f);
    check("the machine defaults to SP, so older patches sound the same",
          P[P.index("machine")].def == 0.0f && P[P.index("trick")].def == 0.0f);

    // ----------------------------------------------------------- the converter
    // measured on its own. Through the whole box the four-pole's saturation
    // colours every reading, which is fine for music and useless for a metre.
    std::printf("\nUndo and A/B\n");
    testHistory();

    std::printf("\nConverter\n");
    auto throughConverter = [&](double freq, double amp, double clock, double aa,
                                double bits, double compand,
                                Machine machine = Machine::SP, double trick = 0.0){
        Converter c;
        c.prepare(sr);
        c.configure(clock, aa, bits, compand, machine, trick);
        auto in = sine(freq, amp, 0.5, sr);
        for (auto& v : in) v = static_cast<float>(c.process(0, v));
        return in;
    };
    {
        const auto lo = throughConverter(1000, 0.25, 26040, 1.0, 16, 0.0);
        const auto hi = throughConverter(10000, 0.25, 26040, 0.0, 16, 0.0);
        const double gLo = goertzel(lo, 1000, sr, 4800) / 0.25;
        const double gHi = goertzel(hi, 10000, sr, 4800) / 0.25;
        check("a 1 kHz tone comes through at unity", std::fabs(dBOf(gLo)) < 0.6,
              f2s(dBOf(gLo)) + " dB");
        // zero-order hold droops as sinc(f/clock); at 10 kHz on a 26.04 kHz
        // clock that is about -2.2 dB before the output filter adds its own
        check("the hold droops the top end the way a sample-and-hold does",
              dBOf(gHi) < -1.5 && dBOf(gHi) > -5.0, f2s(dBOf(gHi)) + " dB at 10 kHz");
    }
    {
        // 16 kHz is above half the clock, so it folds back to 26040-16000
        const double in = 16000.0, fold = 26040.0 - 16000.0;
        const auto a = throughConverter(in, 0.25, 26040, 0.0, 16, 0.0);
        const auto b = throughConverter(in, 0.25, 26040, 1.0, 16, 0.0);
        const double fa = dBOf(goertzel(a, fold, sr, 4800) / 0.25);
        const double fb = dBOf(goertzel(b, fold, sr, 4800) / 0.25);
        check("content above half the clock folds back into the band",
              fa > -30.0, f2s(fa) + " dB at " + f2s(fold, 0) + " Hz");
        check("and the anti-alias control is what decides how much",
              fa - fb > 10.0, f2s(fa) + " dB open vs " + f2s(fb) + " dB shut");
    }
    {
        // six decibels a bit, measured as the noise left once the tone is removed
        auto floorAt = [&](double bits){
            const auto r = throughConverter(1000, 0.5, 48000, 1.0, bits, 0.0);
            return 10.0 * std::log10(std::max(1e-14, residualPower(r, 1000, sr, 4800)));
        };
        const double n16 = floorAt(16), n12 = floorAt(12), n8 = floorAt(8);
        check("dropping 16 bits to 12 costs about 24 dB of noise floor",
              n12 - n16 > 16.0 && n12 - n16 < 32.0, f2s(n12 - n16) + " dB");
        check("and 12 to 8 costs about the same again",
              n8 - n12 > 16.0 && n8 - n12 < 32.0, f2s(n8 - n12) + " dB");
    }
    {
        // companding spends resolution on quiet signals and takes it from loud
        // ones — that trade IS the character, so both directions are asserted
        auto floorFor = [&](double amp, double compand){
            const auto r = throughConverter(1000, amp, 48000, 1.0, 12, compand);
            // relative to the signal, or the quiet case just looks quieter
            return 10.0 * std::log10(std::max(1e-14, residualPower(r, 1000, sr, 4800)))
                   - 20.0 * std::log10(amp);
        };
        const double loudLin = floorFor(0.5, 0.0), loudMu = floorFor(0.5, 1.0);
        const double quietLin = floorFor(0.01, 0.0), quietMu = floorFor(0.01, 1.0);
        check("companding makes loud hits grainier than linear 12-bit",
              loudMu - loudLin > 3.0, f2s(loudMu - loudLin) + " dB");
        check("and quiet tails cleaner",
              quietLin - quietMu > 3.0, f2s(quietLin - quietMu) + " dB");
    }

    // ------------------------------------------------------------ the machines
    std::printf("\nMachine\n");
    {
        const auto r = throughConverter(1000, 0.25, 26040, 0.25, 16, 0.0, Machine::S900);
        const double g = dBOf(goertzel(r, 1000, sr, 4800) / 0.25);
        check("S900: a 1 kHz tone comes through at unity", std::fabs(g) < 0.6, f2s(g) + " dB");
    }
    {
        // 20 kHz on a 26.04 kHz clock folds to 6.04 kHz. The SP's gentle filter
        // lets it through; the S900's six-pole, at 0.46 of the clock with the
        // knob open, should take at least 20 dB more of it out
        const double fold = 26040.0 - 20000.0;
        const auto sp = throughConverter(20000, 0.25, 26040, 0.0, 16, 0.0, Machine::SP);
        const auto s9 = throughConverter(20000, 0.25, 26040, 0.0, 16, 0.0, Machine::S900);
        const double fsp = dBOf(goertzel(sp, fold, sr, 4800) / 0.25);
        const double fs9 = dBOf(goertzel(s9, fold, sr, 4800) / 0.25);
        check("S900: its anti-alias filter stops what the SP folds back",
              fsp - fs9 > 20.0, "SP " + f2s(fsp) + " dB, S900 " + f2s(fs9) + " dB at 6.04 kHz");
    }
    {
        // 5 kHz on a 16 kHz clock leaves an image of the hold at 11 kHz. The SP
        // keeps it; the S900's reconstruction filter removes it
        const auto sp = throughConverter(5000, 0.25, 16000, 1.0, 16, 0.0, Machine::SP);
        const auto s9 = throughConverter(5000, 0.25, 16000, 1.0, 16, 0.0, Machine::S900);
        const double isp = dBOf(goertzel(sp, 11000, sr, 4800) / 0.25);
        const double is9 = dBOf(goertzel(s9, 11000, sr, 4800) / 0.25);
        check("S900: its reconstruction filter removes the images the SP leaves in",
              isp - is9 > 20.0, "SP " + f2s(isp) + " dB, S900 " + f2s(is9) + " dB at 11 kHz");
    }

    // ------------------------------------------------------- the pitch trick
    std::printf("\nPitch trick\n");
    {
        // sped up six semitones and read back onto the fixed clock: the pitch
        // and the level must come out where they went in...
        const auto plain = throughConverter(1000, 0.25, 26040, 1.0, 16, 0.0, Machine::SP, 0.0);
        const auto trick = throughConverter(1000, 0.25, 26040, 1.0, 16, 0.0, Machine::SP, 6.0);
        const double fund = dBOf(goertzel(trick, 1000, sr, 4800) / 0.25);
        const double down = dBOf(goertzel(trick, 707.1, sr, 4800) / 0.25);
        check("the pitch trick leaves the pitch and the level alone",
              std::fabs(fund) < 1.0 && down < -40.0,
              "1 kHz " + f2s(fund) + " dB, 707 Hz " + f2s(down) + " dB");
        // ...and what it adds is the drop-sample grit: irregular repeats that are
        // not harmonics of anything, measured as what is left once the tone is fit
        const double nPlain = 10.0 * std::log10(std::max(1e-14, residualPower(plain, 1000, sr, 4800)));
        const double nTrick = 10.0 * std::log10(std::max(1e-14, residualPower(trick, 1000, sr, 4800)));
        check("and adds grit that the plain converter does not have",
              nTrick - nPlain > 6.0, f2s(nTrick - nPlain) + " dB more residue");
    }
    {
        // the record was sampled sped up, so the effective rate drops: at +6 it
        // is 26040 / 1.414 = 18413 Hz, and 10 kHz now folds to 8413 Hz, where
        // the plain converter (half-clock 13 kHz) puts nothing
        const double eff = 26040.0 / std::pow(2.0, 0.5), fold = eff - 10000.0;
        const auto plain = throughConverter(10000, 0.25, 26040, 0.0, 16, 0.0, Machine::SP, 0.0);
        const auto trick = throughConverter(10000, 0.25, 26040, 0.0, 16, 0.0, Machine::SP, 6.0);
        const double a = dBOf(goertzel(plain, fold, sr, 4800) / 0.25);
        const double b = dBOf(goertzel(trick, fold, sr, 4800) / 0.25);
        check("it lowers the effective sample rate, so fold-back lands lower",
              b > -30.0 && b - a > 20.0, "plain " + f2s(a) + " dB, trick " + f2s(b) + " dB at " + f2s(fold, 0) + " Hz");
    }
    {
        // tune moves the clock without moving the pitch
        const auto r = run({ {"tune", -12}, {"aa", 50}, {"bits", 12} }, sine(1000, 0.25, 0.5, sr));
        const double fund = dBOf(goertzel(r.l, 1000, sr, 4800) / 0.25);
        const double octDown = dBOf(goertzel(r.l, 500, sr, 4800) / 0.25);
        check("tune leaves the pitch alone", fund > -6.0 && octDown < -30.0,
              "1 kHz " + f2s(fund) + " dB, 500 Hz " + f2s(octDown) + " dB");
    }

    // --------------------------------------------------------------- the filter
    std::printf("\nFour-pole\n");
    {
        // the filter on its own, at a level low enough that its saturation is
        // out of the way — this is a frequency response, not a distortion test
        auto at = [&](double f){
            Ladder l;
            l.prepare(sr);
            l.configure(1000.0, 0.0, 1.0);
            auto in = sine(f, 0.02, 0.5, sr);
            for (auto& v : in) v = static_cast<float>(l.process(0, v));
            return dBOf(goertzel(in, f, sr, 12000) / 0.02);
        };
        const double m100 = at(100), m1k = at(1000), m4k = at(4000), m8k = at(8000);
        check("the passband is flat below the cutoff", std::fabs(m100) < 1.0, f2s(m100) + " dB at 100 Hz");
        check("the marked cutoff is the -3 dB point", std::fabs(m1k + 3.0) < 1.2,
              f2s(m1k) + " dB at the mark");
        // a four-pole only reaches its asymptote a couple of octaves out, so the
        // slope is measured where the asymptote actually is
        check("four poles means about 24 dB an octave up where it settles",
              (m4k - m8k) > 17.0 && (m4k - m8k) < 27.0,
              f2s(m4k - m8k) + " dB from 4 to 8 kHz");
    }

    // ----------------------------------------------------------------- the feel
    // ------------------------------------------------------ shape and poles
    std::printf("\nShape and poles\n");
    {
        // every combination, on the filter alone at the rate it really runs at
        // (the host rate times four), low enough that the saturation is out of it
        const double osr = sr * 4.0;
        auto at = [&](int shape, int poles, double f){
            Ladder l; l.prepare(osr); l.configure(1000.0, 0.0, 1.0, shape, poles);
            auto in = sine(f, 0.02, 0.4, osr);
            for (auto& v : in) v = static_cast<float>(l.process(0, v));
            return dBOf(goertzel(in, f, osr, static_cast<int>(osr * 0.15)) / 0.02);
        };
        const char* nm[] = { "LP", "BP", "HP", "BR" };
        double worstMark = 0.0, worstSlope = 0.0, worstNotch = -999.0, worstPeak = 0.0;
        std::string slopeDetail;
        for (int shape = 0; shape < 4; ++shape) for (int poles = 2; poles <= 8; poles += 2){
            const double mark = at(shape, poles, 1000);
            if (shape == Ladder::LP || shape == Ladder::HP){
                worstMark = std::max(worstMark, std::fabs(mark + 3.0));
                // far enough out that N poles have reached their asymptote
                const double a = at(shape, poles, shape == Ladder::LP ? 16000 : 62.5);
                const double b = at(shape, poles, shape == Ladder::LP ? 32000 : 31.25);
                const double err = std::fabs((a - b) - 6.0 * poles) / (6.0 * poles);
                if (err > worstSlope){ worstSlope = err;
                    slopeDetail = std::string(nm[shape]) + std::to_string(poles) + " " + f2s(a - b) + " dB/oct"; }
            } else if (shape == Ladder::BP){
                worstPeak = std::max(worstPeak, std::fabs(mark));
            } else {
                worstNotch = std::max(worstNotch, mark);
            }
        }
        check("low and high pass are 3 dB down on the mark, at every slope",
              worstMark < 0.3, "worst " + f2s(worstMark) + " dB off");
        check("and fall at 6 dB an octave per pole",
              worstSlope < 0.12, "worst " + slopeDetail);
        check("band pass peaks on the mark at unity", worstPeak < 0.3, "worst " + f2s(worstPeak) + " dB");
        check("band reject is a real notch on the mark", worstNotch < -60.0,
              "shallowest " + f2s(worstNotch) + " dB");
        // and it must still be the old filter at the old settings
        check("four-pole low pass is still the default", P[P.index("fltShape")].def == 0.0f
              && P[P.index("fltPoles")].def == 1.0f && P[P.index("fltMix")].def == 100.0f);
    }
    {
        // Filter mix at 0 takes the four-pole out completely: a closed filter
        // and an open one must then give the same output
        const auto in = noise(0.3, 0.3, sr);
        const auto a = run({ {"fltFreq", 200}, {"fltMix", 0} }, in);
        const auto b = run({ {"fltFreq", 18000}, {"fltMix", 0}, {"fltReso", 80} }, in);
        double worst = 0.0;
        for (size_t k = 0; k < a.l.size(); ++k) worst = std::max(worst, static_cast<double>(std::fabs(a.l[k] - b.l[k])));
        check("filter mix at zero takes the filter out", worst < 1e-6, f2s(worst, 9));
    }

    // ------------------------------------------------------------ the rhythm
    std::printf("\nRhythm\n");
    auto rhythmTrace = [&](const Patch& patch, double seconds, bool playing, double startPpq,
                           double bpm, int ch = 0){
        Engine e; e.prepare(sr, 64); applyPatch(e, patch);
        const int n = static_cast<int>(seconds * sr);
        std::vector<float> l(static_cast<size_t>(n), 0.0f), r = l, trace;
        double ppq = startPpq;
        for (int i = 0; i < n; i += 64){
            const int m = std::min(64, n - i);
            e.setTransport(playing, ppq, bpm);
            float* io[2] = { l.data() + i, r.data() + i };
            e.process(io, 2, m);
            trace.push_back(static_cast<float>(e.rhythmValue(ch)));   // one value a block
            if (playing) ppq += m / sr * bpm / 60.0;
        }
        return trace;
    };
    const double blockSec = 64.0 / sr;
    {
        // square on 1/4 at 120 BPM: half a second a cycle, high for the first half
        const Patch sq = { {"rhShape", 4}, {"rhDiv", 6}, {"rhGlide", 0} };
        const auto t = rhythmTrace(sq, 2.0, true, 0.0, 120.0);
        int rises = 0; double firstRise = -1;
        for (size_t k = 1; k < t.size(); ++k)
            if (t[k - 1] < 0.5f && t[k] >= 0.5f){ ++rises; if (firstRise < 0 && k > 2) firstRise = k * blockSec; }
        check("a synced square on 1/4 at 120 BPM rises every half second",
              rises >= 3 && rises <= 4 && std::fabs(firstRise - 0.5) < 0.004,
              std::to_string(rises) + " rises, first at " + f2s(firstRise, 4) + " s");
        // the same bar from a different start: position, not integration
        const auto u = rhythmTrace(sq, 1.0, true, 8.0, 120.0);    // two bars later
        const auto v = rhythmTrace(sq, 1.0, true, 0.0, 120.0);
        double worst = 0.0;
        for (size_t k = 0; k < std::min(u.size(), v.size()); ++k) worst = std::max(worst, static_cast<double>(std::fabs(u[k] - v[k])));
        // exactly: the rhythm is read mid-sample, so rounding in the song position
        // cannot push an edge from one sample to the next (before it was, this
        // measured 0.0008 and FRACTURE's version of the test failed outright)
        check("the same bar moves the same way from anywhere on the timeline", worst < 1e-6, f2s(worst, 9));
    }
    {
        // groove 66 on 1/8 squares: the second of each pair starts two thirds of
        // the way through the pair, i.e. at 0.667 beats, not 0.5
        const Patch g = { {"rhShape", 4}, {"rhDiv", 9}, {"rhGroove", 66.6667f}, {"rhGlide", 0} };
        const auto t = rhythmTrace(g, 1.0, true, 0.0, 120.0);
        std::vector<double> rises;
        for (size_t k = 1; k < t.size(); ++k) if (t[k - 1] < 0.5f && t[k] >= 0.5f) rises.push_back(k * blockSec);
        const double beatSec = 0.5;
        const bool ok = rises.size() >= 2 && std::fabs(rises[0] - beatSec * 2.0 / 3.0) < 0.004
                                           && std::fabs(rises[1] - beatSec) < 0.004;
        check("groove swings every second cycle late, and only that one", ok,
              rises.size() >= 2 ? "rises at " + f2s(rises[0] / beatSec, 3) + " and " + f2s(rises[1] / beatSec, 3) + " beats"
                                : "too few rises");
    }
    {
        // the step pattern, one step per 1/16, read back in order
        Patch st = { {"rhShape", 5}, {"rhDiv", 11}, {"rhGlide", 0} };
        const float want[8] = { 90, 10, 70, 30, 50, 0, 100, 20 };
        for (int k = 0; k < 8; ++k) st["rhStep" + std::to_string(k + 1)] = want[k];
        const auto t = rhythmTrace(st, 1.0, true, 0.0, 120.0);
        double worst = 0.0;
        for (int k = 0; k < 8; ++k){
            const double mid = (k + 0.5) * 0.125;                  // mid-step, in seconds at 120 BPM
            const size_t idx = static_cast<size_t>(mid / blockSec);
            worst = std::max(worst, std::fabs(t[idx] - want[k] / 100.0));
        }
        check("the eight steps play in order, one per division", worst < 1e-6, f2s(worst, 9));
    }
    {
        // stereo phase 180 on a sine: the right channel is the left upside down
        const Patch ph = { {"rhShape", 0}, {"rhDiv", 6}, {"rhPhase", 180}, {"rhGlide", 0} };
        const auto L = rhythmTrace(ph, 1.0, true, 0.0, 120.0, 0);
        const auto R = rhythmTrace(ph, 1.0, true, 0.0, 120.0, 1);
        double worst = 0.0;
        for (size_t k = 0; k < L.size(); ++k) worst = std::max(worst, std::fabs(L[k] + R[k] - 1.0));
        check("phase 180 puts the right channel opposite the left", worst < 1e-6, f2s(worst, 9));
    }
    {
        // random is a function of position: two renders agree, and it moves
        const Patch rn = { {"rhShape", 6}, {"rhDiv", 11}, {"rhGlide", 0} };
        const auto a = rhythmTrace(rn, 1.0, true, 4.0, 100.0), b = rhythmTrace(rn, 1.0, true, 4.0, 100.0);
        double worst = 0.0, lo = 1.0, hi = 0.0;
        for (size_t k = 0; k < a.size(); ++k){
            worst = std::max(worst, static_cast<double>(std::fabs(a[k] - b[k])));
            lo = std::min(lo, static_cast<double>(a[k])); hi = std::max(hi, static_cast<double>(a[k]));
        }
        check("random is the same on every render, and actually random", worst == 0.0 && hi - lo > 0.5,
              "range " + f2s(lo) + " to " + f2s(hi));
    }
    {
        // Free runs at Rate; a division with the transport stopped runs at the
        // division's speed at the host tempo instead of freezing
        auto period = [&](const Patch& p, double bpm){
            const auto t = rhythmTrace(p, 3.0, false, 0.0, bpm);
            std::vector<double> rises;
            for (size_t k = 1; k < t.size(); ++k) if (t[k - 1] < 0.5f && t[k] >= 0.5f) rises.push_back(k * blockSec);
            return rises.size() >= 3 ? (rises.back() - rises.front()) / (rises.size() - 1) : -1.0;
        };
        const double pf = period({ {"rhShape", 4}, {"rhDiv", 0}, {"rhRate", 2}, {"rhGlide", 0} }, 120.0);
        const double ps = period({ {"rhShape", 4}, {"rhDiv", 6}, {"rhGlide", 0} }, 90.0);
        check("Free runs at the rate it says", std::fabs(pf - 0.5) < 0.003, f2s(pf, 4) + " s at 2 Hz");
        check("and a division keeps time with the transport stopped", std::fabs(ps - 60.0 / 90.0) < 0.003,
              f2s(ps, 4) + " s for 1/4 at 90 BPM");
    }
    {
        // glide rounds the edges: where a square has got to 5 ms after it falls
        auto edge = [&](float glide){
            const auto t = rhythmTrace({ {"rhShape", 4}, {"rhDiv", 6}, {"rhGlide", glide} }, 1.0, true, 0.0, 120.0);
            return static_cast<double>(t[static_cast<size_t>((0.25 + 0.005) / blockSec)]);
        };
        const double sharp = edge(0), soft = edge(60);
        check("glide takes the edges off", sharp < 0.01 && soft > 0.8,
              "5 ms after the fall: " + f2s(sharp, 3) + " at 0, " + f2s(soft, 3) + " at 60%");
    }
    {
        // and the rhythm really moves the filter: a 1/4 square, four octaves up
        // from 300 Hz, on noise. The high half of each cycle is brighter.
        const Patch mv = { {"fltFreq", 300}, {"fltReso", 0}, {"clock", 48000}, {"aa", 100}, {"bits", 16},
                           {"rhShape", 4}, {"rhDiv", 6}, {"rhGlide", 0}, {"rhDepth", 4} };
        const auto in = noise(0.3, 2.0, sr, 3);
        Engine e; e.prepare(sr, 128); applyPatch(e, mv);
        const int lat = e.latencySamples();
        auto l = in, r = in; double ppq = 0.0;
        for (int i = 0; i < static_cast<int>(l.size()); i += 128){
            const int m = std::min(128, static_cast<int>(l.size()) - i);
            e.setTransport(true, ppq, 120.0);
            float* io[2] = { l.data() + i, r.data() + i };
            e.process(io, 2, m);
            ppq += m / sr * 2.0;
        }
        double hiP = 0, loP = 0;
        for (int c = 1; c < 3; ++c){
            const int start = static_cast<int>(c * 0.5 * sr) + lat;
            for (int k = 0; k < static_cast<int>(0.2 * sr); ++k){
                const double on = l[static_cast<size_t>(start + 480 + k)];
                const double off = l[static_cast<size_t>(start + static_cast<int>(0.25 * sr) + 480 + k)];
                hiP += on * on; loP += off * off;
            }
        }
        check("Mod +4 oct opens the filter on the high half of the rhythm",
              10.0 * std::log10(hiP / loP) > 6.0, "+" + f2s(10.0 * std::log10(hiP / loP)) + " dB");
    }

    // ------------------------------------------------------ the oversampling
    std::printf("\nOversampling\n");
    {
        // 5 kHz driven hard into the four-pole, with the converter out of the
        // way. At drive 8 the ladder is close to a hard clipper, so its odd
        // harmonics fall away slowly and at a 48 kHz host rate the ones past
        // 24 kHz fold back into the audible band — 35 kHz lands on 13 kHz, 45 on
        // 3 kHz — as tones no analogue filter makes. The true harmonic at 15 kHz
        // is the yardstick. (25 kHz folds to 23 kHz and sits in the half-band
        // filter's transition, where no practical oversampler removes it; it is
        // above hearing, so it is not what is measured here.)
        const Patch hot = { {"clock", 48000}, {"aa", 100}, {"bits", 16},
                            {"fltFreq", 18000}, {"fltReso", 0}, {"fltDrive", 8} };
        auto audibleAlias = [&](int factor){
            Engine e; e.setOversampling(factor); e.prepare(sr, 128); applyPatch(e, hot);
            auto l = sine(5000, 0.8, 0.5, sr), r = l;
            for (int i = 0; i < static_cast<int>(l.size()); i += 128){
                const int m = std::min(128, static_cast<int>(l.size()) - i);
                float* io[2] = { l.data() + i, r.data() + i };
                e.process(io, 2, m);
            }
            const double h = goertzel(l, 15000, sr, 4800);
            const double al = std::max(goertzel(l, 13000, sr, 4800), goertzel(l, 3000, sr, 4800));
            return dBOf(al) - dBOf(h);
        };
        const double a1 = audibleAlias(1), a2 = audibleAlias(2), a4 = audibleAlias(4);
        check("at the host rate, the drive folds harmonics back into the audible band",
              a1 > -20.0, "worst of 3 and 13 kHz " + f2s(a1) + " dB against the 15 kHz harmonic");
        // why 4x and not 2x: printed, not asserted, since a better filter should
        // never fail a test for being better
        std::printf("        (2x leaves %s dB, which is why the default is 4x)\n", f2s(a2).c_str());
        check("four times, the default, puts them 60 dB under the harmonic",
              a4 < -60.0, f2s(a4) + " dB at 4x");
        Engine dflt; dflt.prepare(sr, 128);
        Engine at4; at4.setOversampling(4); at4.prepare(sr, 128);
        check("and four times is what the plugin runs", dflt.latencySamples() == at4.latencySamples());
    }
    {
        // the dry path is delayed by exactly the oversampling latency. The test
        // predicts a half mix from the wet signal and the INPUT, shifted by the
        // latency the plugin reports, as complex numbers (so the filter's own
        // phase is accounted for), and requires the real half mix to match.
        // The dry reference must be the input and not a mix-0 render: the first
        // version used a render, which goes through the same delay line and so
        // agreed with itself even when the delay was a sample out
        const Patch base = { {"clock", 48000}, {"aa", 100}, {"bits", 16},
                             {"fltFreq", 18000}, {"fltReso", 0} };
        auto phasor = [&](const std::vector<float>& x, double f, int from){
            const double w = 2.0 * M_PI * f / sr;
            const int avail = static_cast<int>(x.size()) - from;
            const int n = static_cast<int>(std::floor(avail * f / sr) * sr / f);
            double re = 0.0, im = 0.0;
            for (int k = 0; k < n; ++k){
                re += x[static_cast<size_t>(from + k)] * std::cos(w * (from + k));
                im -= x[static_cast<size_t>(from + k)] * std::sin(w * (from + k));
            }
            return std::pair<double, double>(2.0 * re / n, 2.0 * im / n);
        };
        double worst = 0.0;
        for (double f : { 2000.0, 6000.0, 9000.0 }){
            Patch half = base, wet = base;
            half["mix"] = 50; wet["mix"] = 100;
            Engine probe; probe.prepare(sr, 128);
            const int lat = probe.latencySamples();
            const auto in = sine(f, 0.25, 0.5, sr);
            std::vector<float> late(in.size(), 0.0f);
            for (size_t k = static_cast<size_t>(lat); k < in.size(); ++k) late[k] = in[k - static_cast<size_t>(lat)];
            const auto H = phasor(run(half, in).l, f, 9600);
            const auto W = phasor(run(wet, in).l, f, 9600);
            const auto D = phasor(late, f, 9600);
            const double er = H.first - 0.5 * (W.first + D.first);
            const double ei = H.second - 0.5 * (W.second + D.second);
            worst = std::max(worst, std::sqrt(er * er + ei * ei) / 0.25);
        }
        check("dry and wet stay in step at a half mix", worst < 0.01,
              "worst error " + f2s(dBOf(worst)) + " dB below the signal");
    }

    // -------------------------------------------------------- the hit envelope
    std::printf("\nHit envelope\n");
    {
        // eight drum-like hits, half a second apart: a noise burst that decays
        // with a 60 ms time constant, which is roughly a snare
        const int spacing = static_cast<int>(0.5 * sr);
        std::vector<float> hits(static_cast<size_t>(spacing * 8 + spacing / 2), 0.0f);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        for (int h = 0; h < 8; ++h)
            for (int i = 0; i < spacing; ++i)
                hits[static_cast<size_t>(h * spacing + i)] =
                    static_cast<float>(0.5 * std::exp(-i / (0.060 * sr))) * d(rng);

        const Patch shut = { {"fltFreq", 250}, {"fltReso", 0}, {"clock", 48000},
                             {"aa", 100}, {"bits", 16} };
        Patch open = shut; open["fltEnv"] = 5; open["fltDecay"] = 30;

        {
            Engine e; e.prepare(sr, 128); applyPatch(e, open);
            std::vector<float> l = hits, r = hits;
            for (int i = 0; i < static_cast<int>(l.size()); i += 128){
                const int m = std::min(128, static_cast<int>(l.size()) - i);
                e.setTransport(false, 0.0, 120.0);
                float* io[2] = { l.data() + i, r.data() + i };
                e.process(io, 2, m);
            }
            check("eight hits in, eight triggers", e.hitCount() == 8,
                  std::to_string(e.hitCount()) + " triggers");
        }
        {
            // a sustained tone is not a string of hits
            Engine e; e.prepare(sr, 128); applyPatch(e, open);
            auto l = sine(220, 0.4, 2.0, sr), r = l;
            for (int i = 0; i < static_cast<int>(l.size()); i += 128){
                const int m = std::min(128, static_cast<int>(l.size()) - i);
                float* io[2] = { l.data() + i, r.data() + i };
                e.process(io, 2, m);
            }
            check("a sustained tone triggers once at most", e.hitCount() <= 1,
                  std::to_string(e.hitCount()) + " triggers");
        }

        Engine probe; probe.prepare(sr, 128); applyPatch(probe, open);
        const int lat = probe.latencySamples();
        const auto a = run(shut, hits), b = run(open, hits);
        auto windowPower = [&](const std::vector<float>& x, double fromMs, double toMs){
            double p = 0.0; int n = 0;
            for (int h = 1; h < 8; ++h){            // skip the first, while the envelopes settle
                const int base = h * spacing + lat;
                for (int i = base + static_cast<int>(fromMs * sr / 1000.0);
                     i < base + static_cast<int>(toMs * sr / 1000.0); ++i){
                    p += static_cast<double>(x[static_cast<size_t>(i)]) * x[static_cast<size_t>(i)]; ++n;
                }
            }
            return 10.0 * std::log10(std::max(1e-20, p / std::max(1, n)));
        };
        const double attackGain = windowPower(b.l, 1, 10) - windowPower(a.l, 1, 10);
        const double tailGain = windowPower(b.l, 250, 400) - windowPower(a.l, 250, 400);
        check("each hit opens the filter: the attack comes through brighter",
              attackGain > 10.0, "+" + f2s(attackGain) + " dB in the first 10 ms");
        check("and it closes again: the tail is the resting filter",
              std::fabs(tailGain) < 1.0, f2s(tailGain) + " dB, 250-400 ms after");

        Patch wild = open; wild["fltReso"] = 100; wild["fltDrive"] = 8;
        wild["fltEnv"] = 6; wild["fltDecay"] = 5; wild["inGain"] = 12;
        const auto w = run(wild, hits);
        check("wide open, fast and resonant stays finite and bounded",
              w.bad == 0 && w.peak <= 1.0, f2s(w.peak, 3));
    }
    {
        // When each trigger lands, straight from the detector. A flam is two
        // hits 20 to 30 ms apart; the first version held for 40 ms and then
        // fired late, in the first hit's tail, opening the filter on nothing
        auto triggers = [&](const std::vector<double>& x){
            HitEnv h; h.prepare(sr); h.setDecay(40);
            std::vector<double> at; long last = 0;
            for (size_t i = 0; i < x.size(); ++i){
                h.process(x[i]);
                if (h.hits != last){ last = h.hits; at.push_back(1000.0 * static_cast<double>(i) / sr); }
            }
            return at;
        };
        std::mt19937 rng(3);
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        auto snare = [&](std::vector<double>& v, double ms, double amp){
            const size_t s = static_cast<size_t>(ms * sr / 1000.0);
            for (size_t i = 0; i < static_cast<size_t>(0.3 * sr) && s + i < v.size(); ++i)
                v[s + i] += amp * std::exp(-static_cast<double>(i) / (0.060 * sr)) * d(rng);
        };
        std::string bad;
        const double flams[][3] = { { 20, 0.2, 0.5 }, { 30, 0.2, 0.5 }, { 25, 0.5, 0.5 } };
        for (const auto& f : flams){
            std::vector<double> v(static_cast<size_t>(0.5 * sr), 0.0);
            snare(v, 100.0, f[1]); snare(v, 100.0 + f[0], f[2]);
            const auto at = triggers(v);
            const bool ok = at.size() == 2 && std::fabs(at[0] - 100.0) < 1.0 && at[1] >= 100.0 + f[0]
                            && at[1] < 100.0 + f[0] + 4.0;
            if (!ok){
                bad += " [" + f2s(f[0], 0) + " ms flam:";
                for (double t : at) bad += " " + f2s(t - 100.0, 1);
                bad += "]";
            }
        }
        check("a flam is two triggers, each within 4 ms of its hit", bad.empty(), bad);

        // the audition loop's eleven kicks and snares each trigger within 2 ms,
        // and the sixteenth hats between them do not all open the filter
        const auto loop = audition::makeLoop(sr, false);
        std::vector<double> mono(loop.l.size());
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5 * (loop.l[i] + loop.r[i]);
        const auto at = triggers(mono);
        const double beat16 = 60000.0 / 90.0 / 4.0;
        const int drums[] = { 0, 4, 7, 10, 12, 16, 20, 23, 26, 28, 30 };   // kicks and snares
        int found = 0;
        for (int k : drums)
            for (double t : at) if (t >= k * beat16 && t < k * beat16 + 2.0){ ++found; break; }
        check("every kick and snare in the loop triggers, on time, and most hats do not",
              found == 11 && at.size() <= 16,
              std::to_string(found) + " of 11 drums, " + std::to_string(at.size()) + " triggers in all");
    }
    {
        // the decay knob is a time constant: 1/e of the way down after that long
        HitEnv h; h.prepare(sr); h.setDecay(50);
        int peakAt = -1;
        std::vector<double> v;
        for (int i = 0; i < static_cast<int>(sr); ++i){
            const double x = i < 480 ? 0.5 * ((i * 7919) % 13 - 6) / 6.0 : 0.0;
            v.push_back(h.process(x));
            if (peakAt < 0 && v.back() >= 1.0) peakAt = i;
        }
        const double after = peakAt >= 0 ? v[static_cast<size_t>(peakAt + static_cast<int>(0.050 * sr))] : -1.0;
        check("Decay is the time constant it says", std::fabs(after - std::exp(-1.0)) < 0.03,
              f2s(after, 3) + " after 50 ms, want 0.368");
    }

    // ------------------------------------------------------ what the panel dims
    std::printf("\nDimmed controls\n");
    {
        // every control the panel dims is moved end to end, in each of these
        // states, and the output must not change by a single bit
        const auto in = noise(0.3, 0.25, sr, 5);
        const std::vector<Patch> states = {
            {}, { {"mix", 0} }, { {"fltMix", 0} }, { {"fltEnv", 3} },
            { {"rhDepth", 3}, {"rhDiv", 0} }, { {"rhDepth", -2}, {"rhShape", 5} },
            { {"dust", 30} }, { {"machine", 1}, {"swing", 60} }
        };
        int claims = 0; std::string lies;
        const Params& PP = Params::get();
        for (size_t si = 0; si < states.size(); ++si){
            Engine probe; probe.prepare(sr, 128); applyPatch(probe, states[si]);
            std::vector<float> vals(static_cast<size_t>(PP.count()));
            for (int i = 0; i < PP.count(); ++i) vals[static_cast<size_t>(i)] = probe.getParam(i);
            const auto base = run(states[si], in, sr, 128, true, 96.0);
            for (const Idle& d : idleControls(vals.data())){
                const ParamInfo& info = PP[d.param];
                const float cur = vals[static_cast<size_t>(d.param)];
                float other;
                if (info.kind == Kind::Float) other = (cur - info.min) > (info.max - cur) ? info.min : info.max;
                else other = cur >= info.max ? info.min : cur + 1.0f;
                Patch moved = states[si];
                moved[info.id] = other;
                const auto t = run(moved, in, sr, 128, true, 96.0);
                ++claims;
                for (size_t k = 0; k < in.size(); ++k)
                    if (t.l[k] != base.l[k] || t.r[k] != base.r[k]){
                        lies += " [state " + std::to_string(si) + ": " + info.id + "]"; break;
                    }
            }
        }
        check("every dimmed control really does nothing (" + std::to_string(claims) + " claims checked)",
              lies.empty(), lies.empty() ? "" : "audible:" + lies);
    }

    // ------------------------------------------------------------ the presets
    std::printf("\nPresets\n");
    {
        // no two presets may sound the same: a preset that renders identically
        // to another is a menu entry that lies about being a choice. Found by
        // listening renders, after the linear default made "Twelve bit,
        // straight" a copy of Init
        const auto in = noise(0.3, 0.2, sr, 9);
        std::vector<std::vector<float>> outs;
        std::vector<std::string> names;
        for (const auto& p : presets()){
            Patch patch;
            for (const auto& kv : p.values) patch[kv.first] = kv.second;
            outs.push_back(run(patch, in, sr, 128, true, 90.0).l);
            names.push_back(p.name);
        }
        std::string same;
        for (size_t a = 0; a < outs.size(); ++a)
            for (size_t b = a + 1; b < outs.size(); ++b)
                if (outs[a] == outs[b]) same += " [" + names[a] + " = " + names[b] + "]";
        check("every preset sounds different from every other (" + std::to_string(outs.size()) + ")",
              same.empty(), same);

        // and every preset, over the audition loop (drums at 90 BPM, transport
        // running), sits within 6 dB of the dry loop and off the ceiling. The
        // renders found one of these 14.5 dB down; this keeps it from recurring
        const auto loop = audition::makeLoop(sr, false);
        auto leftRms = [](const std::vector<float>& x, size_t from, size_t n){
            double acc = 0.0, pk = 0.0;
            for (size_t i = from; i < from + n; ++i){ acc += static_cast<double>(x[i]) * x[i]; pk = std::max(pk, static_cast<double>(std::fabs(x[i]))); }
            return std::make_pair(std::sqrt(acc / static_cast<double>(n)), pk);
        };
        const double dry = leftRms(loop.l, 0, loop.l.size()).first;
        std::string out;
        for (const auto& p : presets()){
            Patch patch;
            for (const auto& kv : p.values) patch[kv.first] = kv.second;
            // the grid, and so the latency, is settled by the first block
            Engine e; e.prepare(sr, 128); applyPatch(e, patch);
            std::vector<float> z(128, 0.0f); float* io[2] = { z.data(), z.data() };
            e.process(io, 1, 128);
            const size_t lat = static_cast<size_t>(e.latencySamples());
            // run on past the end by the latency and drop it from the front, as a host would
            std::vector<float> padded = loop.l;
            padded.resize(loop.l.size() + lat, 0.0f);
            const auto r = run(patch, padded, sr, 128, true, 90.0);
            const auto [rms, pk] = leftRms(r.l, lat, loop.l.size());
            const double rel = dBOf(rms / dry);
            if (std::fabs(rel) > 6.0 || pk > 0.95) out += std::string(" [") + p.name + " " + f2s(rel, 1) + " dB, peak " + f2s(pk, 2) + "]";
        }
        check("every preset sits within 6 dB of the dry loop, off the ceiling", out.empty(), out);
    }

    std::printf("\nZipper noise\n");
    {
        struct Case { const char* name; Patch base; const char* id; float from, to; };
        const std::vector<Case> cases = {
            { "Cutoff", { {"fltFreq", 300} }, "fltFreq", 300, 12000 },
            { "Cutoff, resonant band pass", { {"fltShape", 1}, {"fltReso", 60} }, "fltFreq", 300, 6000 },
            { "Resonance", { {"fltFreq", 1500} }, "fltReso", 0, 90 },
            { "Clock", {}, "clock", 8000, 48000 },
            { "Tune", {}, "tune", -12, 12 },
            { "Output", {}, "outGain", -24, 6 },
            { "Mix", {}, "mix", 0, 100 },
            { "Filter mix", { {"fltFreq", 500} }, "fltMix", 0, 100 },
        };
        const Params& PP = Params::get();
        std::string worstName; double worst = -99.0;
        for (const auto& c : cases){
            double r[2];
            for (int pass = 0; pass < 2; ++pass){
                const int blk = pass ? 512 : 16;
                Engine e; e.prepare(sr, 512); applyPatch(e, c.base);
                const int n = 96000;
                std::vector<float> L(n), R(n);
                for (int i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(0.25 * std::sin(2.0 * M_PI * 440.0 * i / sr));
                const ParamInfo& pi = PP[PP.index(c.id)];
                for (int i = 0; i < n; i += blk){
                    const float t = static_cast<float>(i) / n;
                    e.setParam(PP.index(c.id), Params::fromNorm(pi, Params::toNorm(pi, c.from) + (Params::toNorm(pi, c.to) - Params::toNorm(pi, c.from)) * t));
                    float* io[2] = { L.data() + i, R.data() + i };
                    e.process(io, 2, std::min(blk, n - i));
                }
                r[pass] = inharmonicDb(L, 440.0, sr);
            }
            if (r[1] - r[0] > worst){ worst = r[1] - r[0]; worstName = c.name; }
        }
        check("no control zippers when a host moves it once a block", worst < 2.0,
              "worst " + worstName + " +" + f2s(worst, 1) + " dB over a 16-sample-block sweep");
    }

    std::printf("\nFeel\n");
    {
        // mix at zero leaves only the timing section in circuit, which is what
        // makes the offsets measurable to the sample
        const double bpm = 90.0;
        const double beat = 60.0 / bpm * sr;              // samples per beat
        const double step = beat / 4.0;                   // sixteenths
        // half a second of tail, or the last hit is still inside the delay line
        // when the render ends and the count comes up one short
        std::vector<float> in(static_cast<size_t>(beat * 4 + sr * 0.5), 0.0f);
        for (int s = 0; s < 16; ++s){
            const int at = static_cast<int>(std::round(s * step));
            if (at < static_cast<int>(in.size())) in[static_cast<size_t>(at)] = 1.0f;
        }
        Engine probe; probe.prepare(sr, 128);
        const int base = probe.latencySamples();

        Patch straight = { {"mix", 0}, {"swing", 50}, {"grid", 1} };
        const auto s0 = run(straight, in, sr, 128, true, bpm);
        const auto p0 = peaks(s0.l, 0.2);
        bool aligned = p0.size() >= 12;
        double worst = 0.0;
        for (size_t i = 0; i < p0.size() && aligned; ++i){
            const double expect = i * step + base;
            worst = std::max(worst, std::fabs(p0[i] - expect));
        }
        check("straight sixteenths come out on the grid",
              aligned && worst < 0.002 * sr, std::to_string(p0.size()) + " hits, worst "
              + f2s(worst / sr * 1000.0) + " ms");

        Patch swung = { {"mix", 0}, {"swing", 66.667f}, {"grid", 1} };
        const auto s1 = run(swung, in, sr, 128, true, bpm);
        const auto p1 = peaks(s1.l, 0.2);
        const double expectedOffset = (66.667 / 100.0 - 0.5) * 2.0 * step;
        double worstOn = 0.0, worstOff = 0.0;
        const bool enough = p1.size() >= 14;
        for (size_t i = 0; i < p1.size() && enough; ++i){
            const double expect = i * step + base + (i % 2 ? expectedOffset : 0.0);
            const double err = std::fabs(p1[i] - expect);
            (i % 2 ? worstOff : worstOn) = std::max(i % 2 ? worstOff : worstOn, err);
        }
        check("swing at 66% delays the off-beats by a third of a step, and only them",
              enough && worstOn < 0.002 * sr && worstOff < 0.002 * sr,
              "offset " + f2s(expectedOffset / sr * 1000.0) + " ms, error on "
              + f2s(worstOn / sr * 1000.0) + " / off " + f2s(worstOff / sr * 1000.0) + " ms");
        // moving a delay pointer re-reads audio unless it is moved in the right
        // place; when it is wrong every hit comes out twice, so count them
        check("and nothing is played twice on the way",
              p1.size() == 16, std::to_string(p1.size()) + " hits from 16 inputs");

        Patch pushed = { {"mix", 0}, {"swing", 50}, {"push", 10} };
        const auto s2 = run(pushed, in, sr, 128, true, bpm);
        const auto p2 = peaks(s2.l, 0.2);
        double worstPush = 0.0;
        bool enough2 = p2.size() >= 12;
        for (size_t i = 0; i < p2.size() && enough2; ++i)
            worstPush = std::max(worstPush, std::fabs(p2[i] - (i * step + base + 0.010 * sr)));
        check("push moves everything by the milliseconds it says",
              enough2 && worstPush < 0.002 * sr, f2s(worstPush / sr * 1000.0) + " ms error");

        Patch stopped = { {"mix", 0}, {"swing", 75} };
        const auto s3 = run(stopped, in, sr, 128, false, bpm);
        const auto p3 = peaks(s3.l, 0.2);
        double worstStopped = 0.0;
        for (size_t i = 0; i < p3.size(); ++i)
            worstStopped = std::max(worstStopped, std::fabs(p3[i] - (i * step + base)));
        check("with the transport stopped there is no grid, so nothing swings",
              p3.size() >= 12 && worstStopped < 0.002 * sr,
              f2s(worstStopped / sr * 1000.0) + " ms");
    }
    {
        // the delay the host is told about has to be the delay it gets
        Engine e; e.prepare(sr, 128);
        Patch p = { {"mix", 0}, {"swing", 50}, {"push", 0} };
        applyPatch(e, p);
        const int lat = e.latencySamples();
        const auto in = noise(0.3, 0.2, sr, 7);
        auto out = in;
        for (int i = 0; i + 128 <= static_cast<int>(out.size()); i += 128){
            e.setTransport(false, 0, 120);
            float* io[2] = { out.data() + i, out.data() + i };
            e.process(io, 1, 128);
        }
        double worst = 0.0;
        for (int i = lat + 64; i + 128 < static_cast<int>(out.size()); ++i)
            worst = std::max(worst, std::fabs(static_cast<double>(out[static_cast<size_t>(i)]) - in[static_cast<size_t>(i - lat)]));
        check("reported latency is the real latency (" + std::to_string(lat) + " samples)",
              lat > 0 && worst < 1e-6, "worst sample error " + f2s(worst, 9));
    }

    // ------------------------------------------------------------------- dust
    std::printf("\nDust\n");
    {
        Patch p = { {"dust", 80}, {"mix", 100} };
        const auto a = run(p, std::vector<float>(static_cast<size_t>(sr * 0.3), 0.0f));
        const auto b = run(p, std::vector<float>(static_cast<size_t>(sr * 0.3), 0.0f));
        double worst = 0.0;
        for (size_t i = 0; i < a.l.size(); ++i)
            worst = std::max(worst, std::fabs(static_cast<double>(a.l[i]) - b.l[i]));
        check("a seeded render is bit-identical twice over", worst == 0.0, f2s(worst, 12));
        check("dust is audible on silence", totalPower(a.l, 4800) > 1e-9,
              f2s(10.0 * std::log10(std::max(1e-14, totalPower(a.l, 4800)))) + " dB");
        const auto none = run({ {"dust", 0} }, std::vector<float>(static_cast<size_t>(sr * 0.2), 0.0f));
        check("and silent when it is off", totalPower(none.l, 2400) < 1e-14);
    }

    // ------------------------------------------------------------- the whole box
    std::printf("\nWhole box\n");
    {
        const auto dry = noise(0.4, 0.3, sr, 11);
        const auto r = run({}, dry);
        check("the default patch is finite", r.bad == 0, std::to_string(r.bad) + " non-finite");
        check("the default patch is audible", totalPower(r.l, 4800) > 1e-4,
              f2s(10.0 * std::log10(totalPower(r.l, 4800))) + " dB");
        check("the default patch stays inside full scale", r.peak <= 1.0, f2s(r.peak, 3));
    }
    {
        Patch hard = { {"inGain", 18}, {"tune", -12}, {"bits", 4}, {"compand", 100},
                       {"aa", 0}, {"fltFreq", 400}, {"fltReso", 100}, {"fltDrive", 8},
                       {"dust", 100}, {"swing", 80}, {"push", -25} };
        const auto r = run(hard, noise(0.5, 0.4, sr, 13), sr, 64, true, 82.0);
        check("everything at once stays finite and bounded",
              r.bad == 0 && r.peak <= 1.0, "peak " + f2s(r.peak, 3));
    }
    {
        const auto in = sine(220, 0.5, 0.2, sr);
        Engine e; e.prepare(sr, 128);
        applyPatch(e, { {"mono", 1} });
        std::vector<float> l = in, rr(in.size(), 0.0f);       // hard left
        for (int i = 0; i + 128 <= static_cast<int>(l.size()); i += 128){
            e.setTransport(false, 0, 120);
            float* io[2] = { l.data() + i, rr.data() + i };
            e.process(io, 2, 128);
        }
        double diff = 0.0;
        for (size_t i = 0; i < l.size(); ++i) diff = std::max(diff, std::fabs(static_cast<double>(l[i]) - rr[i]));
        check("mono sums both channels to the same thing", diff < 1e-6, f2s(diff, 9));
    }
    for (double rate : { 44100.0, 48000.0, 96000.0 }){
        const auto r = run({ {"dust", 40}, {"swing", 62}, {"bits", 10} },
                           noise(0.4, 0.25, rate, 17), rate, 64, true, 93.0);
        check(f2s(rate / 1000.0, 1) + " kHz renders finite and bounded",
              r.bad == 0 && r.peak <= 1.0 && totalPower(r.l, 2400) > 1e-5,
              "peak " + f2s(r.peak, 3));
    }
    for (int block : { 16, 64, 512, 1024 }){
        const auto r = run({ {"swing", 66} }, noise(0.4, 0.2, sr, 19), sr, block, true, 90.0);
        check("block size " + std::to_string(block), r.bad == 0 && r.peak <= 1.0);
    }

    std::printf("\n%d assertions passed, %zu failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.c_str());
    return failures.empty() ? 0 : 1;
}
