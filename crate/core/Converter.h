// Converter.h — the 1987 converter, which is where nearly all of the character
// comes from. Four things happen here, in this order, and each one is audible:
//
//   1. a GENTLE anti-aliasing filter. Not a brickwall — the point is that some
//      content above half the sample clock survives and folds back. A clean
//      resampler would remove exactly the thing people buy these boxes for.
//   2. a SLOW sample clock (26.04 kHz by default, the rate the hardware ran at).
//   3. 12-bit quantisation, LINEAR by default, because that is what the
//      hardware did: the SP stored 12-bit linear PCM (Rossum's reissue keeps
//      "exactly the same 12-bit linear data format"), and so did the S900. The
//      first version of this file defaulted to µ-law companding and called it
//      the character; it was a guess and it was wrong. Compand is kept as an
//      extra colour, off by default: it spends resolution on quiet signals and
//      takes it from loud ones, so hits get grainier while tails stay clean.
//   4. ZERO-ORDER HOLD reconstruction — each sample is held until the next one,
//      which is what produces the images above the clock and the familiar sinc
//      droop at the top of the band. A reconstruction filter tidies it, gently.
//
// The two machines pitched samples in different ways, and each way has its own
// sound, so there are two controls rather than one:
//
// TUNE moves the sample clock, which is how the S900 pitched: every voice had
// its own variable DAC clock. On a bus only half of that is wanted, so TUNE
// scales the clock and leaves the pitch and the timeline alone. Down twelve
// semitones is a 13 kHz clock.
//
// PITCH TRICK is how the SP pitched, and it is the "45 on 33" trick: speed the
// record up, sample it, tune it back down on the machine. The SP's output clock
// never moved; it pitched down by reading its memory with a fractional step and
// no interpolation, repeating some samples and not others (Yeh, Nolting and
// Smith, ICMC 2007). The irregular repeats are inharmonic, and they are the
// grit. That streams: at +N semitones, samples are taken at clock / 2^(N/12)
// (the sped-up record, seen from the original's timeline) and read onto the
// fixed clock by holding the latest one. Pitch and tempo come out unchanged,
// the effective sample rate drops, and the output timing snaps to a grid that
// does not divide evenly. At 0 the two clocks tick together and it is exactly
// the plain converter.
//
// MACHINE decides the filters around the quantiser, which is the real
// difference between the two families of box this is after. It does not touch
// the clock, the bits or the companding — those stay on their own knobs, and a
// preset sets them — because a switch that silently moved three other controls
// would make every knob a liar.
//
//   SP     the filters described above: a gentle anti-alias filter, so what is
//          above half the clock folds back, and a mild output stage that leaves
//          the staircase's images in. Bright, gritty, aliased.
//   S900   a six-pole Butterworth anti-alias filter below half the clock, and
//          another behind the hold. The hardware used MF6 switched-capacitor
//          filters, sixth-order Butterworth, 36 dB an octave, no resonance, and
//          the S950 shows its "bandwidth" as the rate divided by 2.5. So both
//          corners default near 0.4 of the clock. Almost nothing folds back and
//          the images are taken out, leaving twelve bits and a band limit:
//          darker, rounder, cleaner. The anti-alias knob still works, moving the
//          input corner from 0.46 of the clock down to 0.34.
#pragma once
#include <cmath>
#include "Biquad.h"

namespace crate {

enum class Machine { SP = 0, S900 = 1 };

// a six-pole Butterworth as three biquads: the Qs are 1 / (2 cos θk) for the
// pole angles of an order-6 Butterworth, so the cascade is maximally flat and
// exactly -3 dB at its corner — the response of the MF6 the S900 used
struct Butter6 {
    Biquad s[3];
    void reset(){ for (auto& b : s) b.reset(); }
    void set(double freq, double sr){
        static constexpr double q[3] = { 0.51763809, 0.70710678, 1.93185165 };
        for (int k = 0; k < 3; ++k) s[k].set(Biquad::LowPass, freq, sr, q[k]);
    }
    inline double process(double x){
        for (auto& b : s) x = b.process(x);
        return x;
    }
};

class Converter {
public:
    void prepare(double sampleRate){
        sr_ = sampleRate;
        reset();
    }
    void reset(){
        for (int ch = 0; ch < 2; ++ch){
            aa1_[ch].reset(); aa2_[ch].reset(); recon_[ch].reset();
            aaSteep_[ch].reset(); reconSteep_[ch].reset();
            held_[ch] = stored_[ch] = 0.0;
            phase_[ch] = phaseIn_[ch] = 0.0;
        }
    }

