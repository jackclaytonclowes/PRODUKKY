// AnalogFilter.h — a nonlinear four-pole ladder, for when the clean biquad is
// too polite. This is the FilterFreak end of the box: a filter you drive into,
// whose resonance squelches and eventually sings, and whose cutoff never sits
// perfectly still.
//
// Three things make it sound like a circuit rather than a transfer function:
//
//   * Zero-delay feedback. The four one-poles are solved together each sample
//     (Zavalishin's TPT form), so the resonance stays in tune as the cutoff
//     rises instead of flattening out the way a naive four-tap loop does, and
//     it is stable right up to self-oscillation.
//   * Saturation in the loop, not after it. The input stage and the feedback
//     path each saturate, which is what makes resonance compress against a
//     loud signal — the "squelch" — and what limits self-oscillation to a
//     sensible level instead of a divergence.
//   * Drift. Cutoff and resonance wander slowly and independently per channel.
//     It is a few per cent and you would not name it in a blind test, but a
//     pair of channels that are not bit-identical is most of what "analogue"
//     means to the ear.
//
// Every response comes from the same ladder by mixing the four taps
// (Oberheim-style mode mixing), so switching type or slope never re-tunes the
// resonance or jumps in level. 36 and 48 dB add two or four plain TPT
// one-poles after the ladder, at the same corner, which steepen the response
// without resonating: low pass adds low passes, high pass adds high passes,
// band pass a low and a high per pair, and from 36 dB up notch and peak are
// built on that steeper band pass (input minus it, and input plus twice it).
// At 12 and 24 dB everything is exactly what it was.
//
// The clean biquad path is still there and is still the default: this filter
// only runs when the Circuit control asks for it, so every patch made before
// it existed sounds exactly as it did.
#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace fracture {

// tanh to about 1e-3 over the range that matters, saturating monotonically to
// exactly +/-1 at +/-3. Cheap enough to afford twice per sample per channel.
inline double satSoft(double x){
    if (x <= -3.0) return -1.0;
    if (x >=  3.0) return  1.0;
    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}
// The same curve pushed off centre, so it is asymmetric about zero and
// generates even harmonics as well as odd. The offset is removed afterwards:
// what comes out is the harmonic content, not a DC step.
inline double satAsym(double x){
    constexpr double off = 0.18;
    static const double base = satSoft(off);
    return satSoft(x + off) - base;
}

class Ladder {
public:
    enum Circuit { Clean = 0, Analogue = 1, Vintage = 2 };
    // filter types, matching fltType: 0 off, 1 lp, 2 hp, 3 bp, 4 notch, 5 peak

    void prepare(double sampleRate, uint32_t seed){
        sr_ = sampleRate > 0 ? sampleRate : 48000.0;
        rng_ = seed | 1u;
        reset();
    }

    void reset(){
        z_[0] = z_[1] = z_[2] = z_[3] = 0.0;
        e_[0] = e_[1] = e_[2] = e_[3] = 0.0;
        hf_ = 0.0;
        drift_ = driftTarget_ = 0.0;
        driftCount_ = 0;
        coeffCount_ = 0;
        freqNow_ = -1.0;
    }

