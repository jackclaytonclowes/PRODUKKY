// Ladder.h — the four-pole on the hardware's individual outputs, which is the
// other half of why those records sound the way they do: people patched each
// drum out separately and rolled the top off the kick.
//
// Four one-pole sections with a saturating feedback path. That core is always
// the resonant part; Shape and Poles decide what is taken out of it:
//
//   LP  N low-pass one-poles                   2 or 4 from the core's own taps,
//   HP  N high-pass one-poles, (1 - L)^N         6 and 8 by adding plain
//   BP  N/2 of each, normalised to unity         one-poles after it, which do
//       at its centre                            not resonate
//   BR  the input minus that band pass, so it is exactly zero at the centre
//
// Every one-pole sits at the same pole frequency, and the Cutoff knob marks the
// composite -3 dB point for LP and HP (the centre for BP and BR), not the pole
// itself: N identical one-poles reach -3 dB at r(N) of their corner — 0.644,
// 0.435, 0.350 and 0.301 for 2, 4, 6 and 8 — so the pole is placed to put that
// point on the mark. Getting it wrong puts the knob 3 dB out, which is what the
// first version did; crate/tests/test_core.cpp measures every combination.
//
// Resonance sings at the pole. For LP and HP that is past the mark on the
// stopband side — further as poles are added — which is the same thing this
// four-pole has always done; for BP and BR it is on the mark.
//
// The one-poles are TPT (bilinear, prewarped) rather than the simpler
// s += g * (x - s). The simple one's frequency response is a circle that does
// not pass through the point where L^2 + (1 - L)^2 = 0, so a band reject built
// from it can never reach zero: the first version of this file measured 21 dB
// of notch at 8 poles, and less at higher cutoffs. The bilinear one-pole traces
// the analogue response exactly, so the notch is exact and the -3 dB point
// lands on the mark by construction.
//
// The cutoff can be set per channel and per sample, which is what the hit
// envelope and the rhythm modulator do.
#pragma once
#include <cmath>

namespace crate {

class Ladder {
public:
    enum Shape { LP = 0, BP = 1, HP = 2, BR = 3 };

    void prepare(double sampleRate){ sr_ = sampleRate; reset(); }
    void reset(){
        for (auto& ch : s_) for (auto& v : ch) v = 0.0;
        for (auto& ch : e_) for (auto& v : ch) v = 0.0;
        y4_[0] = y4_[1] = 0.0;
    }

    // shape: LP/BP/HP/BR. poles: 2, 4, 6 or 8.
    void configure(double cutoffHz, double resonance, double drive,
                   int shape = LP, int poles = 4){
        shape_ = shape < 0 ? 0 : (shape > 3 ? 3 : shape);
        poles_ = poles <= 2 ? 2 : (poles >= 8 ? 8 : (poles >= 6 ? 6 : 4));
        static constexpr double r[] = { 0.6436, 0.4350, 0.3499, 0.3008 };
        const double rr = r[poles_ / 2 - 1];
        markToPole_ = shape_ == LP ? 1.0 / rr : (shape_ == HP ? rr : 1.0);
        // a band pass of N/2 low and N/2 high one-poles at their shared corner
        // is 2^(-N/2) there, with no phase shift; this puts its peak at unity
        bpNorm_ = std::pow(2.0, poles_ / 2);
        setCutoff(cutoffHz);
        res_ = resonance < 0.0 ? 0.0 : (resonance > 1.0 ? 1.0 : resonance);
        drive_ = drive;
    }

    // cheap enough to call per sample; ch < 0 sets both channels
    inline void setCutoff(double cutoffHz, int ch = -1){
        const double marked = cutoffHz < 20.0 ? 20.0 : cutoffHz;
        const double pole = marked * markToPole_;
        const double nyq = sr_ * 0.49;
        const double w = std::tan(M_PI * (pole > nyq ? nyq : pole) / sr_);
        const double G = w / (1.0 + w);
        if (ch < 0) g_[0] = g_[1] = G;
        else g_[ch & 1] = G;
    }

    inline double process(int ch, double x){
        ch &= 1;
        double* s = s_[ch];
        const double g = g_[ch];
        // 0.5 * x keeps the passband roughly level as resonance comes up
        double u = x * drive_ - 4.0 * res_ * (y4_[ch] - 0.5 * x);
        u = std::tanh(u);
        const double y1 = lp(s[0], g, u),  y2 = lp(s[1], g, y1);
        const double y3 = lp(s[2], g, y2), y4 = lp(s[3], g, y3);
        y4_[ch] = y4;                                    // the loop's one-sample delay
        const bool two = poles_ == 2;
        const int extra = poles_ - 4;                   // plain one-poles after the core
        double* e = e_[ch];
        switch (shape_){
        case HP: {
            double h = two ? u - 2.0 * y1 + y2
                           : u - 4.0 * y1 + 6.0 * y2 - 4.0 * y3 + y4;
            for (int k = 0; k < extra; ++k) h -= lp(e[k], g, h);
            return h;
        }
        case BP:
        case BR: {
            // L(1-L) from two taps, or L^2 (1-L)^2 from four, then one low and
            // one high one-pole per extra pair
            double b = two ? y1 - y2 : y2 - 2.0 * y3 + y4;
            for (int k = 0; k < extra; k += 2){
                b = lp(e[k], g, b);                              // low
                b -= lp(e[k + 1], g, b);                         // high
            }
            b *= bpNorm_;
            return shape_ == BP ? b : u - b;
        }
        default: {
            double l = two ? y2 : y4;
            for (int k = 0; k < extra; ++k) l = lp(e[k], g, l);
            return l;
        }
        }
    }

private:
    // one TPT low-pass step on state s: the high pass is the input minus this
    static inline double lp(double& s, double G, double x){
        const double v = (x - s) * G;
        const double y = v + s;
        s = y + v;
        return y;
    }

    double sr_ = 48000.0, g_[2] = { 0.5, 0.5 }, res_ = 0.0, drive_ = 1.0;
    double markToPole_ = 1.0 / 0.4350, bpNorm_ = 4.0;
    int shape_ = LP, poles_ = 4;
    double s_[2][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
    double e_[2][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
    double y4_[2] = { 0.0, 0.0 };
};

} // namespace crate
