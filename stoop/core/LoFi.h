// LoFi.h — a slower sample clock and fewer bits, with nothing in between to
// tidy it up.
//
// Rate is a sample-and-hold clock: each tick takes the input, and it is held
// until the next. There is no filter before it, so whatever sits above half
// the Rate folds back into the band as inharmonic grit, and no filter after it,
// so the staircase's images stay in. That is what a lo-fi effect is for; CRATE
// is the place for a careful model of a particular converter. Bits rounds each
// held sample to 2^(bits - 1) steps either side of zero, and clips at full
// scale.
//
// The clock is shared by both channels, so the stereo image does not smear.
#pragma once
#include <cmath>

namespace stoop {

class LoFi {
public:
    void prepare(double sampleRate){ sr_ = sampleRate; reset(); }
    void reset(){ phase_ = 1.0; ticked_ = true; held_[0] = held_[1] = 0.0; }

    // once a sample, before the channels: does the clock tick on this one?
    inline void step(double rateHz){
        const double s = rateHz >= sr_ ? 1.0 : rateHz / sr_;
        phase_ += s;
        ticked_ = phase_ >= 1.0;
        if (ticked_) phase_ -= std::floor(phase_);
    }
    inline double process(int ch, double x, double bits){
        if (ticked_){
            const double levels = std::exp2(bits - 1.0);
            const double c = x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x);
            held_[ch & 1] = std::round(c * levels) / levels;
        }
        return held_[ch & 1];
    }

private:
    double sr_ = 48000.0, phase_ = 1.0;
    bool ticked_ = true;
    double held_[2] = { 0.0, 0.0 };
};

} // namespace stoop
