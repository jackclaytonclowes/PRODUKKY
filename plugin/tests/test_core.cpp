// test_core.cpp — what the browser suite (tests/fx.mjs) checks, plus the things
// only a native build can check: that the port matches the original arithmetic,
// that the reported latency is the real latency, that the crossover sums flat,
// and that a bounce is reproducible.
//
//   npm run test:core
#include "FractureCore.h"
#include "Relevance.h"
#include "FactoryPresets.h"
#include "History.h"
#include "HarmonicTable.h"
#include "Harmonics.h"
#include "../../tools/audition/common.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <random>
#include <map>
#include <complex>
#include <tuple>

using namespace fracture;

static int passed = 0;
static std::vector<std::string> failures;
static void check(const std::string& name, bool ok, const std::string& detail = ""){
    if (ok){ ++passed; std::printf("  ok    %s%s\n", name.c_str(), std::getenv("VERBOSE") && !detail.empty() ? (" — " + detail).c_str() : ""); }
    else {
        failures.push_back(name + (detail.empty() ? "" : " — " + detail));
        std::printf("  FAIL  %s%s\n", name.c_str(), detail.empty() ? "" : (" — " + detail).c_str());
    }
}
static std::string f2s(double v, int p = 4){
    char b[64]; std::snprintf(b, sizeof b, "%.*f", p, v); return b;
}

// ------------------------------------------------------------------ rendering
struct Result { double peak = 0, rms = 0; long bad = 0; std::vector<float> left; };

struct Patch { std::map<std::string, float> v; };

static void applyPatch(Engine& e, const Patch& p){
    const Params& P = Params::get();
    for (int i = 0; i < P.count(); ++i) e.setParam(i, P[i].def);
    for (const auto& kv : p.v){
        const int i = P.index(kv.first);
        if (i >= 0) e.setParam(i, kv.second);
    }
}

// noise in, the whole chain, out — the first 30 ms is skipped because the
// parameter ramps and the oversampling filters are still filling
static Result render(const Patch& patch, double seconds = 0.3, double sr = 48000.0,
                     int block = 128, unsigned seed = 1, bool keepLeft = false){
    Engine e;
    e.prepare(sr, block);
    applyPatch(e, patch);
    e.seedFrom(0);
    const int n = static_cast<int>(sr * seconds);
    std::vector<float> L(n), R(n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    for (int i = 0; i < n; ++i){ L[i] = d(rng); R[i] = d(rng); }
    for (int i = 0; i < n; i += block){
        const int m = std::min(block, n - i);
        float* io[2] = { L.data() + i, R.data() + i };
        e.process(io, 2, m);
    }
    Result r;
    const int skip = static_cast<int>(sr * 0.03);
    double sum = 0.0; long count = 0;
    for (int ch = 0; ch < 2; ++ch){
        const std::vector<float>& v = ch == 0 ? L : R;
        for (int i = skip; i < n; ++i){
            const float x = v[i];
            if (!std::isfinite(x)){ ++r.bad; continue; }
            r.peak = std::max(r.peak, static_cast<double>(std::fabs(x)));
            sum += static_cast<double>(x) * x; ++count;
        }
    }
    r.rms = std::sqrt(sum / std::max(1L, count));
    if (keepLeft) r.left = L;
    return r;
}

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

// ------------------------------------------------------- shaper parity vs JS
static double db(double gain){ return 20.0 * std::log10(std::max(gain, 1.0e-12)); }

// ----------------------------------------------------------- anti-aliasing
// Shapers.h averages each anti-aliased shaper over the step between samples,
// using its antiderivative. Three things have to hold: every antiderivative
// really is one, the oversampler hands the stateful stages their samples in
// order (it once did not, under GCC), and the result is less aliasing in the
// audible band without a different sound underneath.

// energy below topHz that is not a harmonic of f0, against the harmonics, in
// dB. A Blackman-Harris window keeps its own leakage near -90 dB, so the
// measure can see a shaper get cleaner rather than hitting its own floor
static double aliasDb(const std::vector<float>& x, double f0, double sr, double topHz){
    const size_t n = 65536;
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i){
        const double t = 2.0 * M_PI * i / (n - 1);
        a[i] = x[x.size() - n + i] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t));
    }
    fftInPlace(a);
    double harm = 0.0, junk = 0.0; const double binHz = sr / n;
    for (size_t k = 3; k < n / 2; ++k){
        const double f = k * binHz, p = std::norm(a[k]);
        const double off = std::fabs(f / f0 - std::round(f / f0)) * f0;
        if (off < 8.0 * binHz) harm += p; else if (f > 30.0 && f < topHz) junk += p;
    }
    return 10.0 * std::log10(std::max(junk, 1e-30) / std::max(harm, 1e-30));
}
static std::vector<float> sineThrough(const Patch& patch, double f0, bool antialias, double sr = 48000.0){
    Engine e; e.prepare(sr, 256); applyPatch(e, patch); e.setAntialiasing(antialias); e.seedFrom(0);
    const int n = 65536 + static_cast<int>(sr * 0.5);
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * f0 * i / sr));
    for (int i = 0; i < n; i += 256){ float* io[2] = { L.data() + i, R.data() + i }; e.process(io, 2, std::min(256, n - i)); }
    return L;
}

// ------------------------------------------------------------- the Table mode
// HarmonicTable.h: bars drawn as harmonics, turned into a Chebyshev curve. The
// claim is exact, so the test is exact: a full-scale cosine through the curve
// comes out as the drawn harmonics and nothing else.
static void testHarmonicTable(){
    const int N = 4096;
    auto spectrum = [&](const TableCurve& c, double amp, std::vector<double>& mag){
        mag.assign(33, 0.0);
        for (int k = 1; k <= 32; ++k){
            std::complex<double> g = 0.0;
            for (int i = 0; i < N; ++i){
                const double t = 2.0 * M_PI * i / N;
                g += c.f(amp * std::cos(t)) * std::polar(1.0, -k * t);
            }
            mag[k] = 2.0 * std::abs(g) / N;
        }
    };
    {   // 1, 3 and 5 at 100, 50 and 35 %: Serum's example
        double bars[tableHarmonics] = {}; bars[0] = 1.0; bars[2] = 0.5; bars[4] = 0.35;
        TableCurve c; c.build(bars);
        std::vector<double> m; spectrum(c, 1.0, m);
        const double sum = 1.85;
        double worst = 0.0;
        for (int k = 1; k <= 32; ++k){
            const double want = k <= tableHarmonics ? bars[k - 1] / sum : 0.0;
            worst = std::max(worst, std::fabs(m[k] - want));
        }
        check("Table: a full-scale sine comes out as exactly the harmonics drawn", worst < 1.0e-9,
              "worst error " + f2s(worst, 12) + " (1, 3, 5 at " + f2s(m[1], 3) + ", " + f2s(m[3], 3) + ", " + f2s(m[5], 3) + ")");
    }
    {   // any drawing: silence stays silent, and the output stays bounded
        std::mt19937 rng(11);
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        double worstZero = 0.0, worstPeak = 0.0, worstSlope = 0.0, worstJump = 0.0;
        for (int trial = 0; trial < 200; ++trial){
            double bars[tableHarmonics];
            for (auto& b : bars) b = d(rng);
            TableCurve c; c.build(bars);
            worstZero = std::max(worstZero, std::fabs(c.f(0.0)));
            for (double x = -3.0; x <= 3.0; x += 0.001){
                worstPeak = std::max(worstPeak, std::fabs(c.f(x)));
                const double h = 1.0e-6;
                if (std::fabs(std::fabs(x) - 1.0) > 1e-3){
                    const double slope = (c.F(x + h) - c.F(x - h)) / (2.0 * h);
                    worstSlope = std::max(worstSlope, std::fabs(slope - c.f(x)));
                }
                worstJump = std::max(worstJump, std::fabs(c.F(x + 0.001) - c.F(x)) - 2.001 * 0.001);
            }
        }
        check("Table: silence in, silence out, whatever is drawn", worstZero < 1.0e-12, f2s(worstZero, 15));
        check("Table: no drawing goes past twice full scale (the even harmonics' offset, which the DC blocker takes)",
              worstPeak <= 2.0 + 1e-9, "peak " + f2s(worstPeak, 4));
        check("Table: its antiderivative differentiates back to it, and has no jumps",
              worstSlope < 1.0e-5 && worstJump < 1.0e-9, "slope " + f2s(worstSlope, 8) + ", jump " + f2s(worstJump, 12));
    }
    {   // morphing: 0 is frame 1, 1 is frame 4, the thirds land on 2 and 3
        HarmonicFrames fr;
        for (int f = 0; f < tableFrames; ++f) for (int k = 0; k < tableHarmonics; ++k) fr.bars[f][k] = (f + 1) * 0.1 + k * 0.01;
        double b[tableHarmonics]; bool ok = true;
        const double at[] = { 0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0 };
        for (int f = 0; f < tableFrames; ++f){
            fr.blend(at[f], b);
            for (int k = 0; k < tableHarmonics; ++k) ok = ok && std::fabs(b[k] - fr.bars[f][k]) < 1e-12;
        }
        fr.blend(0.5, b);
        for (int k = 0; k < tableHarmonics; ++k) ok = ok && std::fabs(b[k] - 0.5 * (fr.bars[1][k] + fr.bars[2][k])) < 1e-12;
        check("Table: Position lands on each frame and blends between neighbours", ok);
    }
}

// the Table mode through the whole engine: oversampling, DC blockers, auto gain
static void testTableInEngine(){
    const double sr = 48000.0, f0 = 220.0;
    const float table = static_cast<float>(Mode::Table);
    auto harmonicLevel = [&](const std::vector<float>& x, int h, size_t from, size_t len){
        const double w = 2.0 * M_PI * f0 * h / sr;
        std::complex<double> g = 0.0;
        for (size_t i = from; i < from + len; ++i) g += static_cast<double>(x[i]) * std::polar(1.0, -w * static_cast<double>(i));
        return 2.0 * std::abs(g) / static_cast<double>(len);
    };
    {   // frame 2 is 1, 3 and 5 at 100, 50 and 35 %; a sine that fills the curve
        // (0.5 in, drive 2) comes out with that recipe
        Patch p; p.v = { { "bands", 0 }, { "m0a", table }, { "d0a", 2 }, { "mx0", 100 }, { "tblPos", 100.0f / 3.0f } };
        const auto y = sineThrough(p, f0, true);
        const size_t len = 48000, from = y.size() - len;
        const double h1 = harmonicLevel(y, 1, from, len), h3 = harmonicLevel(y, 3, from, len), h5 = harmonicLevel(y, 5, from, len);
        const double h2 = harmonicLevel(y, 2, from, len);
        const double e3 = db(h3 / h1) - db(0.5), e5 = db(h5 / h1) - db(0.35);
        check("Table in the engine: a sine that fills the curve comes out as the drawn 1, 3 and 5",
              std::fabs(e3) < 0.3 && std::fabs(e5) < 0.3 && db(h2 / h1) < -60.0,
              "3rd " + f2s(db(h3 / h1), 2) + " dB (want " + f2s(db(0.5), 2) + "), 5th " + f2s(db(h5 / h1), 2)
                  + " dB (want " + f2s(db(0.35), 2) + "), 2nd " + f2s(db(h2 / h1), 1) + " dB");
    }
    {   // an LFO on Position: the 3rd harmonic swells and fades as it sweeps
        // from frame 1 (none) towards frame 2 (half the fundamental)
        Patch p; p.v = { { "bands", 0 }, { "m0a", table }, { "d0a", 2 }, { "mx0", 100 }, { "tblPos", 0 },
                         { "mS0", 1 }, { "mD0", static_cast<float>(1 + std::distance(Params::get().dests().begin(),
                             std::find(Params::get().dests().begin(), Params::get().dests().end(), Ids::get().tblPos))) },
                         { "mA0", 33 }, { "l1Rate", 4.0f }, { "l1Shape", 1 } };
        const auto y = sineThrough(p, f0, true);
        double lo = 1e9, hi = 0.0;
        const size_t win = 2400;                                   // 50 ms windows across the last second
        for (size_t from = y.size() - 48000; from + win <= y.size(); from += win){
            const double r = harmonicLevel(y, 3, from, win) / harmonicLevel(y, 1, from, win);
            lo = std::min(lo, r); hi = std::max(hi, r);
        }
        check("Table in the engine: an LFO on Position makes the harmonics wobble",
              db(hi) - db(lo) > 12.0, "3rd harmonic moves between " + f2s(db(lo), 1) + " and " + f2s(db(hi), 1) + " dB");
        // and it glides rather than stepping once a block. The same render in
        // 16-sample blocks (where a step is too small to hear) and in 512-sample
        // ones should differ only smoothly; a step at each block edge is a click,
        // which shows in the top end of the difference. Measured on the slope of
        // the difference against the slope of the signal. Gliding measures near
        // -38 dB (the LFO is read once a block, so the glide draws it in straight
        // lines); stepping, near -19
        auto renderAt = [&](int block){
            Engine e; e.prepare(sr, block); applyPatch(e, p); e.seedFrom(0);
            const int n = 48000; std::vector<float> L(n), R(n);
            for (int i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * f0 * i / sr));
            for (int i = 0; i < n; i += block){ float* io[2] = { L.data() + i, R.data() + i }; e.process(io, 2, std::min(block, n - i)); }
            return L;
        };
        const auto fine = renderAt(16), host = renderAt(512);
        double dd = 0.0, ss = 0.0;
        for (size_t i = 12000; i < fine.size(); ++i){
            const double d1 = (host[i] - fine[i]) - (host[i - 1] - fine[i - 1]);
            const double s1 = fine[i] - fine[i - 1];
            dd += d1 * d1; ss += s1 * s1;
        }
        const double clicks = 10.0 * std::log10(std::max(dd, 1e-30) / ss);
        check("Table in the engine: Position under an LFO glides, with no steps at host block edges",
              clicks < -30.0, "block-edge difference " + f2s(clicks, 1) + " dB under the signal");
    }
    {   // silence stays silent, even with the even harmonics drawn
        Patch p; p.v = { { "bands", 0 }, { "m0a", table }, { "d0a", 8 }, { "mx0", 100 }, { "tblPos", 66.67f } };
        Engine e; e.prepare(sr, 256); applyPatch(e, p); e.seedFrom(0);
        std::vector<float> L(48000, 0.0f), R(48000, 0.0f);
        for (int i = 0; i < 48000; i += 256){ float* io[2] = { L.data() + i, R.data() + i }; e.process(io, 2, std::min(256, 48000 - i)); }
        double peak = 0.0; for (float v : L) peak = std::max(peak, static_cast<double>(std::fabs(v)));
        check("Table in the engine: silence in, silence out, with the 2nd and 4th drawn", peak == 0.0, f2s(peak, 12));
    }
}