    // clockHz: the sample clock after TUNE. aa: 0 = barely filtered (maximum
    // fold-back), 1 = clean. bits: 4..16. compand: 0 = linear, 1 = full µ-law.
    // trickSemis: how far the record was sped up before sampling, 0..12.
    void configure(double clockHz, double aa, double bits, double compand,
                   Machine machine = Machine::SP, double trickSemis = 0.0){
        machine_ = machine;
        clock_ = clockHz < 2000.0 ? 2000.0 : (clockHz > sr_ ? sr_ : clockHz);
        step_ = clock_ / sr_;
        // the rate the sped-up record was sampled at, on the original's timeline.
        // The input filter ran on that record, so it moves down with it.
        const double ratio = std::pow(2.0, (trickSemis < 0.0 ? 0.0 : trickSemis) / 12.0);
        const double inClock = clock_ / ratio;
        stepIn_ = inClock / sr_;
        bits_ = bits < 2.0 ? 2.0 : bits;
        levels_ = std::pow(2.0, bits_ - 1.0);
        mu_ = compand <= 0.0 ? 0.0 : compand * 255.0;
        muNorm_ = mu_ > 0.0 ? std::log1p(mu_) : 1.0;

        // the first section always runs; the second is parked an octave and a
        // half above the clock when aa is 0, which is flat in band, and slides
        // down onto the first as aa comes up — a smooth way to go from two
        // poles of nothing much to four poles of real filtering
        const double c1 = inClock * (0.85 + (0.42 - 0.85) * aa);
        const double c2 = inClock * (3.0 + (0.42 - 3.0) * aa);
        for (int ch = 0; ch < 2; ++ch){
            aa1_[ch].set(Biquad::LowPass, c1, sr_);
            aa2_[ch].set(Biquad::LowPass, c2, sr_);
            // the output stage is fixed and mild: it takes the hardest edges off
            // the zero-order-hold staircase without removing the images
            recon_[ch].set(Biquad::LowPass, clock_ * 0.72, sr_);
            aaSteep_[ch].set(inClock * (0.46 + (0.34 - 0.46) * aa), sr_);
            reconSteep_[ch].set(clock_ * 0.40, sr_);
        }
    }

    inline double process(int ch, double x){
        const bool steep = machine_ == Machine::S900;
        const double v = steep ? aaSteep_[ch].process(x)
                               : aa2_[ch].process(aa1_[ch].process(x));
        // the converter stores a sample on its (possibly slower) input clock...
        phaseIn_[ch] += stepIn_;
        if (phaseIn_[ch] >= 1.0){
            phaseIn_[ch] -= std::floor(phaseIn_[ch]);
            stored_[ch] = quantise(v);
        }
        // ...and the output clock, which never moves, reads the latest one. With
        // no pitch trick both tick on the same sample, in this order, so this is
        // one new quantised sample per clock tick and nothing else
        phase_[ch] += step_;
        if (phase_[ch] >= 1.0){
            phase_[ch] -= std::floor(phase_[ch]);
            held_[ch] = stored_[ch];
        }
        return steep ? reconSteep_[ch].process(held_[ch]) : recon_[ch].process(held_[ch]);
    }

    double clock() const { return clock_; }
    Machine machine() const { return machine_; }

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

    double sr_ = 48000.0, clock_ = 26040.0, step_ = 0.5425, stepIn_ = 0.5425;
    double bits_ = 12.0, levels_ = 2048.0, mu_ = 0.0, muNorm_ = 1.0;
    double held_[2] = { 0.0, 0.0 }, phase_[2] = { 0.0, 0.0 };
    double stored_[2] = { 0.0, 0.0 }, phaseIn_[2] = { 0.0, 0.0 };
    Machine machine_ = Machine::SP;
    Biquad aa1_[2], aa2_[2], recon_[2];
    Butter6 aaSteep_[2], reconSteep_[2];
};

} // namespace crate
