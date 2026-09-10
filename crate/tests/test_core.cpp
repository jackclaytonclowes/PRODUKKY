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
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <random>
#include <map>

using namespace crate;

static int passed = 0;
static std::vector<std::string> failures;
static void check(const std::string& name, bool ok, const std::string& detail = ""){
    if (ok){ ++passed; std::printf("  ok    %s\n", name.c_str()); }
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

int main(){
    const double sr = 48000.0;
    const Params& P = Params::get();

    std::printf("\nParameters\n");
    check("the table is complete", P.count() == 18, std::to_string(P.count()) + " parameters");
    check("the clock defaults to the rate the hardware ran at",
          std::fabs(P[P.index("clock")].def - 26040.0f) < 1.0f);
    check("twelve bits by default", std::fabs(P[P.index("bits")].def - 12.0f) < 0.01f);

    // ----------------------------------------------------------- the converter
    // measured on its own. Through the whole box the four-pole's saturation
    // colours every reading, which is fine for music and useless for a metre.
    std::printf("\nConverter\n");
    auto throughConverter = [&](double freq, double amp, double clock, double aa,
                                double bits, double compand){
        Converter c;
        c.prepare(sr);
        c.configure(clock, aa, bits, compand);
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
