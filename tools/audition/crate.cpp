// crate.cpp — every CRATE preset over a loop, written as WAV files.
//   crate-audition <outDir> [input.wav]
#include "CrateCore.h"
#include "Presets.h"
#include "common.h"
using namespace crate;

static audition::Stereo render(const Preset& preset, const audition::Stereo& in, double bpm){
    Engine e;
    e.prepare(in.sr, 128);
    const Params& P = Params::get();
    for (int i = 0; i < P.count(); ++i) e.setParam(i, P[i].def);
    for (const auto& kv : preset.values) e.setParam(P.index(kv.first), kv.second);
    e.seedFrom(0);
    // the plugin sits behind a fixed delay so its swing can pull hits early;
    // a host compensates it, so the render does too: run on past the end by
    // the most it can be, and drop that much from the front
    const int extra = static_cast<int>(in.sr * 0.25);
    const size_t n = in.l.size() + static_cast<size_t>(extra);
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    std::copy(in.l.begin(), in.l.end(), l.begin());
    std::copy(in.r.begin(), in.r.end(), r.begin());
    double ppq = 0.0;
    for (size_t i = 0; i < n; i += 128){
        const int m = static_cast<int>(std::min<size_t>(128, n - i));
        e.setTransport(true, ppq, bpm);
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
    if (argc < 2){ std::fprintf(stderr, "usage: crate-audition <outDir> [input.wav]\n"); return 2; }
    const std::string dir = argv[1];
    audition::Stereo in;
    if (argc > 2){
        std::string err;
        if (!audition::readWav(argv[2], in, err)){ std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    } else in = audition::makeLoop(48000.0, false);
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
        std::printf(",\n  {\"name\":\"%s\",\"file\":\"%s\",\"rms\":%.6f,\"peak\":%.6f}",
                    p.name, file, audition::rms(out), audition::peak(out));
    }
    std::printf("\n]\n");
    return 0;
}
