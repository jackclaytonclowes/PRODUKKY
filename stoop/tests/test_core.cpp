// test_core.cpp — measurements, not vibes.
//
// Nobody involved in writing this has heard it: there is no audio device where
// it was built. So every claim the panel makes gets measured instead: the
// compressor's curve, its attack and release and that its make-up holds the
// level; how far and how often the turntable moves the pitch; that the noise
// sits on top of the compression rather than under it, and that the scratch
// comes round once a revolution; that the pitch shifter lands on the note it
// says; the filter's marks and slopes; six decibels a bit; and that nothing
// the panel dims, and nothing about the host's block size, changes a bit of
// the output.
//
//   npm run test:stoop          (VERBOSE=1 prints the number behind every check)
#include "StoopCore.h"
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
#include <functional>

using namespace stoop;

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
        if (i < 0){ std::printf("  (no parameter called %s)\n", kv.first.c_str()); std::exit(2); }
        e.setParam(i, kv.second);
    }
}

// magnitude of one frequency, by Goertzel
static double goertzel(const std::vector<float>& x, double freq, double sr, int from, int to = -1){
    if (to < 0) to = static_cast<int>(x.size());
    const int n = to - from;
    if (n <= 0) return 0.0;
    const double w = 2.0 * M_PI * freq / sr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = from; i < to; ++i){
        const double s = x[static_cast<size_t>(i)] + c * s1 - s2;
        s2 = s1; s1 = s;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / n;
}
// the noise a quantiser adds: fit the test tone exactly and take the rest
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
        const double e = x[static_cast<size_t>(from + i)] - (a * std::sin(w * i) + b * std::cos(w * i));
        p += e * e;
    }
    return p / n;
}
static double totalPower(const std::vector<float>& x, int from, int to = -1){
    if (to < 0) to = static_cast<int>(x.size());
    double p = 0.0; int n = 0;
    for (int i = from; i < to; ++i){ p += static_cast<double>(x[static_cast<size_t>(i)]) * x[static_cast<size_t>(i)]; ++n; }
    return n > 0 ? p / n : 0.0;
}
static double peakOf(const std::vector<float>& x, int from, int to = -1){
    if (to < 0) to = static_cast<int>(x.size());
    double p = 0.0;
    for (int i = from; i < to; ++i) p = std::max(p, static_cast<double>(std::fabs(x[static_cast<size_t>(i)])));
    return p;
}

struct Render { std::vector<float> l, r; long bad = 0; double peak = 0.0; };

