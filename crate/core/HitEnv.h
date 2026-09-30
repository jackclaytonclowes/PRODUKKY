// HitEnv.h — the envelope on the SP's first two outputs, rebuilt for a bus.
//
// Outputs 1 and 2 of the hardware ran through an SSM2044 four-pole whose cutoff
// was opened by every note and fell back within milliseconds. That is the
// murky, thumping kick and the filtered bass line on a lot of those records:
// the attack gets through bright, and the body is dark.
//
// On the hardware the note triggered it. A bus processor has no notes, so the
// hits are found in the audio. A hit is a sudden rise: an envelope that has just
// climbed 2 dB above where it was a few milliseconds ago, and is louder than
// the recent average (a slow envelope) by 2.3 dB, which keeps the quieter hats
// between the drums from each opening the filter. Both tests are relative, so
// they follow the material rather than a fixed threshold. A 10 ms hold stops
// one attack from triggering twice, and a floor at -50 dBFS keeps hiss and dust
// from counting.
//
// The first version compared a fast envelope with the slow one (6 dB clear)
// and held for 40 ms. Two things were wrong with it. The second hit of a flam
// fired late, when the hold ran out, in the middle of the first hit's tail,
// so the filter opened on nothing; and on the audition loop it missed two of
// the eleven kicks and snares, because the slow envelope was still up from the
// hit before. The rise test finds each hit as it starts, flams included, and
// test_core checks both. A roll of equal hits 20 ms apart still reads as one
// or two: each lands on the tails of the last, and telling those apart needs
// a detector that looks at the spectrum, not the level.
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
        medAtk_ = coeff(0.0005); medRel_ = coeff(0.040); lowRise_ = coeff(0.004);
        rise_ = coeff(0.0003);
        hold_ = static_cast<int>(sampleRate * 0.010);
        reset();
    }
    void reset(){ fast_ = slow_ = med_ = low_ = 0.0; value_ = target_ = 0.0; since_ = hold_; hits = 0; }

    void setDecay(double ms){ fall_ = coeff((ms < 1.0 ? 1.0 : ms) / 1000.0); }

    // x: the detection signal, one sample (the dry input, summed to mono)
    inline double process(double x){
        const double a = std::fabs(x);
        fast_ += (a > fast_ ? fastAtk_ : fastRel_) * (a - fast_);
        slow_ += (a > slow_ ? slowAtk_ : slowRel_) * (a - slow_);
        // the rise: med_ follows the level, low_ where it was a few ms ago
        // (it drops with med_ at once and climbs after it over 4 ms)
        med_ += (a > med_ ? medAtk_ : medRel_) * (a - med_);
        low_ = med_ < low_ ? med_ : low_ + lowRise_ * (med_ - low_);
        if (since_ < hold_) ++since_;
        else if (fast_ > floor_ && med_ > low_ * rise2dB_ && fast_ > slow_ * louder_){
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

    static constexpr double rise2dB_ = 1.25; // a 2 dB jump in a few milliseconds
    static constexpr double louder_ = 1.3;   // and 2.3 dB over the recent average
    static constexpr double floor_ = 0.00316; // -50 dBFS
    double sr_ = 48000.0;
    double med_ = 0.0, low_ = 0.0, medAtk_ = 0, medRel_ = 0, lowRise_ = 0;
    double fast_ = 0.0, slow_ = 0.0, value_ = 0.0, target_ = 0.0;
    double fastAtk_ = 0, fastRel_ = 0, slowAtk_ = 0, slowRel_ = 0, rise_ = 0, fall_ = 0.001;
    int hold_ = 480, since_ = 480;
};

} // namespace crate
