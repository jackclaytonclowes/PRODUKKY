// HitEnv.h — the envelope on the SP's first two outputs, rebuilt for a bus.
//
// Outputs 1 and 2 of the hardware ran through an SSM2044 four-pole whose cutoff
// was opened by every note and fell back within milliseconds. That is the
// murky, thumping kick and the filtered bass line on a lot of those records:
// the attack gets through bright, and the body is dark.
//
// On the hardware the note triggered it. A bus processor has no notes, so the
// hits are found in the audio: a fast envelope is compared against a slow one,
// and when the fast one jumps 6 dB clear of it, that is a hit. The comparison is
// relative, so it follows the material rather than a fixed threshold, and a
// hold stops one hit's own ringing from triggering again. A floor at -50 dBFS
// keeps hiss and dust from counting.
//
// The value it produces is 0..1: it jumps to 1 on a hit (over a fraction of a
// millisecond, so the cutoff move is not itself a click) and falls away
// exponentially with the Decay time as its time constant. The caller turns it
// into octaves above the Cutoff knob, so the knob stays the resting point — the
// place the filter closes back down to — which is how the hardware behaved.
//
// Detection is causal: it hears the hit as it arrives, so the first fraction of
// a millisecond of an attack passes through the closed filter. The hardware
// knew about the note before the sound; this cannot, short of adding latency.
#pragma once
#include <cmath>

namespace crate {

class HitEnv {
public:
    void prepare(double sampleRate){
        sr_ = sampleRate;
        fastAtk_ = coeff(0.0005); fastRel_ = coeff(0.010);
        slowAtk_ = coeff(0.030);  slowRel_ = coeff(0.250);
        rise_ = coeff(0.0003);
        hold_ = static_cast<int>(sampleRate * 0.040);
        reset();
    }
    void reset(){ fast_ = slow_ = 0.0; value_ = target_ = 0.0; since_ = hold_; hits = 0; }

    void setDecay(double ms){ fall_ = coeff((ms < 1.0 ? 1.0 : ms) / 1000.0); }

    // x: the detection signal, one sample (the dry input, summed to mono)
    inline double process(double x){
        const double a = std::fabs(x);
        fast_ += (a > fast_ ? fastAtk_ : fastRel_) * (a - fast_);
        slow_ += (a > slow_ ? slowAtk_ : slowRel_) * (a - slow_);
        if (since_ < hold_) ++since_;
        else if (fast_ > floor_ && fast_ > slow_ * ratio_){
            since_ = 0; target_ = 1.0; ++hits;
        }
        if (target_ > 0.0){
            value_ += rise_ * (target_ - value_);
            if (value_ > 0.995){ value_ = 1.0; target_ = 0.0; }
        } else {
            value_ *= 1.0 - fall_;
        }
        return value_;
    }

    double value() const { return value_; }
    long hits = 0;                           // counted for the tests and nothing else

private:
    double coeff(double seconds) const { return 1.0 - std::exp(-1.0 / (seconds * sr_)); }

    static constexpr double ratio_ = 2.0;    // 6 dB clear of the slow envelope
    static constexpr double floor_ = 0.00316; // -50 dBFS
    double sr_ = 48000.0;
    double fast_ = 0.0, slow_ = 0.0, value_ = 0.0, target_ = 0.0;
    double fastAtk_ = 0, fastRel_ = 0, slowAtk_ = 0, slowRel_ = 0, rise_ = 0, fall_ = 0.001;
    int hold_ = 1920, since_ = 1920;
};

} // namespace crate
