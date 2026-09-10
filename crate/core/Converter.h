// Converter.h — the 1987 converter, which is where nearly all of the character
// comes from. Four things happen here, in this order, and each one is audible:
//
//   1. a GENTLE anti-aliasing filter. Not a brickwall — the point is that some
//      content above half the sample clock survives and folds back. A clean
//      resampler would remove exactly the thing people buy these boxes for.
//   2. a SLOW sample clock (26.04 kHz by default, the rate the hardware ran at).
//   3. COMPANDED 12-bit quantisation: the signal is µ-law encoded, quantised to
//      12 bits and expanded again. That trade is the character — companding
//      spends resolution on quiet signals and takes it from loud ones, so hits
//      get grainier as they get louder while tails stay clean. A linear 12-bit
//      converter does the opposite. Compand at 0 gives you the linear one.
//   4. ZERO-ORDER HOLD reconstruction — each sample is held until the next one,
//      which is what produces the images above the clock and the familiar sinc
//      droop at the top of the band. A reconstruction filter tidies it, gently.
//
// TUNE moves the sample clock without moving the pitch. On the hardware,
// pitching a sample down slowed the clock and slowed the audio together; on a
// bus you only want the first half of that, so TUNE scales the clock and leaves
// the timeline alone. Down twelve semitones is a 13 kHz clock, which is the
// classic "pitched down for grit" sound with the tempo left where you put it.
#pragma once
#include <cmath>
#include "Biquad.h"

namespace crate {

class Converter {
public:
    void prepare(double sampleRate){
        sr_ = sampleRate;
        reset();
    }
    void reset(){
        for (int ch = 0; ch < 2; ++ch){
            aa1_[ch].reset(); aa2_[ch].reset(); recon_[ch].reset();
            held_[ch] = 0.0;
            phase_[ch] = 0.0;
        }
    }

    // clockHz: the sample clock after TUNE. aa: 0 = barely filtered (maximum
    // fold-back), 1 = clean. bits: 4..16. compand: 0 = linear, 1 = full µ-law.
    void configure(double clockHz, double aa, double bits, double compand){
        clock_ = clockHz < 2000.0 ? 2000.0 : (clockHz > sr_ ? sr_ : clockHz);
        step_ = clock_ / sr_;
        bits_ = bits < 2.0 ? 2.0 : bits;
        levels_ = std::pow(2.0, bits_ - 1.0);
        mu_ = compand <= 0.0 ? 0.0 : compand * 255.0;
        muNorm_ = mu_ > 0.0 ? std::log1p(mu_) : 1.0;

        // the first section always runs; the second is parked an octave and a
        // half above the clock when aa is 0, which is flat in band, and slides
        // down onto the first as aa comes up — a smooth way to go from two
        // poles of nothing much to four poles of real filtering
        const double c1 = clock_ * (0.85 + (0.42 - 0.85) * aa);
        const double c2 = clock_ * (3.0 + (0.42 - 3.0) * aa);
        for (int ch = 0; ch < 2; ++ch){
            aa1_[ch].set(Biquad::LowPass, c1, sr_);
            aa2_[ch].set(Biquad::LowPass, c2, sr_);
            // the output stage is fixed and mild: it takes the hardest edges off
            // the zero-order-hold staircase without removing the images
            recon_[ch].set(Biquad::LowPass, clock_ * 0.72, sr_);
        }
    }

    inline double process(int ch, double x){
        double v = aa2_[ch].process(aa1_[ch].process(x));
        phase_[ch] += step_;
        if (phase_[ch] >= 1.0){
            phase_[ch] -= std::floor(phase_[ch]);        // one new sample per clock tick
            held_[ch] = quantise(v);
        }
        return recon_[ch].process(held_[ch]);
    }

    double clock() const { return clock_; }

private:
    inline double compress(double x) const {
        if (mu_ <= 0.0) return x;
        const double s = x < 0 ? -1.0 : 1.0;
        return s * std::log1p(mu_ * std::fabs(x)) / muNorm_;
    }
    inline double expand(double y) const {
        if (mu_ <= 0.0) return y;
        const double s = y < 0 ? -1.0 : 1.0;
        return s * (std::pow(1.0 + mu_, std::fabs(y)) - 1.0) / mu_;
    }
    inline double quantise(double x) const {
        // the converter clips, and hitting it is a technique rather than a fault
        if (x > 1.0) x = 1.0; else if (x < -1.0) x = -1.0;
        const double c = compress(x);
        return expand(std::round(c * levels_) / levels_);
    }

    double sr_ = 48000.0, clock_ = 26040.0, step_ = 0.5425;
    double bits_ = 12.0, levels_ = 2048.0, mu_ = 0.0, muNorm_ = 1.0;
    double held_[2] = { 0.0, 0.0 }, phase_[2] = { 0.0, 0.0 };
    Biquad aa1_[2], aa2_[2], recon_[2];
};

} // namespace crate
