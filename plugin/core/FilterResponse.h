// FilterResponse.h — the post filter's frequency response, for the panel to draw.
//
// Computed from the same coefficients the audio uses, never from a separate
// sketch of what the filter "should" look like: a clean cascade is its biquads
// exactly, and the ladder is its small-signal response (AnalogFilter.h). The
// filter's own mix is included, because a notch at 50% is only half a notch.
// plugin/tests/test_core.cpp renders sine tones through the real engine and
// requires the measured level to match this curve.
#pragma once
#include <complex>
#include <algorithm>
#include "Biquad.h"
#include "AnalogFilter.h"

namespace fracture {

struct FilterState {
    int type = 0;            // 0 off, 1 lp, 2 hp, 3 bp, 4 notch, 5 peak
    int circuit = 0;         // Ladder::Clean / Analogue / Vintage
    int poles = 4;           // 2, 4, 6 or 8
    double freq = 1000.0;    // the cutoff now, modulation and rhythm included
    double q = 0.7, drive = 1.0, mix = 1.0;
};

inline const char* filterTypeLabel(int type){
    static const char* n[] = { "Off", "Low pass", "High pass", "Band pass", "Notch", "Peak" };
    return n[std::clamp(type, 0, 5)];
}

// the response at freqHz as a complex gain (1 = unchanged)
inline std::complex<double> filterResponse(const FilterState& st, double freqHz, double sr){
    if (st.type <= 0) return 1.0;
    std::complex<double> h;
    if (st.circuit == Ladder::Clean){
        static const Biquad::Type map[] = { Biquad::AllPass, Biquad::LowPass, Biquad::HighPass,
                                            Biquad::BandPass, Biquad::Notch, Biquad::Peaking };
        Biquad b;
        b.set(map[std::clamp(st.type, 1, 5)], st.freq, sr, st.q, 9.0);
        // one section per 12 dB, but Peak stops at two, as in the engine
        const int sections = st.type == 5 ? std::min(2, st.poles / 2) : st.poles / 2;
        const double w = 2.0 * M_PI * std::clamp(freqHz, 1.0, sr * 0.499) / sr;
        h = std::pow(b.at(w), sections);
    } else {
        Ladder l;
        l.prepare(sr, 1u);
        l.set(st.type, st.poles, st.circuit, st.freq, st.q, st.drive, 0.0);
        h = l.response(freqHz);
    }
    return 1.0 + (h - 1.0) * std::clamp(st.mix, 0.0, 1.0);
}

} // namespace fracture