// Tube and Soft must be different sounds. They were not: Tube's first formula
// reduced to tanh(x), Soft's, exactly. A symmetric curve makes only odd
// harmonics; Tube caps its positive half lower, so the two halves bend
// differently and it adds the 2nd.
static void testTubeIsNotSoft(){
    const double sr = 48000.0, f0 = 220.0;
    auto harmonicDb = [&](const std::vector<float>& x, int h){         // re the fundamental
        auto level = [&](int k){
            const double w = 2.0 * M_PI * f0 * k / sr;
            std::complex<double> g = 0.0;
            for (size_t i = x.size() - 48000; i < x.size(); ++i)
                g += static_cast<double>(x[i]) * std::polar(1.0, -w * static_cast<double>(i));
            return std::abs(g);
        };
        return db(level(h) / level(1));
    };
    for (double drive : { 1.5, 4.0, 12.0 }){
        Patch soft, tube;
        soft.v = { { "bands", 0 }, { "d0a", static_cast<float>(drive) }, { "m0a", 0 }, { "mx0", 100 } };
        tube.v = soft.v; tube.v["m0a"] = 1;
        const auto a = sineThrough(soft, f0, true), b = sineThrough(tube, f0, true);
        const double s2 = harmonicDb(a, 2), t2 = harmonicDb(b, 2);
        check("Tube is not Soft at drive " + f2s(drive, 1) + ": it adds the 2nd harmonic, Soft does not",
              t2 > -32.0 && s2 < -80.0 && t2 - s2 > 50.0,
              "2nd harmonic: Tube " + f2s(t2, 1) + " dB, Soft " + f2s(s2, 1) + " dB");
    }
}

static void testAntialiasing(){
    // 1. F' = f for every mode that claims an antiderivative, and F has no jumps
    {
        double worstSlope = 0.0, worstJump = 0.0; std::string where;
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> d(-12.0, 12.0);
        for (int m = 0; m < static_cast<int>(Mode::Count); ++m){
            if (!hasAntiderivative(m)) continue;
            for (int k = 0; k < 4000; ++k){
                const double x = d(rng), h = 1.0e-6;
                const double slope = (antiderivative(m, x + h) - antiderivative(m, x - h)) / (2.0 * h);
                // right at a jump or corner the difference quotient straddles it
                if (std::fabs(shape(m, x + 1e-4) - shape(m, x - 1e-4)) > 1e-2) continue;
                const double err = std::fabs(slope - shape(m, x));
                if (err > worstSlope){ worstSlope = err; where = modeName(m) + (" at " + f2s(x, 3)); }
            }
            // continuity on a fine grid: F may change by at most max|f| per step
            // (Tube's lower half reaches past 1, so each mode's own maximum)
            const double step = 1.0e-4;
            double fmax = 0.0;
            for (double x = -12.0; x <= 12.0; x += step) fmax = std::max(fmax, std::fabs(shape(m, x)));
            for (double x = -12.0; x < 12.0; x += step){
                const double jump = std::fabs(antiderivative(m, x + step) - antiderivative(m, x)) - (fmax * 1.001) * step;
                worstJump = std::max(worstJump, jump);
            }
        }
        check("anti-aliasing: every antiderivative differentiates back to its shaper", worstSlope < 1.0e-5,
              "worst " + f2s(worstSlope, 8) + " (" + where + ")");
        check("anti-aliasing: and none of them jumps (a jump would click)", worstJump < 1.0e-9,
              "worst excess " + f2s(worstJump, 12));
    }
    // 2. the oversampler hands its callback the samples in time order. A linear
    //    ramp stays a linear ramp through the half-band filters once they have
    //    filled, so any step backwards means a swapped pair
    for (int factor : { 2, 4 }){
        Oversampler os; os.prepare(factor);
        double prev = -1e9; int backwards = 0, calls = 0;
        for (int i = 0; i < 2000; ++i)
            os.process(i * 0.001, [&](double s){
                if (i > 200){ if (s < prev) ++backwards; ++calls; }
                prev = s; return s;
            });
        check("the oversampler feeds its stages in order at " + std::to_string(factor) + "x",
              backwards == 0 && calls > 0, std::to_string(backwards) + " of " + std::to_string(calls) + " out of order");
    }
    // 3. less aliasing in the audible band, where it was heard
    Patch wrap; wrap.v = { { "bands", 0 }, { "d0a", 9 }, { "m0a", 9 }, { "mx0", 100 } };
    Patch rift; rift.v = { { "bands", 0 }, { "d0a", 9 }, { "m0a", 6 }, { "sb0", 1 }, { "d0b", 5 },
                          { "m0b", 9 }, { "t0", -2 }, { "mx0", 100 } };
    Patch fold; fold.v = { { "bands", 0 }, { "d0a", 9 }, { "m0a", 6 }, { "mx0", 100 } };
    struct Case { const char* name; Patch p; double f0, atMost, gain; };
    const Case cases[] = {
        { "Wrap on 1.2 kHz",             wrap, 1234.5, -40.0, 20.0 },
        { "Wrap on 3.7 kHz",             wrap, 3721.3, -30.0, 20.0 },
        { "Fold on 3.7 kHz",             fold, 3721.3, -55.0, 20.0 },
        { "Rift's fold into wrap, 1.2 kHz", rift, 1234.5, -30.0, 15.0 },
    };
    for (const auto& c : cases){
        const double on = aliasDb(sineThrough(c.p, c.f0, true), c.f0, 48000.0, 16000.0);
        const double off = aliasDb(sineThrough(c.p, c.f0, false), c.f0, 48000.0, 16000.0);
        check(std::string("anti-aliasing: ") + c.name + ", aliasing under 16 kHz",
              on < c.atMost && off - on > c.gain,
              f2s(off, 1) + " dB without, " + f2s(on, 1) + " dB with");
    }
    // 4. the same sound underneath: on a low note, where there was little to
    //    alias, the harmonics come out at the same levels
    {
        Patch p; p.v = { { "bands", 0 }, { "d0a", 6 }, { "m0a", 6 }, { "mx0", 100 } };
        const double sr = 48000.0, f0 = 110.0;
        const auto a = sineThrough(p, f0, true), b = sineThrough(p, f0, false);
        double worst = 0.0;
        for (int h = 1; h <= 20; ++h){
            const double w = 2.0 * M_PI * f0 * h / sr;
            std::complex<double> ga = 0.0, gb = 0.0;
            for (size_t i = a.size() - 48000; i < a.size(); ++i){
                const auto z = std::polar(1.0, -w * static_cast<double>(i));
                ga += static_cast<double>(a[i]) * z; gb += static_cast<double>(b[i]) * z;
            }
            if (std::abs(gb) > 1e-3 * 48000) worst = std::max(worst, std::fabs(db(std::abs(ga) / std::abs(gb))));
        }
        check("anti-aliasing: the first twenty harmonics of a low note are unchanged, within 0.3 dB",
              worst < 0.3, "worst " + f2s(worst, 3) + " dB");
    }
}


// A render the caller supplies the input for, with a transport, for the things
// that cannot be measured from noise: an impulse into a self-oscillating
// filter, a flat level through a tremolo, an LFO locked to a tempo.
struct Take { std::vector<float> L, R; };
static Take renderWith(const Patch& patch, const std::vector<float>& in, double sr, int block,
                       bool playing, double bpm, double startPpq){
    Engine e;
    e.prepare(sr, block);
    applyPatch(e, patch);
    e.seedFrom(0);
    Take t; t.L = in; t.R = in;
    const double beatsPerSample = bpm / 60.0 / sr;
    double ppq = startPpq;
    for (size_t i = 0; i < in.size(); i += static_cast<size_t>(block)){
        const int m = static_cast<int>(std::min(static_cast<size_t>(block), in.size() - i));
        Transport tr; tr.bpm = bpm; tr.ppq = ppq; tr.playing = playing; tr.valid = true;
        e.setTransport(tr);
        float* io[2] = { t.L.data() + i, t.R.data() + i };
        e.process(io, 2, m);
        ppq += beatsPerSample * m;
    }
    return t;
}

// The frequency of anything periodic, from the time between its first and last
// upward crossing of its own mean — counting crossings alone is only good to
// plus or minus one cycle, which is a 5% error over a couple of seconds.
static double freqOf(const std::vector<float>& v, size_t from, size_t to, double sr){
    double mean = 0.0;
    for (size_t i = from; i < to; ++i) mean += v[i];
    mean /= static_cast<double>(to - from);
    const double hyst = 0.02 * std::fabs(mean) + 1.0e-9;    // ignore ripple at the top
    long n = 0; size_t first = 0, last = 0;
    bool above = v[from] > mean;
    for (size_t i = from + 1; i < to; ++i){
        if (!above && v[i] > mean + hyst){ if (n == 0) first = i; last = i; ++n; above = true; }
        else if (above && v[i] < mean - hyst) above = false;
    }
    if (n < 2 || last == first) return 0.0;
    return static_cast<double>(n - 1) * sr / static_cast<double>(last - first);
}

static void testShaperParity(const char* csvPath){
    std::ifstream in(csvPath);
    if (!in){ check("shaper reference table found", false, csvPath); return; }
    std::string line;
    std::getline(in, line);                      // header
    std::map<std::string, int> byId;
    for (int i = 0; i < numModes; ++i) byId[modeId(i)] = i;
    long rows = 0;
    double worst = 0.0;
    std::string worstWhere;
    while (std::getline(in, line)){
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string id, xs, ys;
        std::getline(ss, id, ','); std::getline(ss, xs, ','); std::getline(ss, ys, ',');
        const auto it = byId.find(id);
        if (it == byId.end()){ check("unknown mode id in reference: " + id, false); return; }
        const double x = std::stod(xs), expected = std::stod(ys);
        const double got = shape(it->second, x);
        const double err = std::fabs(got - expected);
        if (err > worst){ worst = err; worstWhere = id + " at x=" + xs; }
        ++rows;
    }
    check("all 14 shapers match the JavaScript to 1e-12 (" + std::to_string(rows) + " points)",
          worst < 1e-12, "worst " + f2s(worst, 18) + " at " + worstWhere);
}

// -------------------------------------------------------------------- helpers
static double sineAmplitude(Engine& e, double freq, double sr, int cycles = 60){
    const int block = 128;
    const int warm = static_cast<int>(sr * 0.08);
    const int n = std::max(static_cast<int>(sr / freq * cycles), 2048) + warm;
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i){
        const float s = static_cast<float>(0.25 * std::sin(2.0 * M_PI * freq * i / sr));
        L[i] = s; R[i] = s;
    }
    for (int i = 0; i < n; i += block){
        const int m = std::min(block, n - i);
        float* io[2] = { L.data() + i, R.data() + i };
        e.process(io, 2, m);
    }
    // RMS over a whole number of cycles, not peak-of-samples: at 12 kHz on a
    // 48 kHz clock there are four samples per cycle, so the largest sample can
    // sit 3 dB below the real peak and the measurement lies
    const int period = std::max(1, static_cast<int>(std::llround(sr / freq)));
    const int usable = ((n - warm) / period) * period;
    double sum = 0.0;
    for (int i = warm; i < warm + usable; ++i) sum += static_cast<double>(L[i]) * L[i];
    const double rms = std::sqrt(sum / std::max(1, usable));
    return rms * std::sqrt(2.0) / 0.25;
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

// "Start from": the bars that give another mode's sound
static void testStartFrom(){
    // the bars are the mode's own harmonics, measured independently: one
    // period of shape(4 cos t), sampled and taken through a plain DFT
    std::string off;
    for (int m : { static_cast<int>(Mode::Soft), static_cast<int>(Mode::Tube), static_cast<int>(Mode::Tape),
                   static_cast<int>(Mode::Diode), static_cast<int>(Mode::Fold) }){
        double bars[tableHarmonics]; barsFromShaper(m, startFromDrive, bars);
        constexpr int n = 2048;
        double a[tableHarmonics + 1] = {}, peak = 0.0;
        for (int k = 1; k <= tableHarmonics; ++k){
            for (int j = 0; j < n; ++j)
                a[k] += shape(m, startFromDrive * std::cos(2.0 * M_PI * j / n)) * std::cos(2.0 * M_PI * k * j / n);
            a[k] *= 2.0 / n; peak = std::max(peak, std::fabs(a[k]));
        }
        double worst = 0.0;
        for (int k = 1; k <= tableHarmonics; ++k)
            worst = std::max(worst, std::fabs(a[k] / peak - bars[k - 1]));
        if (worst > 0.006) off += std::string(" [") + modeName(m) + " " + f2s(worst * 100, 2) + "%]";
    }
    check("Start from: the bars are the mode's own harmonics (to half a percent)", off.empty(), off);

    double soft[tableHarmonics], tube[tableHarmonics];
    barsFromShaper(static_cast<int>(Mode::Soft), startFromDrive, soft);
    barsFromShaper(static_cast<int>(Mode::Tube), startFromDrive, tube);
    double evenSoft = 0.0;
    for (int k = 1; k < tableHarmonics; k += 2) evenSoft = std::max(evenSoft, std::fabs(soft[k]));
    check("  ... so Soft starts with no even harmonics and Tube with a 2nd",
          evenSoft == 0.0 && std::fabs(tube[1]) > 0.03,
          "Soft's largest even " + f2s(evenSoft * 100, 1) + "%, Tube's 2nd " + f2s(tube[1] * 100, 1) + "%");

    const double eSoft = startFromError(static_cast<int>(Mode::Soft), startFromDrive);
    const double eTube = startFromError(static_cast<int>(Mode::Tube), startFromDrive);
    const double eTape = startFromError(static_cast<int>(Mode::Tape), startFromDrive);
    const double eWrap = startFromError(static_cast<int>(Mode::Wrap), startFromDrive);
    check("  ... and drawn, Soft, Tube and Tape are within 1.5% of their own curves",
          eSoft < 0.015 && eTube < 0.015 && eTape < 0.015,
          f2s(eSoft * 100, 2) + "%, " + f2s(eTube * 100, 2) + "%, " + f2s(eTape * 100, 2) + "%");
    check("  ... while Wrap, which sixteen harmonics cannot draw, is not offered",
          eWrap > 0.3 && !startFromFits(static_cast<int>(Mode::Wrap)) && startFromFits(static_cast<int>(Mode::Tube)),
          "Wrap " + f2s(eWrap * 100, 0) + "% out");
}

