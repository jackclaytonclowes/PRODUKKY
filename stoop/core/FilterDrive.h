// FilterDrive.h — the first of the original's effect buttons: a resonant
// filter, then overdrive. Cutoff, Resonance and Drive were its three knobs;
// Type (low, band or high pass) is the menu beside them.
//
// The filter is a two-pole state-variable filter in the bilinear (TPT) form,
// so it traces the analogue response exactly: at Resonance 0 it is a
// Butterworth, 3 dB down on the Cutoff mark for low and high pass, and the band
// pass peaks at unity on it. Resonance raises Q from 0.71 to 20.
//
// Drive comes after the filter, so a resonant peak is what saturates first.
// It is tanh(g x), scaled by 1 / sqrt(g tanh g): at Drive 0 g is 0.01 and the
// curve is a straight line to within -100 dB, and as g rises to 20 the small
// signals come up by sqrt(g / tanh g), +13 dB at the top, while full scale
// comes down by the same amount. Half of the drive is heard as level and half
// as saturation, so turning it up does not simply make everything louder.
//
// The whole section runs at four times the host rate (Oversampler.h), and its
// retuning glides: Cutoff and Resonance are interpolated across each host
// block and the coefficients are worked out every 16 samples.
#pragma once
#include <cmath>

namespace stoop {

class FilterDrive {
public:
    enum Type { LP = 0, BP = 1, HP = 2 };

    void prepare(double oversampledRate){ sr_ = oversampledRate; reset(); }
    void reset(){ for (auto& s : s_){ s[0] = s[1] = 0.0; } }

    static double qFor(double resonance){
        const double r = resonance < 0.0 ? 0.0 : (resonance > 1.0 ? 1.0 : resonance);
        return 0.70710678 * std::pow(20.0 / 0.70710678, r);
    }
    static double gainFor(double drive){                 // the g in tanh(g x)
        const double d = drive < 0.0 ? 0.0 : (drive > 1.0 ? 1.0 : drive);
        return 0.01 * std::pow(2000.0, d);
    }

    void setType(int type){ type_ = type < 0 ? 0 : (type > 2 ? 2 : type); }
    // cutoff in Hz, resonance 0..1
    void tune(double cutoffHz, double resonance){
        const double nyq = sr_ * 0.49;
        const double fc = cutoffHz < 10.0 ? 10.0 : (cutoffHz > nyq ? nyq : cutoffHz);
        const double g = std::tan(M_PI * fc / sr_);
        k_ = 1.0 / qFor(resonance);
        a1_ = 1.0 / (1.0 + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }
    void setDrive(double drive){
        g_ = gainFor(drive);
        norm_ = 1.0 / std::sqrt(g_ * std::tanh(g_));
    }

    inline double process(int ch, double x){
        double* s = s_[ch & 1];
        const double v3 = x - s[1];
        const double v1 = a1_ * s[0] + a2_ * v3;
        const double v2 = s[1] + a2_ * s[0] + a3_ * v3;
        s[0] = 2.0 * v1 - s[0];
        s[1] = 2.0 * v2 - s[1];
        double y;
        switch (type_){
        case HP: y = x - k_ * v1 - v2; break;
        case BP: y = k_ * v1; break;                     // unity at the centre
        default: y = v2; break;
        }
        return std::tanh(g_ * y) * norm_;
    }

private:
    double sr_ = 192000.0, k_ = 1.414, a1_ = 1.0, a2_ = 0.0, a3_ = 0.0;
    double g_ = 0.01, norm_ = 1.0;
    int type_ = LP;
    double s_[2][2] = { { 0, 0 }, { 0, 0 } };
};

} // namespace stoop
