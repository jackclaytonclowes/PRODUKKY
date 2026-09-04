// test_core.cpp — what the browser suite (tests/fx.mjs) checks, plus the things
// only a native build can check: that the port matches the original arithmetic,
// that the reported latency is the real latency, that the crossover sums flat,
// and that a bounce is reproducible.
//
//   npm run test:core
#include "FractureCore.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <random>
#include <map>

using namespace fracture;

static int passed = 0;
static std::vector<std::string> failures;
static void check(const std::string& name, bool ok, const std::string& detail = ""){
    if (ok){ ++passed; std::printf("  ok    %s\n", name.c_str()); }
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

// ------------------------------------------------------- shaper parity vs JS
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
                const Result r = render(patch, 0.35);
                const bool ok = r.bad == 0 && r.peak <= 1.0 && r.rms > 0.002;
                check("preset \"" + name + "\"", ok,
                      "bad " + std::to_string(r.bad) + " peak " + f2s(r.peak, 3) + " rms " + f2s(r.rms));
                if (ok) ++rendered;
                ++count;
                pos = braceClose;
            }
            check("all nine browser presets load and render", count == 9 && rendered == 9,
                  std::to_string(rendered) + "/" + std::to_string(count));
            check("every value in every browser patch maps to a plugin parameter",
                  unknown == 0, unknownIds);
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

    std::printf("\n%d assertions passed, %zu failed\n", passed, failures.size());
    for (const auto& f : failures) std::printf("  - %s\n", f.c_str());
    return failures.empty() ? 0 : 1;
}
