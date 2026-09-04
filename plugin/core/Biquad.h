// Biquad.h — RBJ cookbook biquad, the same formulas Web Audio's BiquadFilterNode
// uses, so a filter here matches the browser version's magnitude response.
//
// One deliberate difference: Web Audio interprets `Q` for lowpass and highpass
// in DECIBELS (a spec quirk — "not a traditional Q, but a resonance value in
// decibels"), which is why the browser build's resonance knob behaves oddly at
// the top. Here Q is a real Q for every type. Cutoff and resonance therefore
// feel slightly different from the same numbers in the browser version, which
// is the correct trade: the filter is now standard.
#pragma once
#include <cmath>

namespace fracture {

struct Biquad {
    enum Type { LowPass, HighPass, BandPass, Notch, Peaking, LowShelf, HighShelf, AllPass };

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   // normalised by a0
    double z1 = 0, z2 = 0;                            // transposed direct form II

    void reset(){ z1 = z2 = 0; }

    void set(Type type, double freq, double sampleRate, double q = 0.70710678, double dbGain = 0){
        const double nyq = sampleRate * 0.5;
        freq = freq < 10.0 ? 10.0 : (freq > nyq * 0.999 ? nyq * 0.999 : freq);
        q = q < 0.05 ? 0.05 : q;
        const double w0 = 2.0 * M_PI * freq / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow(10.0, dbGain / 40.0);
        double B0, B1, B2, A0, A1, A2;
        switch (type){
        case LowPass:
            B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2;
            A0 = 1 + alpha;    A1 = -2 * cw; A2 = 1 - alpha; break;
        case HighPass:
            B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2;
            A0 = 1 + alpha;    A1 = -2 * cw;   A2 = 1 - alpha; break;
        case BandPass:                                  // constant 0 dB peak gain
            B0 = alpha;     B1 = 0;        B2 = -alpha;
            A0 = 1 + alpha; A1 = -2 * cw;  A2 = 1 - alpha; break;
        case Notch:
            B0 = 1;         B1 = -2 * cw;  B2 = 1;
            A0 = 1 + alpha; A1 = -2 * cw;  A2 = 1 - alpha; break;
        case Peaking:
            B0 = 1 + alpha * A; B1 = -2 * cw;  B2 = 1 - alpha * A;
            A0 = 1 + alpha / A; A1 = -2 * cw;  A2 = 1 - alpha / A; break;
        case LowShelf: {
            const double s = 2.0 * std::sqrt(A) * alpha;
            B0 =      A * ((A + 1) - (A - 1) * cw + s);
            B1 =  2 * A * ((A - 1) - (A + 1) * cw);
            B2 =      A * ((A + 1) - (A - 1) * cw - s);
            A0 =           (A + 1) + (A - 1) * cw + s;
            A1 =      -2 * ((A - 1) + (A + 1) * cw);
            A2 =           (A + 1) + (A - 1) * cw - s; break;
        }
        case HighShelf: {
            const double s = 2.0 * std::sqrt(A) * alpha;
            B0 =      A * ((A + 1) + (A - 1) * cw + s);
            B1 = -2 * A * ((A - 1) + (A + 1) * cw);
            B2 =      A * ((A + 1) + (A - 1) * cw - s);
            A0 =           (A + 1) - (A - 1) * cw + s;
            A1 =       2 * ((A - 1) - (A + 1) * cw);
            A2 =           (A + 1) - (A - 1) * cw - s; break;
        }
        case AllPass:
        default:
            B0 = 1 - alpha; B1 = -2 * cw; B2 = 1 + alpha;
            A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    // copy coefficients from another section, keeping this one's state — used
    // for the second half of a cascaded pair
    void copyCoeffs(const Biquad& o){ b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    inline double process(double x){
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

} // namespace fracture