    // Called once per block. freq/reso are already modulated; drive is a gain
    // into the input stage and `driftAmt` is 0..1.
    void set(int type, int poles, int circuit, double freq, double reso,
             double drive, double driftAmt){
        type_ = std::clamp(type, 1, 5);
        poles_ = poles >= 8 ? 8 : (poles >= 6 ? 6 : (poles >= 4 ? 4 : 2));
        circuit_ = circuit == Vintage ? Vintage : Analogue;
        freq_ = std::clamp(freq, 20.0, sr_ * 0.45);
        k_ = resoToFeedback(reso);
        drive_ = std::clamp(drive, 0.1, 24.0);
        driftAmt_ = std::clamp(driftAmt, 0.0, 1.0);
        // Drive should be audible as drive, not as level, so most of it is
        // taken back out again — but not all of it, because a filter you push
        // getting a little louder is the behaviour people reach for.
        trim_ = std::pow(drive_, -0.8);
        // A ladder loses its bottom end as resonance rises; put most of it
        // back, and only where it went missing (the low-pass taps).
        bassComp_ = (type_ == 1) ? 1.0 + 0.42 * k_ : 1.0;
        // The mark is the pole corner, which is where the resonance sings —
        // the same place it is written on a ladder's front panel, and the only
        // calibration that lets you play a resonant sweep in tune. It is not
        // the -3 dB point: four cascaded one-poles are 3 dB down at 0.435 of
        // their corner, so at a given number the analogue circuit is darker
        // than the clean biquad. That is the character of a four-pole, and
        // moving the corner to hide it would put the whistle an octave above
        // the number on the knob.
        poleFreq_ = freq_;
        setMix();
        coeffCount_ = 0;                                  // recompute on the next sample
    }

    // move the corner without touching anything else: the rhythm modulator
    // calls this every few samples, and the coefficients follow on the next one
    void retune(double freq){
        freq_ = poleFreq_ = std::clamp(freq, 20.0, sr_ * 0.45);
        coeffCount_ = 0;
    }

    double process(double x){
        if (--coeffCount_ <= 0){ updateDrift(); updateCoeffs(); coeffCount_ = coeffInterval; }

        const double inGain = drive_ * bassComp_;
        // zero-delay solve: what the fourth tap would be with the current
        // states, and the input that makes the loop consistent with it
        const double s1 = z_[0], s2 = z_[1], s3 = z_[2], s4 = z_[3];
        const double b1 = s1 * omG_, b2 = s2 * omG_, b3 = s3 * omG_, b4 = s4 * omG_;
        const double sFb = G_ * G_ * G_ * b1 + G_ * G_ * b2 + G_ * b3 + b4;
        double u = (x * inGain - kNow_ * satSoft(sFb)) / (1.0 + kNow_ * G4_);
        u = circuit_ == Vintage ? satAsym(u) : satSoft(u);

        // each stage is a TPT integrator: y = G*in + (1-G)*s, then s = 2y - s
        const double y1 = G_ * u  + b1; z_[0] = 2.0 * y1 - s1;
        const double y2 = G_ * y1 + b2; z_[1] = 2.0 * y2 - s2;
        const double y3 = G_ * y2 + b3; z_[2] = 2.0 * y3 - s3;
        const double y4 = G_ * y3 + b4; z_[3] = 2.0 * y4 - s4;

        double out = a0_ * u + a1_ * y1 + a2_ * y2 + a3_ * y3 + a4_ * y4;
        if (poles_ > 4) out = steepen(out, u, y2, y3, y4);
        if (circuit_ == Vintage){                          // the top end a real one never had
            hf_ += (out - hf_) * hfK_;
            out = hf_;
        }
        return out * trim_;
    }

private:
    // one TPT low-pass step on state s
    inline double lp(double& s, double x) const {
        const double v = (x - s) * G_;
        const double y = v + s;
        s = y + v;
        return y;
    }
    // the extra one-poles for 36 and 48 dB
    double steepen(double out, double u, double y2, double y3, double y4){
        const int extra = poles_ - 4;
        switch (type_){
        case 1: for (int k = 0; k < extra; ++k) out = lp(e_[k], out); return out;
        case 2: for (int k = 0; k < extra; ++k) out -= lp(e_[k], out); return out;
        default: {
            // band pass of N poles, unity at the corner: the four-pole one from
            // the taps, then a low and a high one-pole per extra pair, each pair
            // doubled because a low and a high meet at -6 dB
            double b = 4.0 * (y2 - 2.0 * y3 + y4);
            for (int k = 0; k < extra; k += 2){
                b = lp(e_[k], b);
                b -= lp(e_[k + 1], b);
                b *= 2.0;
            }
            if (type_ == 3) return b;
            if (type_ == 4) return u - b;
            return u + 2.0 * b;
        }
        }
    }

