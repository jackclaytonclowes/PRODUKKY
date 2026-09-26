// fracture.cpp — every FRACTURE preset over a loop, written as WAV files.
//   fracture-audition <outDir> [input.wav]
#include "FractureCore.h"
#include "Presets.h"
#include "common.h"
using namespace fracture;

static audition::Stereo render(const Preset& preset, const audition::Stereo& in, double bpm){
    Engine e;
    e.prepare(in.sr, 128);
    const Params& P = Params::get();
    for (int i = 0; i < P.count(); ++i) e.setParam(i, P[i].def);
    // the same import the Paste patch button uses: the browser's flat JSON
    for (const auto& kv : audition::parseFlatJson(preset.json)){
        const int idx = P.index(kv.first);
        if (idx < 0) continue;
        float v = 0.0f;
        const auto& j = kv.second;
        if (patchValueToParam(P[idx], j.text, j.isNumber, j.number, j.isBool, j.boolean, v)) e.setParam(idx, v);
    }
    e.seedFrom(0);
    const int extra = static_cast<int>(in.sr * 0.05);             // more than any oversampling latency
    const size_t n = in.l.size() + static_cast<size_t>(extra);
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    std::copy(in.l.begin(), in.l.end(), l.begin());
    std::copy(in.r.begin(), in.r.end(), r.begin());
    double ppq = 0.0;
    for (size_t i = 0; i < n; i += 128){
        const int m = static_cast<int>(std::min<size_t>(128, n - i));
        Transport t; t.bpm = bpm; t.ppq = ppq; t.playing = true; t.valid = true;
        e.setTransport(t);
        float* io[2] = { l.data() + i, r.data() + i };
        e.process(io, 2, m);
        ppq += m / in.sr * bpm / 60.0;
    }
    const size_t lat = static_cast<size_t>(e.latencySamples());
    audition::Stereo out; out.sr = in.sr;
    out.l.assign(l.begin() + static_cast<long>(lat), l.begin() + static_cast<long>(lat + in.l.size()));
    out.r.assign(r.begin() + static_cast<long>(lat), r.begin() + static_cast<long>(lat + in.l.size()));
    return out;
}

int main(int argc, char** argv){
    if (argc < 2){ std::fprintf(stderr, "usage: fracture-audition <outDir> [input.wav]\n"); return 2; }
    const std::string dir = argv[1];
    audition::Stereo in;
    if (argc > 2){
        std::string err;
        if (!audition::readWav(argv[2], in, err)){ std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    } else in = audition::makeLoop(48000.0, true);
    const double bpm = 90.0;
    audition::writeWav16(dir + "/dry.wav", in);
    std::printf("[\n  {\"name\":\"Dry\",\"file\":\"dry.wav\",\"rms\":%.6f,\"peak\":%.6f}",
                audition::rms(in), audition::peak(in));
    int k = 0;
    for (const auto& p : presets()){
        const auto out = render(p, in, bpm);
        char file[160];
        std::snprintf(file, sizeof file, "%02d-%s.wav", k++, audition::slug(p.name).c_str());
        audition::writeWav16(dir + "/" + file, out);
        std::string name = p.name;                                   // names are plain text, but quote-safe
        for (size_t q = 0; (q = name.find('"', q)) != std::string::npos; q += 2) name.insert(q, "\\");
        std::printf(",\n  {\"name\":\"%s\",\"file\":\"%s\",\"rms\":%.6f,\"peak\":%.6f}",
                    name.c_str(), file, audition::rms(out), audition::peak(out));
    }
    std::printf("\n]\n");
    return 0;
}