static Render run2(const Patch& patch, const std::vector<float>& inL, const std::vector<float>& inR,
                   double sr = 48000.0, int block = 128, int64_t seed = 0,
                   const std::function<void(Engine&, int)>& perBlock = nullptr){
    Engine e;
    e.prepare(sr, block);
    applyPatch(e, patch);
    e.seedFrom(seed);
    Render out;
    out.l = inL; out.r = inR;
    const int n = static_cast<int>(inL.size());
    for (int i = 0; i < n; i += block){
        if (perBlock) perBlock(e, i);
        const int m = std::min(block, n - i);
        float* io[2] = { out.l.data() + i, out.r.data() + i };
        e.process(io, 2, m);
    }
    for (auto* ch : { &out.l, &out.r })
        for (float v : *ch){
            if (!std::isfinite(v)) ++out.bad;
            else out.peak = std::max(out.peak, static_cast<double>(std::fabs(v)));
        }
    return out;
}
static Render run(const Patch& patch, const std::vector<float>& in, double sr = 48000.0, int block = 128){
    return run2(patch, in, in, sr, block);
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
static int latencyOf(double sr = 48000.0){ Engine e; e.prepare(sr, 128); return e.latencySamples(); }

// The pitch of a steady tone, cycle by cycle: the times of its upward zero
// crossings, found to a fraction of a sample, give one frequency per cycle.
// Returned as cents against f0, at the middle of each cycle (seconds).
struct Track { std::vector<double> t, cents; };
static Track pitchTrack(const std::vector<float>& x, double sr, double f0, int from){
    Track tr;
    double last = -1.0;
    for (size_t i = static_cast<size_t>(from) + 1; i < x.size(); ++i){
        if (x[i - 1] < 0.0f && x[i] >= 0.0f){
            const double t = (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (last >= 0.0){
                tr.t.push_back(0.5 * (t + last) / sr);
                tr.cents.push_back(1200.0 * std::log2(sr / (t - last) / f0));
            }
            last = t;
        }
    }
    return tr;
}
// the size of one frequency's sine in a track, over whole periods of it
static double trackComponent(const Track& tr, double hz){
    const double span = tr.t.back() - tr.t.front();
    const double periods = std::floor(span * hz);
    const double end = tr.t.front() + periods / hz;
    std::complex<double> acc = 0.0; int n = 0;
    for (size_t k = 0; k < tr.t.size() && tr.t[k] < end; ++k){
        acc += tr.cents[k] * std::polar(1.0, -2.0 * M_PI * hz * tr.t[k]); ++n;
    }
    return n > 0 ? 2.0 * std::abs(acc) / n : 0.0;
}

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
    const int lat = latencyOf(sr);
    // the sections neutral, so a measurement sees only the section it is about
    const Patch quiet = { {"comp", 0}, {"noise", 0}, {"wow", 0} };
    auto with = [&](Patch base, const Patch& more){ for (const auto& kv : more) base[kv.first] = kv.second; return base; };

    std::printf("\nThe table\n");
    check("the table is complete", P.count() == 22, std::to_string(P.count()) + " parameters");
    check("the vinyl sim is on by default, with a little of each of its knobs",
          P[P.index("vinylOn")].def == 1.0f && P[P.index("comp")].def > 0 && P[P.index("noise")].def > 0
          && P[P.index("wow")].def > 0);
    check("and the other three sections are off",
          P[P.index("pitchOn")].def == 0.0f && P[P.index("fltOn")].def == 0.0f && P[P.index("lofiOn")].def == 0.0f);
    check("the pitch shifter covers two octaves either way", P[P.index("pitch")].min == -24.0f && P[P.index("pitch")].max == 24.0f);

    testHistory();

    // ------------------------------------------------------------- the path
    std::printf("\nThe path\n");
    {
        const auto in = noise(0.3, 0.3, sr, 3);
        const auto neutral = run(quiet, in);
        const auto off = run({ {"vinylOn", 0} }, in);
        double worst = 0.0;
        for (size_t i = static_cast<size_t>(lat); i < in.size(); ++i)
            worst = std::max(worst, std::fabs(static_cast<double>(neutral.l[i]) - in[i - static_cast<size_t>(lat)]));
        check("with its knobs at zero the box is a pure delay of the latency it reports ("
              + std::to_string(lat) + " samples, " + f2s(lat / sr * 1000.0, 1) + " ms)",
              worst == 0.0, "worst " + f2s(worst, 9));
        check("  ... and the vinyl sim at zero is exactly the vinyl sim switched off",
              neutral.l == off.l && neutral.r == off.r);
        const auto half = run(with(quiet, { {"mix", 50} }), in);
        double worstHalf = 0.0;
        for (size_t i = static_cast<size_t>(lat); i < in.size(); ++i)
            worstHalf = std::max(worstHalf, std::fabs(static_cast<double>(half.l[i]) - in[i - static_cast<size_t>(lat)]));
        check("the dry path is in step with the wet: a half mix of the two is the input",
              worstHalf < 1e-7, f2s(worstHalf, 9));
        check("the latency is the same at every setting, so the host never has to re-learn it", [&]{
            for (const auto& p : presets()){
                Engine e; e.prepare(sr, 128);
                Patch patch; for (const auto& kv : p.values) patch[kv.first] = kv.second;
                applyPatch(e, patch);
                if (e.latencySamples() != lat) return false;
            }
            return true; }());
        // switching the filter on (wide open) does not move the audio in time
        std::vector<float> imp(4800, 0.0f); imp[100] = 0.5f;
        const auto fi = run(with(quiet, { {"fltOn", 1}, {"fltFreq", 20000}, {"fltReso", 0} }), imp);
        int at = 0; for (int i = 1; i < static_cast<int>(fi.l.size()); ++i) if (std::fabs(fi.l[i]) > std::fabs(fi.l[at])) at = i;
        // within a sample: a two-pole low pass at 20 kHz delays the lows by half of one
        check("with the filter on and open, an impulse still comes out at the reported latency",
              std::abs(at - (100 + lat)) <= 1, "at " + std::to_string(at - 100) + " samples");
    }

    // ----------------------------------------------------------- the cutting
    std::printf("\nComp, the record's cutting\n");
    {
        // a 1 kHz tone at a range of levels through Comp 100, the level out
        // read once the gain has settled
        auto levelOut = [&](double comp, double inDb){
            const auto in = sine(1000.0, std::pow(10.0, inDb / 20.0), 0.6, sr);
            const auto r = run(with(quiet, { {"comp", static_cast<float>(comp)} }), in);
            return dBOf(goertzel(r.l, 1000.0, sr, static_cast<int>(0.4 * sr), static_cast<int>(0.6 * sr)));
        };
        const double o6 = levelOut(100, -6), o18 = levelOut(100, -18), o40 = levelOut(100, -40), o46 = levelOut(100, -46);
        const double slopeTop = (o6 - o18) / 12.0, slopeBottom = (o40 - o46) / 6.0;
        check("Comp 100 is 10:1 above its threshold: 12 dB more in is 1.2 dB more out",
              std::fabs(slopeTop - 0.1) < 0.02, "slope " + f2s(slopeTop, 3) + " (" + f2s(o18, 1) + " to " + f2s(o6, 1) + " dBFS)");
        check("  ... and 1:1 below it, the quiet parts lifted by the make-up",
              std::fabs(slopeBottom - 1.0) < 0.03 && std::fabs(o46 + 46.0 - VinylComp::makeupDb(1.0)) < 0.3,
              "slope " + f2s(slopeBottom, 3) + ", -46 dBFS comes out at " + f2s(o46, 1) + " (make-up "
              + f2s(VinylComp::makeupDb(1.0), 1) + " dB)");
        const double h3 = levelOut(50, -3), h15 = levelOut(50, -15), h30 = levelOut(50, -30);
        check("Comp 50 is gentler: 5.5:1 above -21 dBFS, with the knee below", std::fabs((h3 - h15) / 12.0 - 1.0 / 5.5) < 0.02
              && (h15 - h30) / 15.0 > 0.4,
              "slope " + f2s((h3 - h15) / 12.0, 3) + " on top, " + f2s((h15 - h30) / 15.0, 3) + " through the knee");
        // how much the knob takes off a loud tone, for the README
        std::string table;
        for (double c : { 25.0, 50.0, 75.0, 100.0 })
            table += " " + f2s(c, 0) + "%: " + f2s(VinylComp::curve(-6.0, c / 100.0) + 6.0, 1) + " dB";
        check("the reduction on a -6 dBFS peak grows with the knob", true, table);
    }
    {
        // attack and release, measured on the output: a 4 kHz tone steps from
        // -36 to -26 dBFS and back, and the output's peak in each 0.25 ms (one
        // cycle) shows the gain closing and opening. A bigger step would
        // overshoot into the soft limit for its first milliseconds and read slow
        const int n = static_cast<int>(1.7 * sr), up = static_cast<int>(0.4 * sr), down = static_cast<int>(0.7 * sr);
        std::vector<float> in(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i){
            const double a = std::pow(10.0, ((i >= up && i < down) ? -26.0 : -36.0) / 20.0);
            in[static_cast<size_t>(i)] = static_cast<float>(a * std::sin(2.0 * M_PI * 4000.0 * i / sr));
        }
        const auto r = run(with(quiet, { {"comp", 100} }), in);
        auto env = [&](int at){ return dBOf(peakOf(r.l, at + lat, at + lat + 12) / peakOf(in, at, at + 12)); };   // gain, dB
        // the gain in dB falls exponentially to where it settles: the time
        // constant is when 63% of the way has been covered
        auto timeConstant = [&](int from, int span){
            const double g0 = env(from), gEnd = env(from + span);
            for (int k = 0; k < span; k += 12)
                if (std::fabs(env(from + k) - g0) >= 0.632 * std::fabs(gEnd - g0)) return k / sr;
            return 1.0;
        };
        const double att = timeConstant(up, static_cast<int>(0.05 * sr));
        const double rel = timeConstant(down, static_cast<int>(0.95 * sr));
        check("the attack is 3 ms", std::fabs(att - 0.003) < 0.0008, f2s(att * 1000.0, 2) + " ms");
        // the level is held 10 ms before it falls, so the gain starts to come
        // back that much after the drop
        check("the release is 120 ms, after the detector's 10 ms hold", std::fabs(rel - 0.130) < 0.010,
              f2s(rel * 1000.0, 1) + " ms from the drop");
    }
    {
        // the two channels share one gain: a loud tone on the left takes a
        // quiet one on the right down by the same amount
        const auto L = sine(1000.0, 0.5, 0.6, sr), R = sine(1500.0, 0.03, 0.6, sr);
        const auto r = run2(with(quiet, { {"comp", 100} }), L, R);
        const int a = static_cast<int>(0.4 * sr), b = static_cast<int>(0.6 * sr);
        const double gl = dBOf(goertzel(r.l, 1000.0, sr, a, b) / 0.5), gr = dBOf(goertzel(r.r, 1500.0, sr, a, b) / 0.03);
        check("the channels are linked: the quiet side moves with the loud one", std::fabs(gl - gr) < 0.3,
              "left " + f2s(gl, 2) + " dB, right " + f2s(gr, 2) + " dB");
    }
    {
        // the make-up holds the loudness, and the compression shows up as a
        // narrower spread of short-term levels (10 ms windows), which is what
        // Comp is for: the hits come down to the tails
        const auto loop = audition::makeLoop(sr, true);
        auto spread = [&](const Render& r){
            std::vector<double> lv;
            for (size_t i = 0; i + 480 <= r.l.size(); i += 480){
                double p = 0.0;
                for (size_t k = i; k < i + 480; ++k) p += r.l[k] * r.l[k] + r.r[k] * r.r[k];
                const double d = 10.0 * std::log10(p / 960.0 + 1e-30);
                if (d > -70.0) lv.push_back(d);
            }
            double m = 0.0, v = 0.0;
            for (double d : lv) m += d;
            m /= static_cast<double>(lv.size());
            for (double d : lv) v += (d - m) * (d - m);
            return std::sqrt(v / static_cast<double>(lv.size()));
        };
        std::string levels; double worst = 0.0, crest0 = 0.0, crest100 = 0.0, ref = 0.0;
        for (double c : { 0.0, 25.0, 50.0, 75.0, 100.0 }){
            const auto r = run2(with(quiet, { {"comp", static_cast<float>(c)} }), loop.l, loop.r);
            audition::Stereo s; s.sr = sr; s.l = r.l; s.r = r.r;
            const double loud = dBOf(audition::loudness(s));
            if (c == 0.0) ref = loud;
            const double crest = spread(r);
            if (c == 0.0) crest0 = crest;
            if (c == 100.0) crest100 = crest;
            worst = std::max(worst, std::fabs(loud - ref));
            levels += " " + f2s(c, 0) + "%: " + (loud - ref >= 0 ? "+" : "") + f2s(loud - ref, 1);
        }
        check("the automatic make-up keeps the loop within 2 dB of its loudness at every Comp", worst < 2.0, levels + " dB");
        check("  ... while Comp 100 narrows the spread of its short-term levels by a quarter or more", crest100 < crest0 * 0.75,
              "standard deviation of 10 ms levels " + f2s(crest0, 1) + " dB to " + f2s(crest100, 1) + " dB");
    }

    // --------------------------------------------------------- the turntable
    std::printf("\nWow flutter, the turntable\n");
    {
        std::string speeds; bool ok = true;
        for (int s = 0; s < 3; ++s){
            const double rev = speedRpm[s] / 60.0;
            const auto in = sine(1000.0, 0.4, 8.0, sr);
            const auto r = run(with(quiet, { {"wow", 100}, {"speed", static_cast<float>(s)} }), in);
            const auto tr = pitchTrack(r.l, sr, 1000.0, lat + 4800);
            const double once = trackComponent(tr, rev);
            ok = ok && std::fabs(once - 17.2) < 2.0;
            speeds += " " + std::string(s == 0 ? "33" : (s == 1 ? "45" : "78")) + ": " + f2s(once, 1) + " cents at " + f2s(rev, 3) + " Hz;";
        }
        check("at full, the pitch swings 1% (17 cents) once a revolution, at each speed", ok, speeds);
    }
    {
        const auto in = sine(1000.0, 0.4, 8.0, sr);
        const auto r = run(with(quiet, { {"wow", 100} }), in);
        const auto tr = pitchTrack(r.l, sr, 1000.0, lat + 4800);
        double mean = 0.0, worst = 0.0;
        for (double c : tr.cents){ mean += c; worst = std::max(worst, std::fabs(c)); }
        mean /= static_cast<double>(tr.cents.size());
        const double f1 = trackComponent(tr, Turntable::flutterHz1), f2 = trackComponent(tr, Turntable::flutterHz2);
        check("flutter moves it too, at 6.3 and 10.1 Hz", f1 > 0.8 && f1 < 3.0 && f2 > 0.8 && f2 < 3.0,
              f2s(f1, 2) + " and " + f2s(f2, 2) + " cents");
        check("  ... the worst of all three together stays under 30 cents", worst < 30.0 && worst > 15.0, f2s(worst, 1) + " cents");
        check("  ... and on average nothing is detuned", std::fabs(mean) < 1.0, f2s(mean, 2) + " cents");
        // a quarter of the knob is a quarter of the movement
        const auto q = run(with(quiet, { {"wow", 25} }), in);
        const double qOnce = trackComponent(pitchTrack(q.l, sr, 1000.0, lat + 4800), 100.0 / 3.0 / 60.0);
        check("the knob is linear: 25% is a quarter of the swing", std::fabs(qOnce - 17.2 / 4.0) < 0.6, f2s(qOnce, 2) + " cents");
    }
    {
        // the platter takes a moment to change speed, so a switch mid-tone
        // never jumps the pitch
        const auto in = sine(1000.0, 0.4, 3.0, sr);
        const auto r = run2(with(quiet, { {"wow", 100} }), in, in, sr, 128, 0, [&](Engine& e, int i){
            if (i >= static_cast<int>(1.5 * sr)) e.setParam(P.index("speed"), 2.0f); });
        const auto tr = pitchTrack(r.l, sr, 1000.0, lat + 4800);
        double jump = 0.0;
        for (size_t k = 1; k < tr.cents.size(); ++k) jump = std::max(jump, std::fabs(tr.cents[k] - tr.cents[k - 1]));
        // the yardstick is the cubic interpolation's own jitter as the read
        // position sweeps between samples, about half a cent on a 1 kHz tone
        // (a step of 20 cents is what a one-pole glide did)
        check("switching Speed mid-tone never jumps the pitch", jump < 2.0, "largest step between cycles " + f2s(jump, 2) + " cents");
    }

    // ------------------------------------------------------------ the surface
    std::printf("\nNoise, the surface\n");
    {
        const std::vector<float> silence(static_cast<size_t>(4.0 * sr), 0.0f);
        const auto none = run(with(quiet, {}), silence);
        check("at 0 it is silent", peakOf(none.l, 0) == 0.0 && peakOf(none.r, 0) == 0.0);
        std::string lv;
        double rmsFull = 0.0, pkFull = 0.0;
        for (double nz : { 25.0, 50.0, 100.0 }){
            const auto r = run(with(quiet, { {"noise", static_cast<float>(nz)} }), silence);
            const double rmsDb = 10.0 * std::log10(totalPower(r.l, lat) + 1e-30), pk = dBOf(peakOf(r.l, lat));
            lv += " " + f2s(nz, 0) + "%: " + f2s(rmsDb, 1) + " dBFS RMS, peaks " + f2s(pk, 1) + ";";
            if (nz == 100.0){ rmsFull = rmsDb; pkFull = pk; }
        }
        check("at full it is a noisy record, not a broken one", rmsFull > -45.0 && rmsFull < -25.0 && pkFull < -3.0, lv);
        const auto a = run(with(quiet, { {"noise", 60} }), silence);
        const auto b = run(with(quiet, { {"noise", 60} }), silence);
        const auto c = run2(with(quiet, { {"noise", 60} }), silence, silence, sr, 128, 48000);
        check("the same song position gives the same crackle, bit for bit", a.l == b.l && a.r == b.r);
        check("  ... and another position, another", a.l != c.l);
        check("  ... and the two channels crackle differently", a.l != a.r);
    }
    {
        // the crackle is on the surface, so the cutting never pumps it: with a
        // loud tone squashed hard, the output is exactly the squashed tone plus
        // the noise on its own
        const auto tone = sine(110.0, 0.5, 1.0, sr);
        const std::vector<float> silence(tone.size(), 0.0f);
        const Patch both = { {"comp", 100}, {"noise", 80}, {"wow", 60}, {"safety", 0} };
        const auto all = run(both, tone);
        const auto toneOnly = run(with(both, { {"noise", 0} }), tone);
        const auto noiseOnly = run(both, silence);
        double worst = 0.0;
        for (size_t i = 0; i < tone.size(); ++i)
            worst = std::max(worst, std::fabs(static_cast<double>(all.l[i]) - toneOnly.l[i] - noiseOnly.l[i]));
        check("the noise is not compressed: tone plus noise is the tone, and the noise as it is on silence",
              worst < 1e-6, "worst " + f2s(worst, 9));
    }
    {
        // the scratch comes round once a revolution: fold the output over the
        // revolution (78 rpm, twenty of them) and one place stands out
        const double rev = 78.0 / 60.0, period = sr / rev;
        const std::vector<float> silence(static_cast<size_t>(20.5 * period), 0.0f);
        const auto r = run(with(quiet, { {"noise", 60}, {"speed", 2} }), silence);
        const int bins = static_cast<int>(period / 24.0);              // half-millisecond bins
        std::vector<double> fold(static_cast<size_t>(bins), 0.0);
        for (size_t i = 0; i < r.l.size(); ++i){
            const double ph = std::fmod(static_cast<double>(i), period) / period;
            fold[static_cast<size_t>(std::min(bins - 1, static_cast<int>(ph * bins)))] += std::fabs(r.l[i]) + std::fabs(r.r[i]);
        }
        auto sorted = fold; std::sort(sorted.begin(), sorted.end());
        const int top = static_cast<int>(std::max_element(fold.begin(), fold.end()) - fold.begin());
        const double where = (top + 0.5) / bins * period;               // samples into the revolution
        const double expect = Turntable::scratchAngle * period + lat;
        check("the scratch ticks once a revolution, in the same place",
              *std::max_element(fold.begin(), fold.end()) > 8.0 * sorted[sorted.size() / 2] && std::fabs(where - expect) < 0.002 * sr,
              f2s(*std::max_element(fold.begin(), fold.end()) / sorted[sorted.size() / 2], 1) + "x the median, at "
              + f2s(where / sr * 1000.0, 1) + " ms (expected " + f2s(expect / sr * 1000.0, 1) + ")");
    }

    // ------------------------------------------------------- the pitch shifter
    // A tone whose period does not divide the 40 ms window is spliced out of
    // phase, so its energy spreads into sidebands at the splice rate (12 Hz
    // for a fifth up): the warble a shifter of this kind has. So the pitch is
    // measured as the energy within half a semitone of a note, not in one bin.
    std::printf("\nPitch\n");
    {
        auto band = [&](const std::vector<float>& x, double hz){
            const size_t n = 32768, from = static_cast<size_t>(0.4 * sr);
            std::vector<std::complex<double>> a(n);
            for (size_t i = 0; i < n; ++i) a[i] = x[from + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1)));
            fftInPlace(a);
            double p = 0.0;
            const double lo = hz * std::pow(2.0, -0.5 / 12.0), hi = hz * std::pow(2.0, 0.5 / 12.0);
            for (size_t k = 1; k < n / 2; ++k){ const double f = k * sr / n; if (f >= lo && f <= hi) p += std::norm(a[k]); }
            return std::sqrt(p);
        };
        auto shifted = [&](double semis, double inHz, double balance = 100, double fb = 0){
            const auto in = sine(inHz, 0.3, 1.2, sr);
            return run(with(quiet, { {"pitchOn", 1}, {"pitch", static_cast<float>(semis)},
                                     {"pitchBal", static_cast<float>(balance)}, {"pitchFb", static_cast<float>(fb)} }), in);
        };
        {
            const auto r = shifted(12, 500);
            const double d = dBOf(band(r.l, 1000.0) / band(r.l, 500.0));
            check("+12 puts 500 Hz at 1 kHz", d > 30.0, f2s(d, 1) + " dB over the original");
            const auto in = sine(500.0, 0.3, 1.2, sr);
            const double lv = dBOf(band(r.l, 1000.0) / band(in, 500.0));
            check("  ... at the level it came in", std::fabs(lv) < 1.0, f2s(lv, 2) + " dB");
        }
        {
            const auto r = shifted(-12, 1000);
            const double d = dBOf(band(r.l, 500.0) / band(r.l, 1000.0));
            check("-12 puts 1 kHz at 500 Hz", d > 30.0, f2s(d, 1) + " dB over the original");
        }
        {
            const auto r = shifted(7, 440);
            const double fifth = 440.0 * std::pow(2.0, 7.0 / 12.0);
            const double d = dBOf(band(r.l, fifth) / band(r.l, 440.0));
            check("+7 is a fifth: 440 Hz comes out at 659.3", d > 20.0, f2s(d, 1) + " dB over the original");
            // how much of what comes out is within half a semitone of the fifth:
            // the rest is the splices' warble, further out
            const auto in = sine(440.0, 0.3, 1.2, sr);
            const double kept = dBOf(band(r.l, fifth) / band(in, 440.0));
            check("  ... with the splices' warble costing it under 3 dB", kept > -3.0, f2s(kept, 2) + " dB within half a semitone");
            const auto h = shifted(7, 440, 50);
            const double both = dBOf(band(h.l, fifth) / band(h.l, 440.0));
            check("Balance 50 keeps both, the original and the fifth", std::fabs(both) < 6.0, f2s(both, 1) + " dB between them");
        }
        {
            // feedback shifts the shifted signal again: 250 Hz up an octave
            // with feedback makes 500, then 1000, then 2000, each quieter
            const auto r = shifted(12, 250, 100, 70);
            const double h1 = band(r.l, 500.0), h2 = band(r.l, 1000.0), h3 = band(r.l, 2000.0);
            check("Feedback climbs: an octave up, then two, then three, each quieter",
                  h1 > h2 && h2 > h3 && dBOf(h3 / h1) > -30.0,
                  "1 kHz " + f2s(dBOf(h2 / h1), 1) + " dB and 2 kHz " + f2s(dBOf(h3 / h1), 1) + " dB against 500 Hz");
            const auto plain = shifted(12, 250);
            check("  ... and without it there is only the first", dBOf(band(plain.l, 1000.0) / band(plain.l, 500.0)) < -30.0);
        }
        {
            const auto in = noise(0.5, 4.0, sr, 21);
            Render worst;
            for (double st : { -24.0, -5.0, 0.0, 7.0, 24.0 }){
                const auto r = run(with(quiet, { {"pitchOn", 1}, {"pitch", static_cast<float>(st)}, {"pitchFb", 100} }), in);
                if (r.bad || r.peak > worst.peak) worst = r;
            }
            check("full feedback at any pitch dies away rather than running off", worst.bad == 0 && worst.peak <= 1.0,
                  "peak " + f2s(worst.peak, 3));
        }
    }

    // --------------------------------------------------------- filter + drive
    std::printf("\nFilter and drive\n");
    {
        auto gainAt = [&](int type, double cutoff, double hz, double reso = 0, double drive = 0, double amp = 0.01){
            const auto in = sine(hz, amp, 0.5, sr);
            const auto r = run(with(quiet, { {"fltOn", 1}, {"fltType", static_cast<float>(type)},
                                             {"fltFreq", static_cast<float>(cutoff)}, {"fltReso", static_cast<float>(reso)},
                                             {"fltDrive", static_cast<float>(drive)} }), in);
            return dBOf(goertzel(r.l, hz, sr, static_cast<int>(0.2 * sr)) / amp);
        };
        const double lpMark = gainAt(0, 1000, 1000), lpPass = gainAt(0, 1000, 100), lpTwo = gainAt(0, 1000, 4000);
        check("low pass: 3 dB down on the Cutoff mark", std::fabs(lpMark + 3.01) < 0.2, f2s(lpMark, 2) + " dB");
        check("  ... flat below it", std::fabs(lpPass) < 0.1, f2s(lpPass, 2) + " dB at 100 Hz");
        check("  ... and two poles: 12 dB an octave, 24 dB down two octaves up", std::fabs(lpTwo + 24.1) < 1.0, f2s(lpTwo, 2) + " dB at 4 kHz");
        const double hpMark = gainAt(2, 1000, 1000), hpTwo = gainAt(2, 1000, 250);
        check("high pass: 3 dB down on the mark, 24 dB two octaves under", std::fabs(hpMark + 3.01) < 0.2 && std::fabs(hpTwo + 24.1) < 1.0,
              f2s(hpMark, 2) + " / " + f2s(hpTwo, 2) + " dB");
        const double bp = gainAt(1, 1000, 1000, 50);
        check("band pass: unity on the mark", std::fabs(bp) < 0.2, f2s(bp, 2) + " dB");
        const double peak = gainAt(0, 1000, 1000, 100);
        check("Resonance 100 is a Q of 20: +26 dB on the mark", std::fabs(peak - 26.0) < 0.5, f2s(peak, 2) + " dB");
    }
    {
        // drive: the harmonics of a -12 dBFS 100 Hz tone, with the filter open
        auto thd = [&](double drive){
            const auto in = sine(100.0, 0.25, 0.5, sr);
            const auto r = run(with(quiet, { {"fltOn", 1}, {"fltFreq", 20000}, {"fltReso", 0}, {"fltDrive", static_cast<float>(drive)} }), in);
            const int from = static_cast<int>(0.2 * sr);
            double h = 0.0; for (int k = 2; k <= 9; ++k) h += std::pow(goertzel(r.l, 100.0 * k, sr, from), 2.0);
            return std::make_pair(dBOf(std::sqrt(h) / goertzel(r.l, 100.0, sr, from)), dBOf(goertzel(r.l, 100.0, sr, from) / 0.25));
        };
        const auto d0 = thd(0), d50 = thd(50), d75 = thd(75), d100 = thd(100);
        check("Drive 0 is clean", d0.first < -90.0, f2s(d0.first, 1) + " dB of harmonics");
        check("  ... and it gets dirtier all the way up", d50.first > -70.0 && d75.first > d50.first + 10.0 && d100.first > -15.0,
              f2s(d50.first, 1) + ", " + f2s(d75.first, 1) + " and " + f2s(d100.first, 1) + " dB of harmonics at 50, 75 and 100");
        // half the drive is level: quiet signals come up 13 dB at full, while
        // a loud one barely moves
        const auto quietIn = sine(100.0, 0.01, 0.5, sr);
        const auto qr = run(with(quiet, { {"fltOn", 1}, {"fltFreq", 20000}, {"fltReso", 0}, {"fltDrive", 100} }), quietIn);
        const double lift = dBOf(goertzel(qr.l, 100.0, sr, static_cast<int>(0.2 * sr)) / 0.01);
        check("  ... with half the drive heard as level: -40 dBFS comes up 13 dB at full, -12 dBFS hardly moves",
              std::fabs(lift - 13.0) < 0.3 && std::fabs(d100.second) < 3.0,
              "+" + f2s(lift, 2) + " dB and " + f2s(d100.second, 2) + " dB");
    }
    {
        // oversampling: at full drive a 5 kHz tone's 7th and 9th harmonics
        // (35 and 45 kHz) fold back past the host's Nyquist to 13 and 3 kHz at
        // 1x; at 4x they are filtered out first. Measured against the 15 kHz
        // harmonic, which is real
        auto aliases = [&](int factor){
            Engine e; e.setOversampling(factor); e.prepare(sr, 128);
            applyPatch(e, with(quiet, { {"fltOn", 1}, {"fltFreq", 20000}, {"fltReso", 0}, {"fltDrive", 100} }));
            e.seedFrom(0);
            auto x = sine(5000.0, 0.5, 2.0, sr);
            auto y = x;
            for (size_t i = 0; i < x.size(); i += 128){
                float* io[2] = { x.data() + i, y.data() + i };
                e.process(io, 2, static_cast<int>(std::min<size_t>(128, x.size() - i)));
            }
            const int from = static_cast<int>(0.5 * sr);
            return dBOf(std::max(goertzel(x, 13000.0, sr, from), goertzel(x, 3000.0, sr, from)) / goertzel(x, 15000.0, sr, from));
        };
        const double at1 = aliases(1), at4 = aliases(4);
        check("four times oversampling takes the drive's audible aliases 50 dB under the harmonics",
              at4 < -50.0 && at4 < at1 - 30.0, f2s(at1, 1) + " dB at 1x, " + f2s(at4, 1) + " dB at 4x");
    }

    // ------------------------------------------------------------------ lo-fi
    std::printf("\nLo-fi\n");
    {
        // a tone with a long period (487 samples, a prime), so the rounding
        // error does not repeat every few samples and is noise, not a pattern
        const double f = sr / 487.0;
        auto floor = [&](double bits){
            const auto in = sine(f, 0.5, 1.0, sr);
            const auto r = run(with(quiet, { {"lofiOn", 1}, {"lofiRate", 48000}, {"lofiBits", static_cast<float>(bits)} }), in);
            return 10.0 * std::log10(residualPower(r.l, f, sr, lat + 480) + 1e-30);
        };
        const double b16 = floor(16), b12 = floor(12), b8 = floor(8);
        check("six decibels a bit: 16 to 12 costs 24 dB of noise floor, 12 to 8 the same again",
              std::fabs(b12 - b16 - 24.1) < 2.5 && std::fabs(b8 - b12 - 24.1) < 2.5,
              f2s(b16, 1) + ", " + f2s(b12, 1) + ", " + f2s(b8, 1) + " dB");
        const auto in = sine(7000.0, 0.3, 0.5, sr);
        const auto slow = run(with(quiet, { {"lofiOn", 1}, {"lofiRate", 12000}, {"lofiBits", 16} }), in);
        const auto fast = run(with(quiet, { {"lofiOn", 1}, {"lofiRate", 48000}, {"lofiBits", 16} }), in);
        const int from = lat + 2400;
        const double fold = dBOf(goertzel(slow.l, 5000.0, sr, from) / goertzel(slow.l, 7000.0, sr, from));
        const double none = dBOf(goertzel(fast.l, 5000.0, sr, from) / goertzel(fast.l, 7000.0, sr, from));
        check("Rate has no filter in front: 7 kHz at a 12 kHz clock folds back to 5 kHz", fold > -6.0 && none < -80.0,
              f2s(fold, 1) + " dB against the tone (" + f2s(none, 1) + " dB at 48 kHz)");
    }

    // ----------------------------------------------------- the switches fade
    std::printf("\nSwitches\n");
    {
        // switching the vinyl sim on mid-tone: it was running all along, so the
        // output is the off render fading into the on render over 10 ms
        const auto in = sine(220.0, 0.4, 0.5, sr);
        const Patch heavy = { {"comp", 100}, {"noise", 50}, {"wow", 60} };
        const int at = 128 * 100;
        const auto offR = run(with(heavy, { {"vinylOn", 0} }), in);
        const auto onR = run(heavy, in);
        const auto sw = run2(with(heavy, { {"vinylOn", 0} }), in, in, sr, 128, 0, [&](Engine& e, int i){
            if (i == at) e.setParam(P.index("vinylOn"), 1.0f); });
        double worst = 0.0;
        for (int i = at - 100; i < at + 1000; ++i){
            const double a = std::clamp((i - at + 1) / 480.0, 0.0, 1.0);
            const double want = offR.l[static_cast<size_t>(i)] * (1.0 - a) + onR.l[static_cast<size_t>(i)] * a;
            worst = std::max(worst, std::fabs(sw.l[static_cast<size_t>(i)] - want));
        }
        const double jump = std::fabs(sw.l[static_cast<size_t>(at)] - offR.l[static_cast<size_t>(at)]);
        check("a section switched on fades in over 10 ms, picking up where it already was",
              worst < 1e-6 && jump < 0.01, "worst " + f2s(worst, 9) + ", first step " + f2s(jump, 4));
    }

    // ------------------------------------------------- what the panel dims
    std::printf("\nWhat the panel dims\n");
    {
        const auto in = noise(0.3, 0.25, sr, 5);
        const std::vector<Patch> states = {
            {}, { {"mix", 0} }, { {"pitchOn", 1}, {"pitchBal", 0} }, { {"pitchOn", 1}, {"fltOn", 1}, {"lofiOn", 1} },
            { {"vinylOn", 0} }, { {"noise", 0}, {"wow", 0} }, { {"noise", 0}, {"wow", 0}, {"comp", 0}, {"fltOn", 1} },
        };
        int claims = 0; std::string lies;
        for (size_t si = 0; si < states.size(); ++si){
            Engine probe; probe.prepare(sr, 128); applyPatch(probe, states[si]);
            std::vector<float> vals(static_cast<size_t>(P.count()));
            for (int i = 0; i < P.count(); ++i) vals[static_cast<size_t>(i)] = probe.getParam(i);
            const auto base = run(states[si], in);
            for (const Idle& d : idleControls(vals.data())){
                const ParamInfo& info = P[d.param];
                const float cur = vals[static_cast<size_t>(d.param)];
                float other;
                if (info.kind == Kind::Float) other = (cur - info.min) > (info.max - cur) ? info.min : info.max;
                else other = cur >= info.max ? info.min : cur + 1.0f;
                Patch moved = states[si];
                moved[info.id] = other;
                const auto t = run(moved, in);
                ++claims;
                if (t.l != base.l || t.r != base.r) lies += " [state " + std::to_string(si) + ": " + info.id + "]";
            }
        }
        check("every dimmed control really does nothing (" + std::to_string(claims) + " claims checked)",
              lies.empty() && claims > 20, lies.empty() ? "" : "audible:" + lies);
    }

    // ------------------------------------------------------------ the presets
    std::printf("\nPresets\n");
    {
        const auto in = noise(0.3, 0.2, sr, 9);
        std::vector<std::vector<float>> outs;
        std::vector<std::string> names;
        for (const auto& p : presets()){
            Patch patch;
            for (const auto& kv : p.values) patch[kv.first] = kv.second;
            outs.push_back(run(patch, in).l);
            names.push_back(p.name);
        }
        std::string same;
        for (size_t a = 0; a < outs.size(); ++a)
            for (size_t b = a + 1; b < outs.size(); ++b)
                if (outs[a] == outs[b]) same += " [" + names[a] + " = " + names[b] + "]";
        check("every preset sounds different from every other (" + std::to_string(outs.size()) + ")", same.empty(), same);

        // over the audition loop with music (drums, bass and a chord, since
        // this is a box for whole loops), within 3 dB of the dry loop's
        // K-weighted loudness and off the ceiling. Init, the defaults, is held
        // to 6 dB
        const auto loop = audition::makeLoop(sr, true);
        const double dry = audition::loudness(loop);
        std::string out, all;
        for (const auto& p : presets()){
            Patch patch;
            for (const auto& kv : p.values) patch[kv.first] = kv.second;
            std::vector<float> l = loop.l, r = loop.r;
            l.resize(loop.l.size() + static_cast<size_t>(lat), 0.0f); r.resize(l.size(), 0.0f);
            const auto rr = run2(patch, l, r);
            audition::Stereo s; s.sr = sr;
            s.l.assign(rr.l.begin() + lat, rr.l.end()); s.r.assign(rr.r.begin() + lat, rr.r.end());
            const double rel = dBOf(audition::loudness(s) / dry), pk = audition::peak(s);
            const double limit = std::string(p.name) == "Init" ? 6.0 : 3.0;
            if (std::fabs(rel) > limit || pk > 0.95) out += std::string(" [") + p.name + " " + f2s(rel, 1) + " dB, peak " + f2s(pk, 2) + "]";
            all += std::string(" [") + p.name + " " + f2s(rel, 1) + "]";
        }
        check("every preset is within 3 dB of the dry loop's loudness (K-weighted), off the ceiling", out.empty(),
              out.empty() ? all : out);
        std::string noHeading;
        for (const auto& pr : presets()) if (std::string(presetCategory(pr.name)).empty()) noHeading += std::string(" [") + pr.name + "]";
        check("every preset has a heading in the menu", noHeading.empty(), noHeading);
    }

    // ---------------------------------------------- buffer size and automation
    std::printf("\nBuffer size and automation\n");
    {
        // the same audio at another buffer size must be the same sound: hosts
        // pick the size, and a bounce may use another one from playback
        audition::Stereo loop = audition::makeLoop(sr, true);
        loop.l.resize(96000); loop.r.resize(96000);
        std::string differ; int checked = 0;
        for (const auto& p : presets()){
            Patch patch;
            for (const auto& kv : p.values) patch[kv.first] = kv.second;
            const auto ref = run2(patch, loop.l, loop.r, sr, 64);
            for (int block : { 100, 1024 }){
                const auto o = run2(patch, loop.l, loop.r, sr, block);
                ++checked;
                if (o.l != ref.l || o.r != ref.r) differ += std::string(" [") + p.name + " at " + std::to_string(block) + "]";
            }
        }
        check("every preset renders bit for bit the same at 64, 100 and 1024 samples a block ("
              + std::to_string(checked) + " renders)", differ.empty(), differ);
    }
    {
        // A host moves a knob once per block. A control whose coefficients
        // jump at each block edge puts energy between the harmonics of what
        // passes through; one that glides does not. The yardstick is the same
        // sweep in 16-sample blocks
        struct Case { const char* name; Patch base; const char* id; float from, to; };
        const std::vector<Case> cases = {
            { "Cutoff", with(quiet, { {"fltOn", 1}, {"fltFreq", 300} }), "fltFreq", 300, 12000 },
            { "Resonance", with(quiet, { {"fltOn", 1}, {"fltFreq", 1500} }), "fltReso", 0, 90 },
            { "Drive", with(quiet, { {"fltOn", 1}, {"fltFreq", 20000} }), "fltDrive", 0, 100 },
            { "Pitch", with(quiet, { {"pitchOn", 1} }), "pitch", -12, 12 },
            { "Balance", with(quiet, { {"pitchOn", 1}, {"pitch", 7} }), "pitchBal", 0, 100 },
            { "Rate", with(quiet, { {"lofiOn", 1}, {"lofiBits", 16} }), "lofiRate", 4000, 48000 },
            { "Comp", quiet, "comp", 0, 100 },
            { "Wow flutter", quiet, "wow", 0, 100 },
            { "Output", quiet, "outGain", -24, 6 },
            { "Mix", with(quiet, { {"comp", 80} }), "mix", 0, 100 },
        };
        std::string worstName, all; double worst = -99.0;
        for (const auto& c : cases){
            double r[2];
            for (int pass = 0; pass < 2; ++pass){
                const int blk = pass ? 512 : 16;
                Engine e; e.prepare(sr, 512); applyPatch(e, c.base); e.seedFrom(0);
                const int n = 96000;
                std::vector<float> L(n), R(n);
                for (int i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(0.25 * std::sin(2.0 * M_PI * 440.0 * i / sr));
                const ParamInfo& pi = P[P.index(c.id)];
                for (int i = 0; i < n; i += blk){
                    const float t = static_cast<float>(i) / n;
                    e.setParam(P.index(c.id), Params::fromNorm(pi, Params::toNorm(pi, c.from) + (Params::toNorm(pi, c.to) - Params::toNorm(pi, c.from)) * t));
                    float* io[2] = { L.data() + i, R.data() + i };
                    e.process(io, 2, std::min(blk, n - i));
                }
                r[pass] = inharmonicDb(L, 440.0, sr);
            }
            all += std::string(" ") + c.name + " " + f2s(r[1] - r[0], 1) + ";";
            if (r[1] - r[0] > worst){ worst = r[1] - r[0]; worstName = c.name; }
        }
        check("no control zippers when a host moves it once a block", worst < 2.0,
              "worst " + worstName + " +" + f2s(worst, 1) + " dB over a 16-sample-block sweep;" + all);
    }

    // ------------------------------------------------------------- the whole box
    std::printf("\nWhole box\n");
    {
        const auto r = run({}, noise(0.4, 0.3, sr, 11));
        check("the default patch is finite, audible and inside full scale",
              r.bad == 0 && totalPower(r.l, 4800) > 1e-4 && r.peak <= 1.0, "peak " + f2s(r.peak, 3));
    }
    {
        Patch hard = { {"inGain", 24}, {"pitchOn", 1}, {"pitch", 24}, {"pitchFb", 100}, {"fltOn", 1}, {"fltFreq", 300},
                       {"fltReso", 100}, {"fltDrive", 100}, {"lofiOn", 1}, {"lofiRate", 1000}, {"lofiBits", 2},
                       {"comp", 100}, {"noise", 100}, {"wow", 100}, {"speed", 2}, {"outGain", 12} };
        const auto r = run(hard, noise(0.5, 0.6, sr, 13), sr, 64);
        check("everything at once stays finite and bounded", r.bad == 0 && r.peak <= 1.0, "peak " + f2s(r.peak, 3));
    }
    {
        const auto in = sine(220, 0.5, 0.2, sr);
        const std::vector<float> zero(in.size(), 0.0f);
        const auto r = run2({ {"mono", 1}, {"noise", 0} }, in, zero);
        double diff = 0.0;
        for (size_t i = 0; i < in.size(); ++i) diff = std::max(diff, std::fabs(static_cast<double>(r.l[i]) - r.r[i]));
        check("mono sums both channels to the same thing", diff < 1e-6, f2s(diff, 9));
    }
    {
        // a mono track gets exactly what the left side of a stereo one does
        // when both sides carry the same thing: every per-sample state
        // (the crackle, the scratch's decay, the shifter's heads) moves once a
        // sample, not once per channel
        const auto in = noise(0.3, 3.0, sr, 23);
        const Patch busy = { {"pitchOn", 1}, {"pitch", 5}, {"noise", 70}, {"wow", 60}, {"comp", 70}, {"speed", 2} };
        const auto st = run(busy, in);
        Engine e; e.prepare(sr, 128); applyPatch(e, busy); e.seedFrom(0);
        auto m = in;
        for (size_t i = 0; i < m.size(); i += 128){
            float* io[1] = { m.data() + i };
            e.process(io, 1, static_cast<int>(std::min<size_t>(128, m.size() - i)));
        }
        double worst = 0.0;
        for (size_t i = 0; i < m.size(); ++i) worst = std::max(worst, std::fabs(static_cast<double>(m[i]) - st.l[i]));
        check("a mono track is the left side of a stereo one, sample for sample", worst == 0.0, "worst " + f2s(worst, 9));
    }
    for (double rate : { 44100.0, 48000.0, 96000.0 }){
        const auto r = run({ {"pitchOn", 1}, {"fltOn", 1}, {"lofiOn", 1}, {"noise", 60}, {"wow", 80} },
                           noise(0.4, 0.4, rate, 17), rate, 64);
        // the latency is the turntable's 8 ms at every rate, plus the
        // oversampler's 48 samples, which are shorter at a higher rate
        check(f2s(rate / 1000.0, 1) + " kHz renders finite and bounded, the latency 8 ms plus 48 samples",
              r.bad == 0 && r.peak <= 1.0 && totalPower(r.l, 4800) > 1e-5
              && latencyOf(rate) == static_cast<int>(std::lround(0.008 * rate)) + 48,
              "peak " + f2s(r.peak, 3) + ", latency " + f2s(latencyOf(rate) / rate * 1000.0, 2) + " ms");
    }
    for (int block : { 1, 16, 512, 4096 }){
        const auto r = run({ {"pitchOn", 1}, {"wow", 80} }, noise(0.4, 0.2, sr, 19), sr, block);
        check("block size " + std::to_string(block), r.bad == 0 && r.peak <= 1.0);
    }

    std::printf("\n%d assertions passed, %zu failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.c_str());
    return failures.empty() ? 0 : 1;
}