    static constexpr int coeffInterval = 32;               // drift is slow; tan() is not

    // 0.3..18 of "Q" mapped onto ladder feedback. 4.0 is where a ladder starts
    // to sing; the top of the knob is past it on purpose, so the filter can be
    // played as an oscillator.
    static double resoToFeedback(double q){
        const double t = std::log(std::clamp(q, 0.3, 18.0) / 0.3) / std::log(18.0 / 0.3);
        return 4.35 * std::pow(t, 1.25);
    }

    void setMix(){
        // taps: u, y1..y4. Everything is a mix of the same ladder, so changing
        // type does not re-tune the resonance.
        const bool four = poles_ >= 4;              // 36 and 48 build on the four-pole taps
        switch (type_){
        case 1:                                            // low pass: one tap
            if (four) set5(0, 0, 0, 0, 1); else set5(0, 0, 1, 0, 0);
            break;
        case 2:                                            // high pass: (1 - z)^n
            if (four) set5(1, -4, 6, -4, 1); else set5(1, -2, 1, 0, 0);
            break;
        case 3:                                            // band pass
            if (four) set5(0, 0, 4, -8, 4); else set5(0, 2, -2, 0, 0);
            break;
        case 4: set5(1, -2, 2, 0, 0); break;               // notch: input minus the band
        default: set5(1, 4, -4, 0, 0); break;              // peak: input plus the band
        }
    }
    void set5(double a0, double a1, double a2, double a3, double a4){
        a0_ = a0; a1_ = a1; a2_ = a2; a3_ = a3; a4_ = a4;
    }

    void updateDrift(){
        if (driftCount_-- <= 0){
            rng_ = rng_ * 1664525u + 1013904223u;
            driftTarget_ = static_cast<double>(rng_ >> 8) / 8388608.0 - 1.0;   // -1..1
            driftCount_ = 24 + static_cast<int>((rng_ >> 5) & 31);             // ~25-55 blocks
        }
        // one pole towards the target, so the wander is smooth rather than stepped
        drift_ += (driftTarget_ - drift_) * 0.06;
    }

    void updateCoeffs(){
        const double extra = circuit_ == Vintage ? 0.075 : 0.035;    // how far it wanders
        const double f = std::clamp(poleFreq_ * (1.0 + driftAmt_ * extra * drift_),
                                    16.0, sr_ * 0.46);
        if (std::fabs(f - freqNow_) > 0.001){
            freqNow_ = f;
            const double g = std::tan(M_PI * f / sr_);
            G_ = g / (1.0 + g);
            omG_ = 1.0 - G_;
            G4_ = G_ * G_ * G_ * G_;
        }
        kNow_ = k_ * (1.0 + driftAmt_ * 0.09 * drift_);
        if (kNow_ < 0.0) kNow_ = 0.0;
        hfK_ = 1.0 - std::exp(-2.0 * M_PI * 13500.0 / sr_);
    }

    double sr_ = 48000.0;
    double freq_ = 1000.0, poleFreq_ = 1000.0, freqNow_ = -1.0, k_ = 0.0, kNow_ = 0.0;
    double drive_ = 1.0, trim_ = 1.0, bassComp_ = 1.0, driftAmt_ = 0.0;
    double G_ = 0.1, omG_ = 0.9, G4_ = 0.0001;
    double a0_ = 0, a1_ = 0, a2_ = 0, a3_ = 0, a4_ = 1;
    double z_[4] = { 0, 0, 0, 0 };
    double e_[4] = { 0, 0, 0, 0 };                        // the 36/48 dB extras
    double hf_ = 0.0, hfK_ = 0.5;
    double drift_ = 0.0, driftTarget_ = 0.0;
    int driftCount_ = 0, coeffCount_ = 0;
    int type_ = 1, poles_ = 4, circuit_ = Analogue;
    uint32_t rng_ = 22222u;
};

} // namespace fracture
