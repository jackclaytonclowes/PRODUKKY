// Harmonics.h — what the Scope reads out: the harmonics FRACTURE added to a tone.
//
// Given the magnitude spectra of the input and the output (the same FFT frame,
// Hann-windowed), it finds whether the input is a single clear tone, where its
// fundamental is, and how loud each harmonic of it is in the output, in dB
// against the output's own fundamental. That is the number that tells Tube
// from Soft (a 2nd harmonic, or none) and shows a drawn Table doing what was
// drawn, which the bars of a spectrum leave to the eye.
//
// It only reads when there is something to read: the input has to have
// 60% of its energy in one peak's main lobe, between 90 Hz and 4 kHz. Below 90 Hz
// a 2048-point frame at 48 kHz cannot separate the harmonics (they are under
// four bins apart, inside the window's main lobe); a chord, a drum or noise is
// not one tone, and a reading of it would be a made-up number.
#pragma once
#include <cmath>
#include <algorithm>

namespace fracture {

struct HarmonicReading {
    static constexpr int maxHarmonic = 8;
    bool tonal = false;
    double f0 = 0.0;                        // Hz, refined between bins
    double level[maxHarmonic + 1] = {};     // dB against the fundamental; level[1] = 0
    bool inRange[maxHarmonic + 1] = {};     // under 95% of Nyquist
};

// mag: bins magnitudes from a 2 * bins point FFT at sampleRate
inline HarmonicReading readHarmonics(const float* inMag, const float* outMag, int bins, double sampleRate){
    HarmonicReading r;
    if (bins < 16) return r;
    const double binHz = sampleRate / (2.0 * bins);
    auto power = [](const float* m, int centre, int half, int bins){
        double p = 0.0;
        for (int k = std::max(1, centre - half); k <= std::min(bins - 1, centre + half); ++k)
            p += static_cast<double>(m[k]) * m[k];
        return p;
    };
    // the input's strongest peak in range, and whether it carries the energy
    const int lo = std::max(2, static_cast<int>(std::ceil(std::max(90.0, 4.0 * binHz) / binHz)));
    const int hi = std::min(bins - 3, static_cast<int>(4000.0 / binHz));
    int peak = lo;
    for (int k = lo; k <= hi; ++k) if (inMag[k] > inMag[peak]) peak = k;
    double total = 0.0;
    for (int k = 1; k < bins; ++k) total += static_cast<double>(inMag[k]) * inMag[k];
    // the window's main lobe is two bins either side; a tone puts most of its
    // energy there, a chord or noise does not
    if (total <= 1.0e-12 || power(inMag, peak, 2, bins) < 0.6 * total) return r;
    // parabolic interpolation on the log magnitude: within a tenth of a bin
    const double a = std::log(std::max(1.0e-12f, inMag[peak - 1]));
    const double b = std::log(std::max(1.0e-12f, inMag[peak]));
    const double c = std::log(std::max(1.0e-12f, inMag[peak + 1]));
    const double den = a - 2.0 * b + c;
    const double shift = std::fabs(den) > 1.0e-12 ? std::clamp(0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
    r.f0 = (peak + shift) * binHz;
    r.tonal = true;
    // each harmonic's power in the output, over the window's main lobe
    double p1 = 0.0;
    for (int h = 1; h <= HarmonicReading::maxHarmonic; ++h){
        const double f = h * r.f0;
        r.inRange[h] = f < 0.95 * sampleRate * 0.5;
        if (!r.inRange[h]){ r.level[h] = -120.0; continue; }
        const int centre = static_cast<int>(std::lround(f / binHz));
        const double p = power(outMag, centre, 2, bins);
        if (h == 1){ p1 = p; r.level[1] = 0.0; continue; }
        r.level[h] = p1 > 0.0 && p > 0.0 ? std::max(-120.0, 10.0 * std::log10(p / p1)) : -120.0;
    }
    if (p1 <= 0.0) r.tonal = false;
    return r;
}

} // namespace fracture