// Per-band stereo: Mid drives what the channels share and leaves the side
// clean, Side the other way round. Each is checked against something it must
// equal exactly: the band at Mix 0 is the clean path, and a mono signal has no
// side, so Mid on it is plain L/R
static void testBandStereo(){
    auto renderLR = [](const Patch& p, const std::vector<float>& inL, const std::vector<float>& inR){
        Engine e; e.prepare(48000.0, 256); applyPatch(e, p); e.seedFrom(0);
        std::vector<float> l = inL, r = inR;
        for (size_t i = 0; i < l.size(); i += 256){
            float* io[2] = { l.data() + i, r.data() + i };
            e.process(io, 2, static_cast<int>(std::min<size_t>(256, l.size() - i)));
        }
        return std::make_pair(l, r);
    };
    auto worst = [](const std::pair<std::vector<float>, std::vector<float>>& a,
                    const std::pair<std::vector<float>, std::vector<float>>& b){
        double w = 0.0;
        for (size_t i = 0; i < a.first.size(); ++i)
            w = std::max({ w, std::fabs(static_cast<double>(a.first[i]) - b.first[i]),
                           std::fabs(static_cast<double>(a.second[i]) - b.second[i]) });
        return w;
    };
    const int n = 24000;
    std::vector<float> a(n), neg(n);
    std::mt19937 rng(11); std::normal_distribution<float> g(0.0f, 0.2f);
    for (int i = 0; i < n; ++i){ a[i] = g(rng); neg[i] = -a[i]; }
    Patch driven; driven.v = { {"bands", 0}, {"m0a", static_cast<float>(Mode::Tube)}, {"d0a", 20}, {"sb0", 1},
                               {"m0b", static_cast<float>(Mode::Fold)}, {"d0b", 6}, {"t0", 4} };
    Patch clean = driven; clean.v["mx0"] = 0;
    Patch mid = driven; mid.v["st0"] = 1;
    Patch side = driven; side.v["st0"] = 2;

    const double sideThroughMid = worst(renderLR(mid, a, neg), renderLR(clean, a, neg));
    check("Mid leaves the side alone: a side-only signal comes out as the clean band",
          sideThroughMid < 1e-6, f2s(sideThroughMid * 1e6, 3) + " millionths at worst");
    const double monoThroughMid = worst(renderLR(mid, a, a), renderLR(driven, a, a));
    check("  ... and drives the mid: a mono signal comes out exactly as in L/R",
          monoThroughMid < 1e-6, f2s(monoThroughMid * 1e6, 3) + " millionths at worst");
    const double monoThroughSide = worst(renderLR(side, a, a), renderLR(clean, a, a));
    check("Side leaves the mid alone: a mono signal comes out as the clean band",
          monoThroughSide < 1e-6, f2s(monoThroughSide * 1e6, 3) + " millionths at worst");
    // and it does something: Mid on a stereo signal is neither L/R nor clean
    std::vector<float> b(n); for (int i = 0; i < n; ++i) b[i] = g(rng);
    const double vsLR = worst(renderLR(mid, a, b), renderLR(driven, a, b));
    const double vsClean = worst(renderLR(mid, a, b), renderLR(clean, a, b));
    check("  ... and on a real stereo signal Mid is its own sound", vsLR > 0.01 && vsClean > 0.01,
          f2s(vsLR, 3) + " from L/R, " + f2s(vsClean, 3) + " from clean");
}

// The Scope's harmonic readout (Harmonics.h): does it read a tone's harmonics
// as they are, and keep quiet when there is no tone to read?
static void testHarmonicReadout(){
    const double sr = 48000.0; const int n = 2048;
    auto mags = [&](const std::vector<double>& x){        // Hann, as the plugin's FFT
        std::vector<std::complex<double>> a(n);
        for (int i = 0; i < n; ++i) a[i] = x[i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1)));
        fftInPlace(a);
        std::vector<float> m(n / 2);
        for (int k = 0; k < n / 2; ++k) m[k] = static_cast<float>(std::abs(a[k]));
        return m;
    };
    auto tone = [&](double f, double amp){
        std::vector<double> x(n);
        for (int i = 0; i < n; ++i) x[i] = amp * std::sin(2.0 * M_PI * f * i / sr);
        return x;
    };
    auto through = [&](const std::vector<double>& x, int mode, double drive){
        std::vector<double> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = shape(mode, drive * x[i]);
        return y;
    };
    // the true levels, from one period of the curve
    auto truth = [&](int mode, double peak, int h){
        const int m = 8192; double a1 = 0.0, ah = 0.0;
        for (int j = 0; j < m; ++j){
            const double t = 2.0 * M_PI * j / m, y = shape(mode, peak * std::sin(t));
            a1 += y * std::sin(t); ah += y * std::sin(h * t);
        }
        // an asymmetric curve's even harmonics sit in the cosine terms
        double bh = 0.0; for (int j = 0; j < m; ++j){ const double t = 2.0 * M_PI * j / m; bh += shape(mode, peak * std::sin(t)) * std::cos(h * t); }
        return 10.0 * std::log10((ah * ah + bh * bh) / (a1 * a1));
    };
    const int tube = static_cast<int>(Mode::Tube), soft = static_cast<int>(Mode::Soft);
    const auto in = tone(220.0, 0.5);
    const auto r = readHarmonics(mags(in).data(), mags(through(in, tube, 4.0)).data(), n / 2, sr);
    double worst = 0.0; std::string got;
    for (int h = 2; h <= 5; ++h){
        const double want = truth(tube, 2.0, h);
        if (want > -50.0) worst = std::max(worst, std::fabs(r.level[h] - want));
        got += " " + std::to_string(h) + ": " + f2s(r.level[h], 1) + " (" + f2s(want, 1) + ")";
    }
    check("the harmonic readout reads Tube's harmonics as they are (within 0.5 dB)",
          r.tonal && worst < 0.5 && std::fabs(r.f0 - 220.0) < 0.5, "f0 " + f2s(r.f0, 2) + " Hz," + got);
    const auto rs = readHarmonics(mags(in).data(), mags(through(in, soft, 4.0)).data(), n / 2, sr);
    check("  ... and reads no 2nd harmonic from Soft", rs.tonal && rs.level[2] < -60.0, f2s(rs.level[2], 1) + " dB");

    // through the whole engine: Tube's 2nd shows, Soft's does not
    auto engineOut = [&](int mode){
        Patch p; p.v = { {"bands", 0}, {"m0a", static_cast<float>(mode)}, {"d0a", 4} };
        Engine e; e.prepare(sr, 256); applyPatch(e, p); e.seedFrom(0);
        const int len = 24000; std::vector<float> l(len), rr(len);
        for (int i = 0; i < len; ++i) l[i] = rr[i] = static_cast<float>(0.4 * std::sin(2.0 * M_PI * 440.0 * i / sr));
        for (int i = 0; i < len; i += 256){ float* io[2] = { l.data() + i, rr.data() + i }; e.process(io, 2, std::min(256, len - i)); }
        std::vector<double> x(n), y(n);
        for (int i = 0; i < n; ++i){ x[i] = 0.4 * std::sin(2.0 * M_PI * 440.0 * (len - n + i) / sr); y[i] = l[len - n + i]; }
        return readHarmonics(mags(x).data(), mags(y).data(), n / 2, sr);
    };
    const auto et = engineOut(tube), es = engineOut(soft);
    check("  ... and through the engine Tube has a 2nd harmonic and Soft none",
          et.tonal && es.tonal && et.level[2] > -35.0 && es.level[2] < -60.0,
          "Tube " + f2s(et.level[2], 1) + " dB, Soft " + f2s(es.level[2], 1) + " dB");

    // nothing to read: noise, or a chord
    std::mt19937 rng(5); std::normal_distribution<double> g(0.0, 0.2);
    std::vector<double> noise(n); for (auto& v : noise) v = g(rng);
    std::vector<double> chord(n);
    for (int i = 0; i < n; ++i) chord[i] = 0.2 * (std::sin(2 * M_PI * 220.0 * i / sr) + std::sin(2 * M_PI * 277.2 * i / sr) + std::sin(2 * M_PI * 329.6 * i / sr));
    const bool quietNoise = !readHarmonics(mags(noise).data(), mags(noise).data(), n / 2, sr).tonal;
    const bool quietChord = !readHarmonics(mags(chord).data(), mags(chord).data(), n / 2, sr).tonal;
    check("  ... and gives no reading for noise or a chord", quietNoise && quietChord,
          std::string(quietNoise ? "" : "noise read as a tone ") + (quietChord ? "" : "chord read as a tone"));
}

// Env follows: Sidechain. The envelope listens to the key instead of the input;
// with no key connected it goes on following the input
static void testSidechain(){
    const Params& P = Params::get();
    const double sr = 48000.0; const int len = 48000;
    std::vector<float> in(len), keyL(len), keyR(len);
    for (int i = 0; i < len; ++i){
        in[i] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * 220.0 * i / sr));
        // a kick-like key: a burst every quarter second
        const int ph = i % 12000;
        keyL[i] = keyR[i] = ph < 2400 ? static_cast<float>(0.8 * std::exp(-ph / 600.0) * std::sin(2.0 * M_PI * 60.0 * ph / sr)) : 0.0f;
    }
    auto dest = [&](const char* id){
        return 1.0f + static_cast<float>(std::find(P.dests().begin(), P.dests().end(), P.index(id)) - P.dests().begin());
    };
    std::vector<double> envTrace;                         // the envelope, once a block
    auto render = [&](float follows, bool withKey, float inGainDb = 0.0f){
        Patch p; p.v = { {"bands", 0}, {"m0a", static_cast<float>(Mode::Soft)}, {"d0a", 2}, {"autoGain", 0},
                         {"mS0", 3}, {"mD0", dest("d0a")}, {"mA0", 80}, {"envAtk", 2}, {"envRel", 60},
                         {"envKey", follows}, {"inGain", inGainDb} };
        Engine e; e.prepare(sr, 256); applyPatch(e, p); e.seedFrom(0);
        std::vector<float> l = in, r = in;
        envTrace.clear();
        for (int i = 0; i < len; i += 256){
            const int m = std::min(256, len - i);
            float* io[2] = { l.data() + i, r.data() + i };
            const float* k[2] = { keyL.data() + i, keyR.data() + i };
            e.process(io, 2, m, withKey ? k : nullptr, withKey ? 2 : 0);
            envTrace.push_back(e.envOut());
        }
        return l;
    };
    auto swing = [&](const std::vector<float>& x){       // how much the level moves, block to block
        double lo = 1e9, hi = 0.0;
        for (int b = 4; b < len / 480; ++b){
            double s = 0.0; for (int i = 0; i < 480; ++i){ const double v = x[b * 480 + i]; s += v * v; }
            const double db = 10.0 * std::log10(s / 480.0 + 1e-20); lo = std::min(lo, db); hi = std::max(hi, db);
        }
        return hi - lo;
    };
    const auto keyed = render(1, true), inputOnly = render(0, true), noKey = render(1, false), none = render(0, false);
    check("Env follows Sidechain: a steady tone pulses with the key", swing(keyed) > swing(inputOnly) + 4.0,
          f2s(swing(keyed), 1) + " dB of movement keyed, " + f2s(swing(inputOnly), 2) + " following the input");
    check("  ... with Env following the input the key changes nothing, bit for bit", inputOnly == none);
    check("  ... and with no key connected, Sidechain follows the input", noKey == none);
    // the Input knob scales what is processed, not what keys it: the envelope
    // the key makes is the same at 0 and at +6 dB in
    render(1, true, 0.0f); const auto env0 = envTrace;
    render(1, true, 6.0f); const auto env6 = envTrace;
    double worstEnv = 0.0;
    for (size_t i = 0; i < env0.size(); ++i) worstEnv = std::max(worstEnv, std::fabs(env0[i] - env6[i]));
    check("  ... and the Input knob does not change what the key does to the envelope", worstEnv < 1e-9,
          "largest difference " + f2s(worstEnv, 12));
}

