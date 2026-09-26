// Ladder.h — the four-pole lowpass on the hardware's individual outputs, which
// is the other half of why those records sound the way they do: people patched
// each drum out separately and rolled the top off the kick.
//
// Four one-pole sections with a saturating feedback path. The cutoff knob marks
// the composite -3 dB point, not the pole frequency: four identical one-poles
// reach -3 dB at 0.435 of their own corner, so the pole is placed above the
// mark to compensate. crate/tests/test_core.cpp measures the response and fails
// if the marked frequency and the real one drift apart.
#pragma once
#include <cmath>

namespace crate {

class Ladder {
public:
    void prepare(double sampleRate){ sr_ = sampleRate; reset(); }
    void reset(){ for (auto& ch : s_) for (auto& v : ch) v = 0.0; }

    void configure(double cutoffHz, double resonance, double drive){
        setCutoff(cutoffHz);
        res_ = resonance < 0.0 ? 0.0 : (resonance > 1.0 ? 1.0 : resonance);
        drive_ = drive;
    }

    // cheap enough to call per sample, which is what the hit envelope does
    inline void setCutoff(double cutoffHz){
        const double marked = cutoffHz < 20.0 ? 20.0 : cutoffHz;
        // four identical one-poles are (1+r^2)^-2, which is -3 dB at r = 0.435,
        // so the pole sits well above the mark. Getting this wrong puts the
        // knob a full 3 dB out, which is what the first version did.
        const double pole = marked / 0.4350;
        const double nyq = sr_ * 0.49;
        g_ = 1.0 - std::exp(-2.0 * M_PI * (pole > nyq ? nyq : pole) / sr_);
    }

    inline double process(int ch, double x){
        double* s = s_[ch & 1];
        // 0.5 * x keeps the passband roughly level as resonance comes up
        double u = x * drive_ - 4.0 * res_ * (s[3] - 0.5 * x);
        u = std::tanh(u);
        s[0] += g_ * (u    - s[0]);
        s[1] += g_ * (s[0] - s[1]);
        s[2] += g_ * (s[1] - s[2]);
        s[3] += g_ * (s[2] - s[3]);
        return s[3];
    }

private:
    double sr_ = 48000.0, g_ = 0.5, res_ = 0.0, drive_ = 1.0;
    double s_[2][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
};

} // namespace crate