int main(int argc, char** argv){
    const char* csv = argc > 1 ? argv[1] : "plugin/tests/shaper_reference.csv";
    const char* presetsPath = argc > 2 ? argv[2] : "plugin/tests/presets.json";
    const Params& P = Params::get();

    std::printf("\nParameters\n");
    check("table has the browser's parameter ids", P.index("d0a") >= 0 && P.index("mS5") >= 0
          && P.index("osFactor") >= 0, std::to_string(P.count()) + " parameters");
    check("every modulatable parameter is a matrix target",
          static_cast<int>(P.dests().size()) > 30,
          std::to_string(P.dests().size()) + " destinations");

    std::printf("\nShaper parity\n");
    testShaperParity(csv);

    std::printf("\nUndo and A/B\n");
    testHistory();

    std::printf("\nRender\n");
    const Result base = render({});
    check("default patch is finite", base.bad == 0, std::to_string(base.bad) + " non-finite");
    check("default patch is audible", base.rms > 0.01, "rms " + f2s(base.rms));
    check("default patch stays inside full scale", base.peak <= 1.0, "peak " + f2s(base.peak, 3));

    for (int m = 0; m < numModes; ++m){
        Patch p; p.v = { {"bands", 0}, {"d0a", 40}, {"m0a", static_cast<float>(m)},
                         {"sb0", 1}, {"d0b", 40}, {"m0b", static_cast<float>(m)} };
        const Result r = render(p);
        check(std::string("mode ") + modeId(m) + " at max drive, two stages",
              r.bad == 0 && r.peak <= 1.0 && r.rms > 0.0005,
              "bad " + std::to_string(r.bad) + " peak " + f2s(r.peak, 3) + " rms " + f2s(r.rms));
    }

    {
        Patch p; p.v = { {"bands", 0}, {"m0a", 4}, {"d0a", 24}, {"fbAmt", 85},
                         {"fbTime", 3}, {"fbTone", 14000} };
        const Result r = render(p, 0.6);
        check("feedback at 85% does not run away", r.bad == 0 && r.peak <= 1.0,
              "peak " + f2s(r.peak, 3));
    }
    {
        Patch p; p.v = { {"bands", 0}, {"bits", 2}, {"redux", 32}, {"crMix", 100} };
        const Result r = render(p);
        check("crush changes the signal", r.bad == 0 && std::fabs(r.rms - base.rms) > 0.001,
              "rms " + f2s(r.rms) + " vs " + f2s(base.rms));
    }
    {
        Patch p; p.v = { {"bands", 0}, {"m0a", 9}, {"d0a", 30}, {"autoGain", 0} };
        const Result r = render(p);
        check("safety clip contains the worst mode", r.bad == 0 && r.peak <= 1.0,
              "peak " + f2s(r.peak, 3));
    }

    std::printf("\nRouting\n");
    for (int b = 0; b < 3; ++b){
        Patch p; p.v = { {"bands", static_cast<float>(b)} };
        const Result r = render(p);
        check(std::to_string(b + 1) + " band(s) audible", r.bad == 0 && r.rms > 0.01,
              "rms " + f2s(r.rms));
    }
    {
        Patch p; p.v = { {"mu0", 1}, {"mu1", 1}, {"mu2", 1} };
        const Result r = render(p);
        check("muting every band silences the wet path", r.rms < 0.001, "rms " + f2s(r.rms, 5));
    }
    {
        Patch p; p.v = { {"so1", 1} };
        const Result r = render(p);
        check("solo leaves one band audible", r.rms > 0.005 && r.rms < base.rms,
              "rms " + f2s(r.rms) + " vs " + f2s(base.rms));
    }

    std::printf("\nLatency and phase\n");
    {
        // dry-only: the output must be the input delayed by exactly the latency
        // the plugin reports, or every bounce is smeared
        Engine e;
        e.prepare(48000.0, 128);
        Patch p; p.v = { {"mix", 0} };
        applyPatch(e, p);
        const int lat = e.latencySamples();
        const int n = 4096;
        std::vector<float> L(n, 0.0f), R(n, 0.0f), in(n, 0.0f);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> d(-0.3f, 0.3f);
        for (int i = 0; i < n; ++i){ in[i] = d(rng); L[i] = in[i]; R[i] = in[i]; }
        for (int i = 0; i < n; i += 128){
            float* io[2] = { L.data() + i, R.data() + i };
            e.process(io, 2, 128);
        }
        double worst = 0.0;
        for (int i = lat + 64; i < n; ++i) worst = std::max(worst, static_cast<double>(std::fabs(L[i] - in[i - lat])));
        check("reported latency is the real latency (" + std::to_string(lat) + " samples)",
              lat > 0 && worst < 1e-6, "worst sample error " + f2s(worst, 9));
    }
    {
        // three bands, each passing its own input: the sum must be flat
        Engine e;
        e.prepare(48000.0, 128);
        Patch p; p.v = { {"bands", 2}, {"mx0", 0}, {"mx1", 0}, {"mx2", 0},
                         {"safety", 0}, {"osFactor", 0}, {"x1", 220}, {"x2", 2200} };
        applyPatch(e, p);
        double worstDb = 0.0; double atFreq = 0.0;
        for (double f : { 30.0, 60.0, 120.0, 220.0, 400.0, 800.0, 1500.0, 2200.0,
                          3000.0, 6000.0, 12000.0, 16000.0 }){
            e.reset();
            const double a = sineAmplitude(e, f, 48000.0);
            const double db = 20.0 * std::log10(std::max(1e-9, a));
            if (std::fabs(db) > std::fabs(worstDb)){ worstDb = db; atFreq = f; }
        }
        check("the three bands sum flat within 0.5 dB",
              std::fabs(worstDb) < 0.5, f2s(worstDb, 2) + " dB at " + f2s(atFreq, 0) + " Hz");
    }

    std::printf("\nModulation\n");
    {
        Patch p; p.v = { {"bands", 0}, {"d0a", 12},
                         {"mS0", 1}, {"mA0", 100}, {"l1Rate", 8} };
        const int dst = -1 + 1;                       // resolved below
        (void) dst;
        // target the first band's Drive A through the destination list
        const auto& dests = P.dests();
        int choice = 0;
        for (size_t i = 0; i < dests.size(); ++i) if (P[dests[i]].id == "d0a") choice = static_cast<int>(i) + 1;
        p.v["mD0"] = static_cast<float>(choice);
        const Result r = render(p, 0.4);
        check("an LFO on drive renders finite and audible",
              choice > 0 && r.bad == 0 && r.rms > 0.005 && r.peak <= 1.0,
              "peak " + f2s(r.peak, 3) + " rms " + f2s(r.rms));
    }
    {
        // random sample-and-hold, seeded from the playhead: two renders of the
        // same passage must be identical, or bounces will not match
        Patch p; p.v = { {"bands", 0}, {"d0a", 10}, {"l1Shape", 5}, {"l1Rate", 12},
                         {"mS0", 1}, {"mA0", 80} };
        const auto& dests = P.dests();
        for (size_t i = 0; i < dests.size(); ++i) if (P[dests[i]].id == "d0a") p.v["mD0"] = static_cast<float>(i) + 1;
        const Result a = render(p, 0.25, 48000.0, 128, 3, true);
        const Result b = render(p, 0.25, 48000.0, 128, 3, true);
        bool same = a.left.size() == b.left.size();
        double worst = 0.0;
        if (same) for (size_t i = 0; i < a.left.size(); ++i)
            worst = std::max(worst, std::fabs(static_cast<double>(a.left[i]) - b.left[i]));
        check("a seeded render is bit-identical twice over", same && worst == 0.0,
              "worst " + f2s(worst, 12));
    }

    std::printf("\nBrowser patches\n");
    {
        std::ifstream in(presetsPath);
        std::stringstream ss; ss << in.rdbuf();
        const std::string json = ss.str();
        if (json.empty()){ check("presets.json found", false, presetsPath); }
        else {
            // the file is generated by plugin/tools/make-reference.mjs, so the
            // shapes are known: scan for "name" and the id/value pairs after it
            size_t pos = 0; int count = 0, unknown = 0, rendered = 0;
            std::vector<std::vector<float>> renders; std::vector<std::string> names;
            std::string unknownIds;
            while ((pos = json.find("\"name\":", pos)) != std::string::npos){
                const size_t q1 = json.find('"', pos + 7), q2 = json.find('"', q1 + 1);
                const std::string name = json.substr(q1 + 1, q2 - q1 - 1);
                const size_t vStart = json.find("\"v\":", q2);
                const size_t braceOpen = json.find('{', vStart);
                const size_t braceClose = json.find('}', braceOpen);
                const std::string body = json.substr(braceOpen + 1, braceClose - braceOpen - 1);
                Patch patch;
                size_t p2 = 0;
                while (true){
                    const size_t k1 = body.find('"', p2);
                    if (k1 == std::string::npos) break;
                    const size_t k2 = body.find('"', k1 + 1);
                    const std::string key = body.substr(k1 + 1, k2 - k1 - 1);
                    const size_t colon = body.find(':', k2);
                    size_t end = body.find(',', colon);
                    if (end == std::string::npos) end = body.size();
                    std::string val = body.substr(colon + 1, end - colon - 1);
                    while (!val.empty() && (val.front() == ' ' || val.front() == '\n')) val.erase(val.begin());
                    while (!val.empty() && (val.back() == ' ' || val.back() == '\n')) val.pop_back();
                    p2 = end + 1;
                    const int idx = P.index(key);
                    if (idx < 0){ ++unknown; unknownIds += " " + key; continue; }
                    const bool isStr = !val.empty() && val.front() == '"';
                    const std::string text = isStr ? val.substr(1, val.size() - 2) : val;
                    const bool isBool = val == "true" || val == "false";
                    const bool isNum = !isStr && !isBool;
                    float out = 0.0f;
                    if (patchValueToParam(P[idx], text, isNum, isNum ? std::stod(val) : 0.0,
                                          isBool, val == "true", out))
                        patch.v[key] = out;
                    else { ++unknown; unknownIds += " " + key + "=" + val; }
                }
                const Result r = render(patch, 0.35, 48000.0, 128, 1, true);   // keep the samples
                renders.push_back(r.left); names.push_back(name);
                const bool ok = r.bad == 0 && r.peak <= 1.0 && r.rms > 0.002;
                check("preset \"" + name + "\"", ok,
                      "bad " + std::to_string(r.bad) + " peak " + f2s(r.peak, 3) + " rms " + f2s(r.rms));
                if (ok) ++rendered;
                ++count;
                pos = braceClose;
            }
            // presets.json and core/Presets.h are both generated from fx/fracture.html;
            // they must agree, and every one must load and render
            const int expected = static_cast<int>(presets().size());
            check("every browser preset (" + std::to_string(expected) + ") loads and renders",
                  count == expected && rendered == expected,
                  std::to_string(rendered) + "/" + std::to_string(count));
            check("every value in every browser patch maps to a plugin parameter",
                  unknown == 0, unknownIds);
            // no preset may be a copy of another: CRATE's audition renders found
            // one there, so both products check for it
            std::string same;
            for (size_t a2 = 0; a2 < renders.size(); ++a2)
                for (size_t b2 = a2 + 1; b2 < renders.size(); ++b2)
                    if (renders[a2] == renders[b2]) same += " [" + names[a2] + " = " + names[b2] + "]";
            bool empty = false;
            for (const auto& r2 : renders) if (r2.empty()) empty = true;   // or they all compare equal
            check("every preset sounds different from every other", !empty && same.empty(), same);
        }
    }

    // ------------------------------------------------------------- the ladder
    // A filter that is a nonlinear feedback loop cannot be checked by reading
    // the coefficients back; everything here is measured from rendered audio.
    std::printf("\nAnalogue filter\n");
    {
        // a patch that puts the filter on its own: one band, passed through
        // dry, no oversampling filters in the way
        auto fltPatch = [](int circuit, int type, int poles, double freq, double q,
                           double drive = 1.0, double drift = 0.0){
            Patch p; p.v = { {"bands", 0}, {"mx0", 0}, {"osFactor", 0},
                             {"fltType", static_cast<float>(type)},
                             {"fltFreq", static_cast<float>(freq)},
                             {"fltQ", static_cast<float>(q)},
                             {"fltCirc", static_cast<float>(circuit)},
                             {"fltPoles", static_cast<float>(poles)},
                             {"fltDrive", static_cast<float>(drive)},
                             {"fltDrift", static_cast<float>(drift)} };
            return p;
        };
        auto gainAt = [&](const Patch& p, double freq, double sr = 48000.0){
            Engine e; e.prepare(sr, 128); applyPatch(e, p); e.seedFrom(0);
            return sineAmplitude(e, freq, sr);
        };

        const double cleanPass = gainAt(fltPatch(0, 1, 1, 1000, 0.3), 100.0);
        const double analogPass = gainAt(fltPatch(1, 1, 1, 1000, 0.3), 100.0);
        check("clean and analogue agree in the passband",
              std::fabs(db(analogPass) - db(cleanPass)) < 1.5,
              f2s(db(analogPass), 2) + " dB vs " + f2s(db(cleanPass), 2) + " dB");

        // The mark is the corner, not the -3 dB point: a four-pole is 3 dB down
        // at 0.435 of its corner, which is why it is darker than the biquad at
        // the same number. Measured, so the number in the comment stays true.
        const double at435 = db(gainAt(fltPatch(1, 1, 1, 1000, 0.3), 435.0)) - db(analogPass);
        check("a four-pole is 3 dB down at 0.435 of its mark", std::fabs(at435 + 3.0) < 1.2,
              f2s(at435, 2) + " dB at 435 Hz");

        // slope, measured over the octave from 4 to 8 kHz with the corner at
        // 500 Hz, where both are well into their rolloff
        const double two = db(gainAt(fltPatch(1, 1, 0, 500, 0.3), 8000.0))
                         - db(gainAt(fltPatch(1, 1, 0, 500, 0.3), 4000.0));
        const double four = db(gainAt(fltPatch(1, 1, 1, 500, 0.3), 8000.0))
                          - db(gainAt(fltPatch(1, 1, 1, 500, 0.3), 4000.0));
        check("12 dB an octave really is 12 dB an octave", std::fabs(two + 12.0) < 2.5,
              f2s(two, 2) + " dB");
        check("and 24 dB an octave really is 24", std::fabs(four + 24.0) < 3.5,
              f2s(four, 2) + " dB");

        // resonance that sings: kick it once and listen to what is left
        {
            const double sr = 48000.0, cutoff = 400.0;
            std::vector<float> in(static_cast<size_t>(sr * 1.5), 0.0f);
            in[64] = 0.6f;
            const Take t = renderWith(fltPatch(1, 1, 1, cutoff, 18.0), in, sr, 128, false, 120.0, 0.0);
            const size_t from = static_cast<size_t>(sr * 1.0);
            double rms = 0.0, peak = 0.0;
            for (size_t i = from; i < t.L.size(); ++i){
                rms += static_cast<double>(t.L[i]) * t.L[i];
                peak = std::max(peak, std::fabs(static_cast<double>(t.L[i])));
            }
            rms = std::sqrt(rms / static_cast<double>(t.L.size() - from));
            const double hz = freqOf(t.L, from, t.L.size(), sr);
            check("resonance sings, and sings where the knob points",
                  rms > 0.02 && std::fabs(hz - cutoff) < cutoff * 0.12,
                  "rms " + f2s(rms, 4) + " at " + f2s(hz, 1) + " Hz");
            check("self-oscillation stays inside the rails", peak < 1.01,
                  "peak " + f2s(peak, 3));
        }

        // drive is drive, not volume: harmonics go up, level barely moves
        {
            const double sr = 48000.0, f0 = 220.0;
            auto thd = [&](double drive){
                Patch p = fltPatch(1, 1, 1, 3000, 0.7, drive);
                const int n = static_cast<int>(sr * 0.5);
                std::vector<float> in(static_cast<size_t>(n));
                for (int i = 0; i < n; ++i)
                    in[static_cast<size_t>(i)] =
                        static_cast<float>(0.3 * std::sin(2.0 * M_PI * f0 * i / sr));
                const Take t = renderWith(p, in, sr, 128, false, 120.0, 0.0);
                const size_t skip = static_cast<size_t>(sr * 0.1);
                double total = 0.0, fund = 0.0, re = 0.0, im = 0.0;
                for (size_t i = skip; i < t.L.size(); ++i){
                    const double x = t.L[i];
                    total += x * x;
                    const double ph = 2.0 * M_PI * f0 * static_cast<double>(i) / sr;
                    re += x * std::cos(ph); im += x * std::sin(ph);
                }
                const double n2 = static_cast<double>(t.L.size() - skip);
                fund = 2.0 * std::sqrt(re * re + im * im) / n2;       // amplitude at f0
                const double rest = std::max(0.0, total / n2 - fund * fund * 0.5);
                return std::make_pair(std::sqrt(rest) / (fund * 0.7071), fund);
            };
            const auto clean = thd(1.0), hot = thd(10.0);
            check("filter drive adds harmonics", clean.first < 0.03 && hot.first > 0.10,
                  f2s(clean.first * 100, 1) + "% at 1x, " + f2s(hot.first * 100, 1) + "% at 10x");
            check("filter drive is not a volume knob",
                  std::fabs(db(hot.second / clean.second)) < 5.0,
                  f2s(db(hot.second / clean.second), 2) + " dB louder at 10x");
        }

        // drift is the whole reason the two channels are not the same channel
        {
            const double sr = 48000.0;
            const int n = static_cast<int>(sr * 0.6);
            std::vector<float> in(static_cast<size_t>(n));
            std::mt19937 rng(7);
            std::uniform_real_distribution<float> d(-0.4f, 0.4f);
            for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = d(rng);
            auto spread = [&](double drift){
                const Take t = renderWith(fltPatch(1, 1, 1, 900, 6.0, 1.0, drift),
                                          in, sr, 128, false, 120.0, 0.0);
                double worst = 0.0;
                for (size_t i = static_cast<size_t>(sr * 0.1); i < t.L.size(); ++i)
                    worst = std::max(worst, std::fabs(static_cast<double>(t.L[i]) - t.R[i]));
                return worst;
            };
            const double off = spread(0.0), on = spread(100.0);
            check("drift pulls the two channels apart, and off means off",
                  off == 0.0 && on > 0.002,
                  "L-R " + f2s(off, 6) + " off, " + f2s(on, 6) + " on");
        }

        // the vintage circuit is asymmetric, which is what even harmonics are
        {
            const double sr = 48000.0, f0 = 150.0;
            auto secondHarmonic = [&](int circuit){
                const int n = static_cast<int>(sr * 0.4);
                std::vector<float> in(static_cast<size_t>(n));
                for (int i = 0; i < n; ++i)
                    in[static_cast<size_t>(i)] =
                        static_cast<float>(0.4 * std::sin(2.0 * M_PI * f0 * i / sr));
                const Take t = renderWith(fltPatch(circuit, 1, 1, 4000, 0.7, 6.0), in, sr, 128,
                                          false, 120.0, 0.0);
                const size_t skip = static_cast<size_t>(sr * 0.1);
                double re = 0.0, im = 0.0;
                for (size_t i = skip; i < t.L.size(); ++i){
                    const double ph = 2.0 * M_PI * 2.0 * f0 * static_cast<double>(i) / sr;
                    re += t.L[i] * std::cos(ph); im += t.L[i] * std::sin(ph);
                }
                const double n2 = static_cast<double>(t.L.size() - skip);
                return 2.0 * std::sqrt(re * re + im * im) / n2;
            };
            const double sym = secondHarmonic(1), asym = secondHarmonic(2);
            check("the vintage circuit adds even harmonics the clean one does not",
                  asym > sym * 3.0, f2s(sym, 5) + " against " + f2s(asym, 5) + " at 2f");
        }
    }

    {   // everything at once: a self-oscillating filter inside the feedback
        // loop, with the tremolo chopping the result
        Patch p; p.v = { {"bands", 2}, {"d0a", 20}, {"sb0", 1}, {"crMix", 70}, {"fbAmt", 60},
                         {"fltType", 1}, {"fltCirc", 2}, {"fltPoles", 1}, {"fltFreq", 800},
                         {"fltQ", 16}, {"fltDrive", 12}, {"fltDrift", 100},
                         {"trOn", 1}, {"trDepth", 100}, {"trShape", 100}, {"trDiv", 12},
                         {"trSpread", 120} };
        const Result r = render(p, 0.5);
        check("the ladder, the tremolo and the feedback loop all at once",
              r.bad == 0 && r.peak <= 1.0 && r.rms > 0.002,
              "peak " + f2s(r.peak, 3) + " rms " + f2s(r.rms));
    }

    // ------------------------------------------------------------- the tremolo
    // The tremolo is the last thing in the chain, so with dry/wet at 0 the
    // output IS its gain curve: everything below measures that curve directly.
    std::printf("\nTremolo\n");
    {
        auto tremPatch = [](int div, double rate, double depth, double shape,
                            double edge, double duty, double spread){
            Patch p; p.v = { {"mix", 0}, {"osFactor", 0}, {"trOn", 1},
                             {"trDiv", static_cast<float>(div)},
                             {"trRate", static_cast<float>(rate)},
                             {"trDepth", static_cast<float>(depth)},
                             {"trShape", static_cast<float>(shape)},
                             {"trEdge", static_cast<float>(edge)},
                             {"trDuty", static_cast<float>(duty)},
                             {"trSpread", static_cast<float>(spread)} };
            return p;
        };
        const double sr = 48000.0;
        const double level = 0.4;
        std::vector<float> flat(static_cast<size_t>(sr * 2.0), static_cast<float>(level));
        const size_t skip = static_cast<size_t>(sr * 0.15);   // the depth ramp

        {   // free-running: the rate knob is in Hz and means it
            const Take t = renderWith(tremPatch(0, 5.0, 100, 0, 100, 50, 0), flat, sr,
                                      128, false, 120.0, 0.0);
            const double hz = freqOf(t.L, skip, t.L.size(), sr);
            check("a free tremolo runs at the rate it says",
                  std::fabs(hz - 5.0) < 0.2,
                  f2s(hz, 3) + " Hz");
        }
        {   // synced: 1/8 at 120 BPM is four a second, and the beat is loud
            const Take t = renderWith(tremPatch(10, 5.0, 100, 100, 100, 50, 0), flat, sr,
                                      128, true, 120.0, 0.0);
            const double hz = freqOf(t.L, skip, t.L.size(), sr);
            check("a synced tremolo counts the host's eighths",
                  std::fabs(hz - 4.0) < 0.15,
                  f2s(hz, 3) + " a second at 120 BPM");
            // ppq 0 is the top of the shape, not the bottom: the beat is loud
            check("the beat lands on the loud half", t.L[8] > static_cast<float>(level * 0.9),
                  f2s(t.L[8], 4) + " against " + f2s(level, 4));
        }
        {   // and it is a property of the song position, not of when play began:
            // the same bar played from bar 1 and from bar 3 is the same audio.
            // A rounded shape, because a hard edge lands a sample either side
            // depending on where the phase accumulator started and that is not
            // something floating point can be held to.
            const Take a = renderWith(tremPatch(7, 5.0, 100, 0, 80, 50, 0), flat, sr,
                                      128, true, 120.0, 0.0);
            const Take b = renderWith(tremPatch(7, 5.0, 100, 0, 80, 50, 0), flat, sr,
                                      128, true, 120.0, 8.0);      // two bars later
            double worst = 0.0;
            for (size_t i = skip; i < a.L.size(); ++i)
                worst = std::max(worst, std::fabs(static_cast<double>(a.L[i]) - b.L[i]));
            check("dropping the playhead two bars on lands in the same place",
                  worst < 1.0e-6, "worst " + f2s(worst, 9));
        }
        {   // depth, measured rather than asserted
            auto extremes = [&](double depth){
                const Take t = renderWith(tremPatch(0, 8.0, depth, 100, 100, 50, 0), flat, sr,
                                          128, false, 120.0, 0.0);
                double lo = 1.0, hi = 0.0, sum = 0.0;
                for (size_t i = skip; i < t.L.size(); ++i){
                    lo = std::min(lo, static_cast<double>(t.L[i]));
                    hi = std::max(hi, static_cast<double>(t.L[i]));
                    sum += t.L[i];
                }
                return std::make_tuple(lo / level, hi / level,
                                       sum / static_cast<double>(t.L.size() - skip) / level);
            };
            const auto full = extremes(100.0), half = extremes(50.0);
            check("full depth chops to silence and back to unity",
                  std::get<0>(full) < 0.02 && std::get<1>(full) > 0.98,
                  f2s(std::get<0>(full), 4) + " to " + f2s(std::get<1>(full), 4));
            check("half depth chops half as far",
                  std::fabs(std::get<0>(half) - 0.5) < 0.03,
                  "floor " + f2s(std::get<0>(half), 4));
            check("a square at 50% duty spends half the bar loud",
                  std::fabs(std::get<2>(full) - 0.5) < 0.03,
                  "mean " + f2s(std::get<2>(full), 4));
        }
        {   // duty moves where the dip falls
            const Take t = renderWith(tremPatch(0, 8.0, 100, 100, 100, 25, 0), flat, sr,
                                      128, false, 120.0, 0.0);
            long loud = 0, total = 0;
            for (size_t i = skip; i < t.L.size(); ++i){
                ++total;
                if (t.L[i] > static_cast<float>(level * 0.5)) ++loud;
            }
            const double fraction = static_cast<double>(loud) / static_cast<double>(total);
            check("duty decides how much of the cycle is loud",
                  std::fabs(fraction - 0.25) < 0.03, f2s(fraction, 4) + " of the cycle");
        }
        {   // edge: the same square, with and without its corners
            auto worstStep = [&](double edge){
                const Take t = renderWith(tremPatch(0, 4.0, 100, 100, edge, 50, 0), flat, sr,
                                          128, false, 120.0, 0.0);
                double worst = 0.0;
                for (size_t i = skip + 1; i < t.L.size(); ++i)
                    worst = std::max(worst, std::fabs(static_cast<double>(t.L[i]) - t.L[i - 1]));
                return worst / level;
            };
            const double hard = worstStep(100.0), soft = worstStep(0.0);
            check("edge is a real slew, not a label", soft < hard * 0.25,
                  "step " + f2s(hard, 5) + " hard, " + f2s(soft, 5) + " soft");
        }
        {   // 180 degrees of spread is auto-pan
            const Take t = renderWith(tremPatch(0, 6.0, 100, 0, 100, 50, 180), flat, sr,
                                      128, false, 120.0, 0.0);
            double sumL = 0, sumR = 0, cross = 0, varL = 0, varR = 0;
            const double n2 = static_cast<double>(t.L.size() - skip);
            for (size_t i = skip; i < t.L.size(); ++i){ sumL += t.L[i]; sumR += t.R[i]; }
            const double mL = sumL / n2, mR = sumR / n2;
            for (size_t i = skip; i < t.L.size(); ++i){
                const double a = t.L[i] - mL, b = t.R[i] - mR;
                cross += a * b; varL += a * a; varR += b * b;
            }
            const double corr = cross / std::sqrt(varL * varR);
            check("180 degrees of spread is auto-pan", corr < -0.97, "correlation " + f2s(corr, 4));
        }
        {   // off is off, to the sample
            Patch on = tremPatch(0, 6.0, 0, 0, 100, 50, 0);       // on, but no depth
            Patch off = tremPatch(0, 6.0, 60, 0, 100, 50, 0); off.v["trOn"] = 0;
            const Take a = renderWith(on, flat, sr, 128, false, 120.0, 0.0);
            const Take b = renderWith(off, flat, sr, 128, false, 120.0, 0.0);
            double worst = 0.0;
            for (size_t i = 0; i < a.L.size(); ++i)
                worst = std::max(worst, std::fabs(static_cast<double>(a.L[i]) - b.L[i]));
            check("a tremolo at zero depth is the bypassed signal", worst < 1.0e-7,
                  "worst " + f2s(worst, 9));
        }
    }

    // --------------------------------------------------------- the synced LFOs
    // ------------------------------------------- slopes, rhythm and filter mix
    std::printf("\nFilter slopes and rhythm\n");
    {
        auto fp = [](int circuit, int type, int poles, double freq, double q = 0.3){
            Patch p; p.v = { {"bands", 0}, {"mx0", 0}, {"osFactor", 0},
                             {"fltType", static_cast<float>(type)}, {"fltFreq", static_cast<float>(freq)},
                             {"fltQ", static_cast<float>(q)}, {"fltCirc", static_cast<float>(circuit)},
                             {"fltPoles", static_cast<float>(poles)}, {"fltDrift", 0} };
            return p;
        };
        auto gainAt = [&](const Patch& p, double freq){
            Engine e; e.prepare(48000.0, 128); applyPatch(e, p); e.seedFrom(0);
            return db(sineAmplitude(e, freq, 48000.0));
        };
        // each slope, clean and analogue, over the octave from 2 to 4 kHz with
        // the corner at 250 Hz, where all of them have settled
        double worst = 0.0; std::string detail;
        for (int circuit = 0; circuit < 2; ++circuit) for (int k = 0; k < 4; ++k){
            const double want = 12.0 * (k + 1);
            const double got = gainAt(fp(circuit, 1, k, 250), 2000) - gainAt(fp(circuit, 1, k, 250), 4000);
            const double err = std::fabs(got - want) / want;
            detail += (circuit ? " A" : " C") + std::to_string(static_cast<int>(want)) + "=" + f2s(got, 1);
            worst = std::max(worst, err);
        }
        check("12, 24, 36 and 48 dB an octave, clean and analogue", worst < 0.12, detail);
        // the extra poles do not move the mark or the level: an analogue band
        // pass at 48 dB still peaks on its corner at the level the 24 dB one does
        const double bp24 = gainAt(fp(1, 3, 1, 1000), 1000), bp48 = gainAt(fp(1, 3, 3, 1000), 1000);
        check("a 48 dB band pass peaks where the 24 dB one does, at the same level",
              std::fabs(bp48 - bp24) < 1.0, f2s(bp24, 2) + " dB vs " + f2s(bp48, 2) + " dB");
        const double n36 = gainAt(fp(1, 4, 2, 1000), 1000) - gainAt(fp(1, 4, 2, 1000), 100);
        check("and a 36 dB notch is still a notch on its corner", n36 < -30.0, f2s(n36, 1) + " dB");
        // saved sessions: 12 and 24 keep their indices, and every new modulatable
        // parameter is at the end of the matrix's destination list
        const auto& d = P.dests();
        // in the order they were added: the rhythm, the tuned feedback, the Table
        const char* newDests[] = { "fltMix", "rhDepth", "rhRate", "rhGroove", "rhPhase", "rhGlide",
                                   "fbNote", "tblPos" };
        const size_t nNew = sizeof(newDests) / sizeof(newDests[0]);
        bool atEnd = d.size() > nNew;
        for (size_t k = 0; k < nNew && atEnd; ++k)
            atEnd = P[d[d.size() - nNew + k]].id == newDests[k];
        bool stepsOut = true;
        for (int dd : d) if (P[dd].id.rfind("rhStep", 0) == 0 || (P[dd].id.size() > 2 && P[dd].id.rfind("tb", 0) == 0 && std::isdigit(static_cast<unsigned char>(P[dd].id[2]))))
            stepsOut = false;                                      // neither the steps nor the table's bars
        check("new destinations are appended, so saved matrices keep their targets",
              atEnd && stepsOut && std::string(slopeIds[0]) == "12" && std::string(slopeIds[1]) == "24");
    }
    {
        // filter mix at zero takes the post filter out entirely
        auto p = [](double freq){
            Patch q; q.v = { {"bands", 0}, {"mx0", 0}, {"osFactor", 0}, {"fltType", 1},
                             {"fltFreq", static_cast<float>(freq)}, {"fltMix", 0} };
            return q;
        };
        std::vector<float> in(24000);
        for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(0.3 * std::sin(i * 0.37) * std::sin(i * 0.011));
        const Take a = renderWith(p(200), in, 48000.0, 128, false, 120.0, 0.0);
        const Take b = renderWith(p(16000), in, 48000.0, 128, false, 120.0, 0.0);
        double worst = 0.0;
        for (size_t i = 0; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a.L[i] - b.L[i])));
        check("filter mix at zero takes the post filter out", worst < 1e-6, f2s(worst, 9));
    }
    {
        // the rhythm on the post filter: a 1/4 square, +4 octaves from 300 Hz,
        // on noise, clean circuit. The high half of each beat is brighter.
        auto rp = [](int circuit){
            Patch q; q.v = { {"bands", 0}, {"mx0", 0}, {"osFactor", 0}, {"fltType", 1},
                             {"fltFreq", 300}, {"fltQ", 0.7f}, {"fltCirc", static_cast<float>(circuit)},
                             {"rhShape", 4}, {"rhDiv", 7}, {"rhGlide", 0}, {"rhDepth", 4} };
            return q;
        };
        std::vector<float> in(96000);
        uint32_t r = 7;
        for (auto& v : in){ r = r * 1664525u + 1013904223u; v = static_cast<float>((r >> 8) / 8388608.0 - 1.0) * 0.3f; }
        for (int circuit = 0; circuit < 2; ++circuit){
            const Take t = renderWith(rp(circuit), in, 48000.0, 128, true, 120.0, 0.0);
            double hi = 0, lo = 0;
            for (int beat = 1; beat < 3; ++beat){
                const size_t s0 = static_cast<size_t>(beat * 24000);
                for (size_t k = 480; k < 9600; ++k){
                    hi += t.L[s0 + k] * t.L[s0 + k];
                    lo += t.L[s0 + 12000 + k] * t.L[s0 + 12000 + k];
                }
            }
            check(std::string("the rhythm opens the ") + (circuit ? "analogue" : "clean") + " filter on the beat",
                  10.0 * std::log10(hi / lo) > 6.0, "+" + f2s(10.0 * std::log10(hi / lo), 1) + " dB");
        }
        // and the same bar sounds the same from two bars later
        std::vector<float> tone(48000);
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = static_cast<float>(0.2 * std::sin(i * 2.0 * M_PI * 220.0 / 48000.0));
        Patch st = rp(1); st.v["rhShape"] = 5; st.v["rhDiv"] = 12;       // steps on 1/16
        const Take a = renderWith(st, tone, 48000.0, 128, true, 120.0, 0.0);
        const Take b = renderWith(st, tone, 48000.0, 128, true, 120.0, 8.0);
        // judged as the RMS of the difference. Before the rhythm was read
        // mid-sample this was -47 dB: at 120 BPM a sixteenth is exactly 6000
        // samples, so step edges sat exactly on sample instants and rounding in
        // the song position decided which side they fell
        double diff = 0.0, sig = 0.0;
        for (size_t i = 0; i < tone.size(); ++i){
            const double d = a.L[i] - b.L[i];
            diff += d * d; sig += static_cast<double>(a.L[i]) * a.L[i];
        }
        const double rel = 10.0 * std::log10(std::max(1e-30, diff) / sig);
        check("a rhythmic patch renders the same bar the same from anywhere", rel < -120.0,
              f2s(rel, 1) + " dB of difference");
    }

    // ------------------------------------------------------- tuned feedback
    std::printf("\nTuned feedback\n");
    {
        // The frequency of the loop's fundamental, from how far its phase
        // advances between two Hann-windowed frames one period apart. This is
        // the partial the tuning sets. The first version of this test used an
        // autocorrelation peak, which measures the spacing of the repeats — the
        // loop's GROUP delay — and at low notes, where the DC blocker bends the
        // phase, that is not the pitch of the fundamental at all.
        auto cents = [](const std::vector<float>& x, size_t from, double hz, double sr){
            const double per = sr / hz;
            const int n = static_cast<int>(per * 12.0), D = static_cast<int>(std::lround(per));
            const double w = 2.0 * M_PI * hz / sr;
            auto frame = [&](size_t start){
                std::complex<double> acc = 0.0;
                for (int i = 0; i < n; ++i){
                    const double win = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
                    acc += static_cast<double>(x[start + static_cast<size_t>(i)]) * win * std::polar(1.0, -w * i);
                }
                return acc;
            };
            const double adv = std::arg(frame(from + static_cast<size_t>(D)) / frame(from));
            const double dphi = std::remainder(adv - w * D, 2.0 * M_PI);
            const double got = hz + dphi * sr / (2.0 * M_PI * D);
            return 1200.0 * std::log2(got / hz);
        };
        const double sr = 48000.0;
        // an impulse into the loop; the tail is the loop ringing on its own
        auto ring = [&](Patch p, double note){
            p.v["fbMode"] = 1; p.v["fbNote"] = static_cast<float>(note);
            p.v["fbAmt"] = 85; p.v["fbTone"] = 14000; p.v["mix"] = 100;
            std::vector<float> in(static_cast<size_t>(sr * 0.8), 0.0f);
            in[100] = 0.5f;
            const Take t = renderWith(p, in, sr, 128, false, 120.0, 0.0);
            const double hz = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
            // from three periods in, over thirty: at 85% a loop at 880 Hz has
            // made 220 trips and decayed by 1e-15 by a quarter of a second, so a
            // fixed window late in the tail measures nothing at all
            return cents(t.L, static_cast<size_t>(100 + 3 * sr / hz), hz, sr);
        };
        Patch plain; plain.v = { {"bands", 0}, {"mx0", 0}, {"osFactor", 0} };
        double worst = 0.0; std::string detail;
        for (double note : { 33.0, 45.0, 57.0, 69.0, 81.0, 93.0 }){
            const double c = ring(plain, note);
            worst = std::max(worst, std::fabs(c));
            detail += " " + f2s(note, 0) + ":" + f2s(c, 2);
        }
        check("the loop rings at the note it is set to, A1 to A6, within a cent",
              worst < 1.0, "cents off at each MIDI note —" + detail);

        // through the drive: the oversampler's 48 samples, a three-band split
        // and the DC blockers are all inside the loop now, and compensated
        Patch thru; thru.v = { {"bands", 2}, {"osFactor", 2}, {"fbThru", 1},
                               {"m0a", 0}, {"m1a", 0}, {"m2a", 0},
                               {"d0a", 1}, {"d1a", 1}, {"d2a", 1} };
        worst = 0.0; detail.clear();
        for (double note : { 45.0, 57.0, 69.0, 81.0 }){
            const double c = ring(thru, note);
            worst = std::max(worst, std::fabs(c));
            detail += " " + f2s(note, 0) + ":" + f2s(c, 2);
        }
        check("and through the drive, split three ways at 4x, within 3 cents",
              worst < 3.0, "cents off —" + detail);
    }
    {
        // sync: a 1/8 at 120 BPM is 250 ms between repeats, to the sample
        Patch p; p.v = { {"bands", 0}, {"mx0", 0}, {"osFactor", 0}, {"fbMode", 2}, {"fbDiv", 9},
                         {"fbAmt", 50}, {"fbTone", 14000}, {"mix", 100} };
        std::vector<float> in(48000, 0.0f);
        in[1000] = 0.5f;
        const Take t = renderWith(p, in, 48000.0, 128, true, 120.0, 0.0);
        size_t second = 0; double best = 0.0;
        for (size_t i = 1000 + 6000; i < 1000 + 18000; ++i)
            if (std::fabs(t.L[i]) > best){ best = std::fabs(t.L[i]); second = i; }
        check("synced feedback repeats on the division, to the sample",
              std::llabs(static_cast<long long>(second) - 13000) <= 1,
              "repeat at " + std::to_string(static_cast<long long>(second) - 1000) + " samples, want 12000");
    }
    {
        // through the drive, every repeat is driven again: a sine burst into a
        // folding band gains harmonics repeat after repeat; without it, only
        // the burst is driven and the repeats stay as they came out
        auto third = [&](bool thru){
            Patch p; p.v = { {"bands", 0}, {"osFactor", 2}, {"m0a", 6}, {"d0a", 3},
                             {"fbMode", 0}, {"fbTime", 40}, {"fbAmt", 80}, {"fbTone", 14000},
                             {"fbThru", thru ? 1.0f : 0.0f}, {"autoGain", 1} };
            std::vector<float> in(static_cast<size_t>(48000 * 0.3), 0.0f);
            for (int i = 0; i < 480; ++i) in[static_cast<size_t>(i)] = static_cast<float>(0.2 * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
            const Take t = renderWith(p, in, 48000.0, 128, false, 120.0, 0.0);
            // the third repeat, 120 ms in, measured as 3 kHz against 1 kHz
            std::vector<float> seg(t.L.begin() + 5760 + 48, t.L.begin() + 5760 + 48 + 480);
            auto amp = [&](double f){
                double re = 0, im = 0;
                for (size_t i = 0; i < seg.size(); ++i){ re += seg[i] * std::cos(2 * M_PI * f * i / 48000.0); im += seg[i] * std::sin(2 * M_PI * f * i / 48000.0); }
                return std::sqrt(re * re + im * im);
            };
            return db(amp(3000) / std::max(1e-12, amp(1000)));
        };
        const double off = third(false), on = third(true);
        check("through the drive, each repeat is driven again", on - off > 6.0,
              "3rd harmonic of the 3rd repeat: " + f2s(off, 1) + " dB, " + f2s(on, 1) + " dB through the drive");
    }
    {
        // the worst case stays inside the rails: the wrap shaper at full drive,
        // 85% feedback, through the drive, at a low note
        Patch p; p.v = { {"bands", 2}, {"m0a", 9}, {"m1a", 9}, {"m2a", 9}, {"d0a", 40}, {"d1a", 40}, {"d2a", 40},
                         {"fbMode", 1}, {"fbNote", 36}, {"fbAmt", 85}, {"fbThru", 1} };
        const Result r = render(p, 1.0);
        check("through the drive at full tilt stays finite and bounded", r.bad == 0 && r.peak <= 1.0,
              "peak " + f2s(r.peak, 3));
    }

    // ------------------------------------------------------ what the panel dims
    std::printf("\nDimmed controls\n");
    {
        // A dimmed control is a promise that it does nothing right now. Each
        // patch here puts the box in a different state; every control the
        // panel would dim is moved to the other end of its range, and the
        // output must not change by a single bit.
        std::vector<float> in(static_cast<size_t>(48000 * 0.25));
        uint32_t r = 11;
        for (auto& x : in){ r = r * 1664525u + 1013904223u; x = static_cast<float>((r >> 8) / 8388608.0 - 1.0) * 0.3f; }
        const std::vector<Patch> states = [&]{
            std::vector<Patch> v(12);
            v.reserve(13);
            v[1].v = { {"fbAmt", 40}, {"fbMode", 1} };
            v[2].v = { {"fbAmt", 40}, {"fbMode", 2}, {"fbThru", 1} };
            v[3].v = { {"fltType", 1}, {"fltCirc", 0} };
            v[4].v = { {"fltType", 3}, {"fltCirc", 1}, {"rhDepth", 3}, {"rhShape", 5} };
            v[5].v = { {"mS0", 1}, {"mD0", 5}, {"mA0", 30}, {"trOn", 1}, {"crMix", 50}, {"l1Div", 7} };
            v[6].v = { {"mix", 0} };
            v[7].v = { {"bands", 0}, {"sb0", 1} };
            v[8].v = { {"fltType", 1}, {"fltMix", 0} };
            v[9].v = { {"mS2", 3}, {"trOn", 1}, {"trDiv", 0} };
            v[10].v = { {"fltType", 2}, {"rhDepth", -2}, {"rhDiv", 0} };
            v[11].v = { {"mS0", 2}, {"mD0", 3}, {"mA0", 0}, {"bands", 1} };
            v.push_back({}); v.back().v = { {"mS0", 8}, {"mD0", 5}, {"mA0", 60}, {"xyX", 30} };
            return v;
        }();
        int claims = 0; std::string lies;
        for (size_t si = 0; si < states.size(); ++si){
            Engine probe; probe.prepare(48000.0, 128); applyPatch(probe, states[si]);
            std::vector<float> vals(static_cast<size_t>(P.count()));
            for (int i = 0; i < P.count(); ++i) vals[static_cast<size_t>(i)] = probe.getParam(i);
            const Take base = renderWith(states[si], in, 48000.0, 128, true, 120.0, 0.0);
            for (const Idle& d : idleControls(vals.data())){
                const ParamInfo& info = P[d.param];
                const float cur = vals[static_cast<size_t>(d.param)];
                float other;
                if (info.kind == Kind::Float) other = (cur - info.min) > (info.max - cur) ? info.min : info.max;
                else other = cur >= info.max ? info.min : cur + 1.0f;
                Patch moved = states[si];
                moved.v[info.id] = other;
                const Take t = renderWith(moved, in, 48000.0, 128, true, 120.0, 0.0);
                ++claims;
                for (size_t k = 0; k < in.size(); ++k)
                    if (t.L[k] != base.L[k] || t.R[k] != base.R[k]){
                        lies += " [state " + std::to_string(si) + ": " + info.id + "]";
                        break;
                    }
            }
        }
        check("every dimmed control really does nothing (" + std::to_string(claims) + " claims checked)",
              lies.empty(), lies.empty() ? "" : "audible:" + lies);
        // and the rules are not vacuous: a default patch dims something, and a
        // control in use is not dimmed
        Engine e; e.prepare(48000.0, 128);
        std::vector<float> vals(static_cast<size_t>(P.count()));
        for (int i = 0; i < P.count(); ++i) vals[static_cast<size_t>(i)] = e.getParam(i);
        const auto idleNow = idleControls(vals.data());
        bool fbDimmed = false, driveDimmed = false;
        for (const Idle& d : idleNow){
            if (P[d.param].id == "fbTime") fbDimmed = true;
            if (P[d.param].id == "d0a") driveDimmed = true;
        }
        check("at the defaults the feedback controls are dimmed and the drive is not",
              fbDimmed && !driveDimmed, std::to_string(idleNow.size()) + " dimmed");
    }

    std::printf("\nAnti-aliasing\n");
    testAntialiasing();
    testTubeIsNotSoft();

    std::printf("\nTable mode\n");
    testHarmonicTable();
    testTableInEngine();
    testStartFrom();

    std::printf("\nBand stereo\n");
    testBandStereo();

    std::printf("\nHarmonic readout\n");
    testHarmonicReadout();

    std::printf("\nSidechain\n");
    testSidechain();

    std::printf("\nZipper noise\n");
    {
        struct Case { const char* name; std::map<std::string, float> base; const char* id; float from, to; };
        const std::vector<Case> cases = {
            { "Cutoff (clean)", { {"fltType", 1}, {"fltQ", 4} }, "fltFreq", 300, 8000 },
            { "Cutoff (analogue)", { {"fltType", 1}, {"fltCirc", 1}, {"fltQ", 4} }, "fltFreq", 300, 8000 },
            { "Output", {}, "outGain", -24, 6 },
            { "Drive A", {}, "d0a", 1, 30 },
            { "Dry/wet", {}, "mix", 0, 100 },
            { "Band tone", {}, "t0", -12, 12 },
            { "Split 1", { {"bands", 1} }, "x1", 100, 1000 },
            { "Filter mix", { {"fltType", 4}, {"fltFreq", 1000} }, "fltMix", 0, 100 },
            { "Macro 1 on cutoff", { {"fltType", 1}, {"mS0", 6}, {"mA0", 60} }, "mc1", 0, 100 },
        };
        const int destCut = [&]{ const auto& d = P.dests(); for (size_t k = 0; k < d.size(); ++k) if (P[d[k]].id == "fltFreq") return static_cast<int>(k + 1); return 0; }();
        std::string worstName; double worst = -99.0;
        for (const auto& c : cases){
            double r[2];
            for (int pass = 0; pass < 2; ++pass){
                const int blk = pass ? 512 : 16;
                Engine e; e.prepare(48000.0, 512);
                for (int i = 0; i < P.count(); ++i) e.setParam(i, P[i].def);
                e.setParam(P.index("bands"), 0); e.setParam(P.index("m0a"), 0); e.setParam(P.index("d0a"), 2);
                for (const auto& kv : c.base) e.setParam(P.index(kv.first), kv.second);
                if (std::string(c.id) == "mc1") e.setParam(P.index("mD0"), static_cast<float>(destCut));
                const int n = 96000;
                std::vector<float> L(n), R(n);
                for (int i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(0.25 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
                const ParamInfo& pi = P[P.index(c.id)];
                for (int i = 0; i < n; i += blk){
                    const float t = static_cast<float>(i) / n;
                    e.setParam(P.index(c.id), Params::fromNorm(pi, Params::toNorm(pi, c.from) + (Params::toNorm(pi, c.to) - Params::toNorm(pi, c.from)) * t));
                    float* io[2] = { L.data() + i, R.data() + i };
                    e.process(io, 2, std::min(blk, n - i));
                }
                r[pass] = inharmonicDb(L, 440.0, 48000.0);
            }
            if (r[1] - r[0] > worst){ worst = r[1] - r[0]; worstName = c.name; }
        }
        check("no control zippers when a host moves it once a block", worst < 2.0,
              "worst " + worstName + " +" + f2s(worst, 1) + " dB over a 16-sample-block sweep");
    }

    // ------------------------------------------------ the drawn filter curve
    std::printf("\nFilter display\n");
    {
        // The panel draws the post filter's response from FilterResponse.h.
        // It must be the filter: every type, circuit and slope, resonant, is
        // measured with sine tones through the real engine and compared.
        auto measured = [&](int circuit, int type, int poles, double freq, double q, double mix){
            Patch p; p.v = { {"bands", 0}, {"mx0", 0}, {"osFactor", 0}, {"fltDrift", 0},
                             {"fltType", static_cast<float>(type)}, {"fltCirc", static_cast<float>(circuit)},
                             {"fltPoles", static_cast<float>(poles)}, {"fltFreq", 1000}, {"fltQ", static_cast<float>(q)},
                             {"fltMix", static_cast<float>(mix * 100.0)} };
            Engine e; e.prepare(48000.0, 128); applyPatch(e, p); e.seedFrom(0);
            // quiet enough that neither the ladder's saturation nor the safety
            // clip is in the reading: a 48 dB resonant cascade peaks near +38 dB
            const int n = 48000 / 4;
            const double amp = 0.0005;
            std::vector<float> L(n), R(n);
            for (int i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * i / 48000.0));
            for (int i = 0; i < n; i += 128){ float* io[2] = { L.data() + i, R.data() + i }; e.process(io, 2, std::min(128, n - i)); }
            std::complex<double> acc = 0.0;
            const int from = n / 2;
            for (int i = from; i < n; ++i) acc += static_cast<double>(L[i]) * std::polar(1.0, -2.0 * M_PI * freq * i / 48000.0);
            return db(2.0 * std::abs(acc) / (n - from) / amp);
        };
        const char* circ[] = { "clean", "analogue", "vintage" };
        std::string detail; double worstClean = 0, worstLadder = 0; int points = 0;
        for (int circuit = 0; circuit < 3; ++circuit)
            for (int type = 1; type <= 5; ++type)
                for (int poles = 0; poles < 4; ++poles)
                    for (double f : { 300.0, 700.0, 1000.0, 1400.0, 3000.0 }){
                        FilterState st; st.type = type; st.circuit = circuit; st.poles = 2 * (poles + 1);
                        st.freq = 1000.0; st.q = 3.0; st.drive = 1.0; st.mix = 1.0;
                        const double want = db(std::abs(filterResponse(st, f, 48000.0)));
                        if (want < -40.0) continue;          // deep in a stopband the reading is noise
                        const double got = measured(circuit, type, poles, f, 3.0, 1.0);
                        const double err = std::fabs(got - want);
                        ++points;
                        double& worst = circuit == 0 ? worstClean : worstLadder;
                        if (err > worst){ worst = err;
                            if (err > (circuit == 0 ? 0.1 : 0.5))
                                detail += std::string(" [") + circ[circuit] + " " + filterTypeLabel(type) + " "
                                        + std::to_string(st.poles * 6) + " dB at " + f2s(f, 0) + " Hz: drawn "
                                        + f2s(want, 1) + ", measured " + f2s(got, 1) + "]"; }
                    }
        check("the drawn clean curve is the filter, to 0.1 dB", worstClean < 0.1,
              "worst " + f2s(worstClean, 3) + " dB over " + std::to_string(points) + " points" + detail);
        check("the drawn ladder curve is the filter, to 0.5 dB", worstLadder < 0.5,
              "worst " + f2s(worstLadder, 3) + " dB" + detail);
        // and the filter's mix is in the drawing: a notch at half mix is half deep
        FilterState half; half.type = 4; half.circuit = 0; half.poles = 4; half.freq = 1000; half.q = 3; half.mix = 0.5;
        const double drawn = db(std::abs(filterResponse(half, 1000.0, 48000.0)));
        const double heard = measured(0, 4, 1, 1000.0, 3.0, 0.5);
        check("a notch at half mix is drawn as deep as it measures", std::fabs(drawn - heard) < 0.2,
              "drawn " + f2s(drawn, 2) + " dB, measured " + f2s(heard, 2) + " dB");
    }

    // ------------------------------------------------------- macros and XY
    std::printf("\nMacros and XY pad\n");
    {
        auto destOf = [&](const char* id){
            const auto& d = P.dests(); const int want = P.index(id);
            for (size_t k = 0; k < d.size(); ++k) if (d[k] == want) return static_cast<float>(k + 1);
            return 0.0f;
        };
        // a quiet sine, so the level can move a long way without the safety clip
        std::vector<float> tone(24000);
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = static_cast<float>(0.05 * std::sin(i * 2.0 * M_PI * 440.0 / 48000.0));
        auto level = [&](const Take& t){ double a = 0; for (size_t i = 4800; i < t.L.size(); ++i) a += t.L[i] * t.L[i];
                                         return 10.0 * std::log10(a / (t.L.size() - 4800)); };
        // Output starts at -12 dB so the move has room: the matrix clamps at the
        // end of a range, so from 0 dB a +18 dB push stops at +12
        Patch base; base.v = { {"bands", 0}, {"mx0", 0}, {"osFactor", 0}, {"outGain", -12} };
        Patch routed = base;
        routed.v["mS0"] = 6; routed.v["mD0"] = destOf("outGain"); routed.v["mA0"] = 50;   // Macro 1 -> output
        const Take a = renderWith(base, tone, 48000.0, 128, false, 120.0, 0.0);
        const Take b = renderWith(routed, tone, 48000.0, 128, false, 120.0, 0.0);
        double worst = 0.0;
        for (size_t i = 0; i < tone.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a.L[i] - b.L[i])));
        check("a routed macro at 0 changes nothing", worst == 0.0, f2s(worst, 9));
        Patch up = routed; up.v["mc1"] = 100;
        const double rise = level(renderWith(up, tone, 48000.0, 128, false, 120.0, 0.0)) - level(a);
        // +50% of Output's 36 dB range is 18 dB
        check("at 100 it moves its target by the amount routed", std::fabs(rise - 18.0) < 1.0,
              "+" + f2s(rise, 2) + " dB, want +18");

        // the pad's two axes are independent: X on the output, Y on the input,
        // each alone raises the level, both together raise it by the sum
        Patch pad = base;
        pad.v["mS0"] = 8; pad.v["mD0"] = destOf("outGain"); pad.v["mA0"] = 20;             // X
        pad.v["mS1"] = 9; pad.v["mD1"] = destOf("inGain");  pad.v["mA1"] = 10;             // Y
        auto at = [&](float x, float y){ Patch q = pad; q.v["xyX"] = x; q.v["xyY"] = y;
                                         return level(renderWith(q, tone, 48000.0, 128, false, 120.0, 0.0)); };
        const double c0 = at(0, 0), cx = at(100, 0) - c0, cy = at(0, 100) - c0, cxy = at(100, 100) - c0;
        check("the pad's X and Y move different things, and add up",
              cx > 5.0 && cy > 3.0 && std::fabs(cxy - (cx + cy)) < 1.0,
              "X +" + f2s(cx, 1) + " dB, Y +" + f2s(cy, 1) + " dB, both +" + f2s(cxy, 1) + " dB");

        // a saved slot's source number must mean what it did before these existed
        const auto& src = P[P.index("mS0")].choiceIds;
        check("the new sources are appended, so saved matrices keep their sources",
              src.size() == 10 && src[1] == "lfo1" && src[5] == "trem" && src[6] == "mc1" && src[9] == "xyy");
    }

    // ------------------------------------------------------- the whole menu
    std::printf("\nFactory presets\n");
    {
        // Every preset in the plugin's menu, over the audition loop (drums, bass
        // and a chord at 90 BPM, transport running), must land within 6 dB of
        // the dry loop and stay off the ceiling. The renders found three browser
        // presets 12 to 25 dB out; this keeps it from happening again.
        const auto loop = audition::makeLoop(48000.0, true);
        // loudness, K-weighted (audition::loudness), not RMS: RMS reads a bright
        // distorted preset as quieter than it sounds. By RMS ten presets were
        // 2 to 4.3 dB louder than they measured, and the spread was 11 dB
        const double dryLoud = audition::loudness(loop);
        std::string badValue, outOfWindow, notDistinct, noFeature;
        std::vector<std::vector<float>> outs;
        const auto& all = factoryPresets();
        for (size_t k = 0; k < all.size(); ++k){
            Engine e; e.prepare(48000.0, 128);
            for (int i = 0; i < P.count(); ++i) e.setParam(i, P[i].def);
            for (const auto& kv : audition::parseFlatJson(all[k].json)){
                const int idx = P.index(kv.first);
                float v = 0.0f; const auto& j = kv.second;
                if (idx < 0 || !patchValueToParam(P[idx], j.text, j.isNumber, j.number, j.isBool, j.boolean, v)){
                    badValue += std::string(" [") + all[k].name + ": " + kv.first + "]"; continue;
                }
                e.setParam(idx, v);
            }
            e.seedFrom(0);
            // a preset that routes the pad or a macro is also rendered at every
            // corner of the pad and both ends of each macro: that is where it
            // will be dragged, not only where it is saved
            std::vector<std::pair<std::string, std::vector<std::pair<const char*, float>>>> poses;
            {
                bool routesPerf = false;
                for (int s2 = 0; s2 < numSlots; ++s2)
                    if (e.getParam(P.index(("mS" + std::to_string(s2)).c_str())) >= 5.5f) routesPerf = true;
                if (routesPerf)
                    for (float c : { 0.0f, 100.0f }) for (float d : { 0.0f, 100.0f })
                        poses.push_back({ "at " + f2s(c, 0) + "/" + f2s(d, 0),
                                          { { "xyX", c }, { "xyY", d }, { "mc1", c }, { "mc2", d } } });
            }
            for (const auto& pose : poses){
                Engine e2; e2.prepare(48000.0, 128);
                for (int i = 0; i < P.count(); ++i) e2.setParam(i, e.getParam(i));
                for (const auto& kv : pose.second) e2.setParam(P.index(kv.first), kv.second);
                e2.seedFrom(0);
                audition::Stereo o2 = loop; double ppq2 = 0.0;
                for (size_t i = 0; i < o2.l.size(); i += 128){
                    const int m = static_cast<int>(std::min<size_t>(128, o2.l.size() - i));
                    Transport t; t.bpm = 90; t.ppq = ppq2; t.playing = true; t.valid = true;
                    e2.setTransport(t);
                    float* io2[2] = { o2.l.data() + i, o2.r.data() + i };
                    e2.process(io2, 2, m);
                    ppq2 += m / 48000.0 * 1.5;
                }
                const double rel2 = db(audition::loudness(o2) / dryLoud), pk2 = audition::peak(o2);
                if (std::fabs(rel2) > 6.0 || pk2 > 0.95)
                    outOfWindow += std::string(" [") + all[k].name + " " + pose.first + " " + f2s(rel2, 1)
                                 + " dB, peak " + f2s(pk2, 2) + "]";
            }
            audition::Stereo o = loop; double ppq = 0.0;
            for (size_t i = 0; i < o.l.size(); i += 128){
                const int m = static_cast<int>(std::min<size_t>(128, o.l.size() - i));
                Transport t; t.bpm = 90; t.ppq = ppq; t.playing = true; t.valid = true;
                e.setTransport(t);
                float* io[2] = { o.l.data() + i, o.r.data() + i };
                e.process(io, 2, m);
                ppq += m / 48000.0 * 1.5;
            }
            const double rel = db(audition::loudness(o) / dryLoud), pk = audition::peak(o);
            // within 3 dB, so switching presets compares sounds; Init is the
            // plugin's defaults, a starting point rather than a sound, and is
            // held to the wider window the pad's corners are
            const double window = k == 0 ? 6.0 : 3.0;
            if (std::fabs(rel) > window || pk > 0.95 || !std::isfinite(rel))
                outOfWindow += std::string(" [") + all[k].name + " " + f2s(rel, 1) + " dB, peak " + f2s(pk, 2) + "]";
            outs.push_back(o.l);
            // a plugin-only preset has to use something the browser does not have
            if (k >= presets().size()){
                auto get = [&](const char* id){ return e.getParam(P.index(id)); };
                bool perf = false;
                for (int s2 = 0; s2 < numSlots; ++s2)
                    if (get(("mS" + std::to_string(s2)).c_str()) >= 5.5f) perf = true;
                bool table = false;                  // the Table drive mode, in any band or stage
                for (int b = 0; b < numBands; ++b)
                    for (const char* st : { "a", "b" })
                        if (static_cast<int>(get(("m" + std::to_string(b) + st).c_str())) == static_cast<int>(Mode::Table)) table = true;
                bool stereo = false;                 // a band driven on its mid or its side
                for (int b = 0; b < numBands; ++b) stereo = stereo || get(("st" + std::to_string(b)).c_str()) > 0.5f;
                const bool uses = get("fbMode") > 0.5f || get("fbThru") > 0.5f || get("rhDepth") != 0.0f
                               || get("fltMix") < 100.0f || get("fltPoles") > 1.5f || perf || table || stereo;
                if (!uses) noFeature += std::string(" [") + all[k].name + "]";
            }
        }
        for (size_t a = 0; a < outs.size(); ++a)
            for (size_t b = a + 1; b < outs.size(); ++b)
                if (outs[a] == outs[b]) notDistinct += std::string(" [") + all[a].name + " = " + all[b].name + "]";
        check("the menu is the browser's presets, then the plugin's own",
              all.size() == presets().size() + pluginOnlyPresets().size() && pluginOnlyPresets().size() >= 10,
              std::to_string(presets().size()) + " + " + std::to_string(pluginOnlyPresets().size()));
        check("every value in every factory preset is a real parameter", badValue.empty(), badValue);
        check("every factory preset is within 3 dB of the dry loop's loudness (K-weighted), off the ceiling",
              outOfWindow.empty(), outOfWindow);
        check("no two factory presets sound the same", notDistinct.empty(), notDistinct);
        check("each plugin-only preset uses something only the plugin has", noFeature.empty(), noFeature);
    }

    std::printf("\nTempo sync\n");
    {
        // an LFO on the output gain, with a flat input: the output is the LFO
        auto lfoPatch = [&](int div, double rate){
            Patch p; p.v = { {"mix", 0}, {"osFactor", 0}, {"l1Rate", static_cast<float>(rate)},
                             {"l1Div", static_cast<float>(div)}, {"l1Shape", 0},
                             {"mS0", 1}, {"mA0", 60} };
            const auto& dd = P.dests();
            for (size_t i = 0; i < dd.size(); ++i)
                if (P[dd[i]].id == "outGain") p.v["mD0"] = static_cast<float>(i) + 1;
            return p;
        };
        const double sr = 48000.0;
        std::vector<float> flat(static_cast<size_t>(sr * 2.0), 0.25f);
        const size_t skip = static_cast<size_t>(sr * 0.2);
        {
            const Take t = renderWith(lfoPatch(7, 0.5), flat, sr, 128, true, 120.0, 0.0);
            const double hz = freqOf(t.L, skip, t.L.size(), sr);
            check("a synced LFO counts the host's quarters",
                  std::fabs(hz - 2.0) < 0.1,
                  f2s(hz, 3) + " a second at 120 BPM");
        }
        {
            const Take t = renderWith(lfoPatch(0, 3.0), flat, sr, 128, true, 120.0, 0.0);
            const double hz = freqOf(t.L, skip, t.L.size(), sr);
            check("Free leaves the rate knob in charge",
                  std::fabs(hz - 3.0) < 0.15,
                  f2s(hz, 3) + " Hz");
        }
        {   // stopped, but still moving: a synced LFO at the host's tempo
            const Take t = renderWith(lfoPatch(7, 0.5), flat, sr, 128, false, 120.0, 0.0);
            const double hz = freqOf(t.L, skip, t.L.size(), sr);
            check("a synced LFO keeps moving while the transport is stopped",
                  std::fabs(hz - 2.0) < 0.15,
                  f2s(hz, 3) + " a second, stopped");
        }
    }

    std::printf("\nSample rates\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 }){
        Patch p; p.v = { {"bands", 2}, {"d0a", 12}, {"sb0", 1}, {"crMix", 60},
                         {"fbAmt", 40}, {"fltType", 1} };
        const Result r = render(p, 0.25, sr, 64);
        check(f2s(sr / 1000.0, 1) + " kHz renders finite and bounded",
              r.bad == 0 && r.peak <= 1.0 && r.rms > 0.005,
              "peak " + f2s(r.peak, 3) + " rms " + f2s(r.rms));
    }
    for (int block : { 16, 64, 512, 1024 }){
        const Result r = render({}, 0.2, 48000.0, block);
        check("block size " + std::to_string(block), r.bad == 0 && r.rms > 0.01,
              "rms " + f2s(r.rms));
    }

    // The same audio at another buffer size must be the same sound. Hosts pick
    // the size, and a bounce may use a different one from playback. With the
    // modulation worked out once per host block, six presets differed between
    // 64 and 1024 (Rift-ish by -4.8 dB, the envelope on Downsample stepping
    // every 21 ms). Every preset, plus a fast LFO and the envelope on the
    // drive, over the first two seconds of the audition loop, at 64, 100 (not
    // a multiple of the control step) and 1024 samples a block.
    std::printf("\nBuffer size does not change the sound\n");
    {
        audition::Stereo loop = audition::makeLoop(48000.0, true);
        loop.l.resize(96000); loop.r.resize(96000);
        auto renderAt = [&](const std::vector<std::pair<int, float>>& vals, int block){
            Engine e; e.prepare(48000.0, block);
            for (int i = 0; i < P.count(); ++i) e.setParam(i, P[i].def);
            for (const auto& kv : vals) e.setParam(kv.first, kv.second);
            e.seedFrom(0);
            audition::Stereo o = loop; double ppq = 0.0;
            for (size_t i = 0; i < o.l.size(); i += static_cast<size_t>(block)){
                const int m = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), o.l.size() - i));
                Transport t; t.bpm = 90; t.ppq = ppq; t.playing = true; t.valid = true;
                e.setTransport(t);
                float* io[2] = { o.l.data() + i, o.r.data() + i };
                e.process(io, 2, m);
                ppq += m / 48000.0 * 1.5;
            }
            return o;
        };
        auto diffDb = [](const audition::Stereo& a, const audition::Stereo& b){
            double d = 0.0, s = 0.0;
            for (size_t i = 4800; i < a.l.size(); ++i){
                d += (a.l[i] - b.l[i]) * (a.l[i] - b.l[i]) + (a.r[i] - b.r[i]) * (a.r[i] - b.r[i]);
                s += a.l[i] * a.l[i] + a.r[i] * a.r[i];
            }
            return d <= 0.0 ? -300.0 : 10.0 * std::log10(d / s);
        };
        std::vector<std::pair<std::string, std::vector<std::pair<int, float>>>> cases;
        for (const auto& pr : factoryPresets()){
            std::vector<std::pair<int, float>> vals;
            for (const auto& kv : audition::parseFlatJson(pr.json)){
                const int idx = P.index(kv.first); float v = 0.0f; const auto& j = kv.second;
                if (idx >= 0 && patchValueToParam(P[idx], j.text, j.isNumber, j.number, j.isBool, j.boolean, v))
                    vals.push_back({ idx, v });
            }
            cases.push_back({ pr.name, vals });
        }
        {
            auto I = [&](const char* s){ return P.index(s); };
            cases.push_back({ "a 20 Hz LFO and the envelope on the drive", {
                { I("d0a"), 6.0f }, { I("m0a"), static_cast<float>(Mode::Fold) }, { I("l1Rate"), 20.0f },
                { I("mS0"), 1.0f }, { I("mD0"), 1.0f + static_cast<float>(
                      std::find(P.dests().begin(), P.dests().end(), I("d0a")) - P.dests().begin()) },
                { I("mA0"), 60.0f }, { I("mS1"), 3.0f }, { I("mD1"), 1.0f + static_cast<float>(
                      std::find(P.dests().begin(), P.dests().end(), I("redux")) - P.dests().begin()) },
                { I("mA1"), 50.0f }, { I("crMix"), 100.0f } } });
        }
        double worst = -300.0; std::string worstName, over;
        for (const auto& c : cases){
            const audition::Stereo ref = renderAt(c.second, 64);
            for (int block : { 100, 1024 }){
                const double d = diffDb(ref, renderAt(c.second, block));
                if (d > worst){ worst = d; worstName = c.first + " at " + std::to_string(block); }
                if (d > -50.0) over += " [" + c.first + " at " + std::to_string(block) + ": " + f2s(d, 1) + " dB]";
            }
        }
        check("every preset sounds the same at 64, 100 and 1024 samples a block (within -50 dB)",
              over.empty(), over.empty() ? "worst " + f2s(worst, 1) + " dB, " + worstName : over);
    }

    std::printf("\n%d assertions passed, %zu failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.c_str());
    return failures.empty() ? 0 : 1;
}
