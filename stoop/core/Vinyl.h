// Vinyl.h — the vinyl simulator: the record's cutting, its surface and the
// turntable that plays it.
//
// The original sampler's Vinyl Sim had three knobs: COMPRESSOR, NOISE LEVEL and
// WOW FLUTTER (its owner's manual names them CMP, noS and FLu). The SX that
// replaced it kept Noise and Wow Flutter and put a Frequency control, a
// playback tone, where the compressor had been, and moved compression out to
// an effect of its own. People who own both say the original's sounds softer
// and less tinny, and that its flutter is stronger. Comp, here, is that knob
// put back.
//
// What Roland's DSP actually did is not published, and nothing here is copied
// from it. These are our own models of the three things the knobs are named
// after, in the order they happen to a record:
//
//   1. Comp is the record's cutting. Vinyl cannot hold the dynamics of a
//      master, so records were cut compressed and limited, and that is part of
//      how a record sounds next to the master it was cut from. A peak-sensing
//      compressor, the two channels linked, with a soft knee and automatic
//      make-up, and its overshoots rounded off rather than clipped.
//   2. Noise is on the record's surface, so it goes on AFTER the cutting: the
//      crackle is not pumped by the music. Hiss, crackle, rumble, and a scratch
//      that ticks once a revolution.
//   3. Wow flutter is the turntable: a record that is not quite centred speeds
//      up and slows down once a revolution (0.56 Hz at 33, 0.75 at 45, 1.3 at
//      78), the motor and belt drift slowly, and flutter is the fast part.
//      Noise comes before it, because the crackle is on the record and moves
//      with it.
//
// Every number below is a choice, made to be measured (stoop/tests/test_core.cpp)
// rather than heard; nobody has compared this with an original yet.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "Biquad.h"
#include "PitchShifter.h"     // FracDelay

namespace stoop {

// transparent below 0.7, rounding off to exactly 1.0 above it, C1 at the knee
inline double softLimit(double x){
    const double t = 0.7, a = std::fabs(x);
    if (a <= t) return x;
    return (x < 0 ? -1.0 : 1.0) * (t + (1.0 - t) * std::tanh((a - t) / (1.0 - t)));
}

// a 64-bit xorshift, seeded from the transport so a bounce is repeatable
struct Rng {
    uint64_t s = 1;
    void seed(uint64_t v){ s = v ? v : 1; }
    inline double uniform(){                       // [0, 1)
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return static_cast<double>(s >> 11) / 9007199254740992.0;
    }
    inline double bipolar(){ return uniform() * 2.0 - 1.0; }
};

// ------------------------------------------------------------- 1. the cutting
// The knob c, 0 to 1, moves the threshold from -6 down to -36 dBFS and the
// ratio from 1:1 to 10:1 together, the way a one-knob compressor does. The knee
// is 10 dB wide. Attack and release are fixed, 3 ms and 120 ms, applied to the
// gain reduction in decibels (a "smooth, decoupled" peak detector, Giannoulis,
// Massberg and Reiss 2012), so the gain moves the same way at every level.
//
// The level it reacts to is the louder channel's peak, held for 10 ms before it
// is let fall. Without the hold the detector saw every zero crossing as a
// moment of silence and started to release, twice a cycle, and the measured
// attack came out at 9 ms instead of 3. Ten milliseconds is half a cycle of
// 50 Hz, so a bass note does not ripple the gain either.
//
// Make-up is automatic: whatever the static curve takes off a -12 dBFS peak,
// it gives back, so turning Comp up brings the quiet parts up to the loud ones
// instead of turning everything down. Measured on the audition loop, that holds
// its loudness within a decibel from Comp 0 to 100.
class VinylComp {
public:
    static constexpr double attackSeconds = 0.003, releaseSeconds = 0.120;
    static constexpr double kneeDb = 10.0, referenceDb = -12.0, holdSeconds = 0.010;

    void prepare(double sampleRate){
        att_ = std::exp(-1.0 / (attackSeconds * sampleRate));
        rel_ = std::exp(-1.0 / (releaseSeconds * sampleRate));
        fall_ = std::exp(-1.0 / (0.002 * sampleRate));
        hold_ = static_cast<int>(std::lround(holdSeconds * sampleRate));
        reset();
    }
    void reset(){ red_ = 0.0; held_ = 0.0; left_ = 0; }

    static double thresholdDb(double c){ return -6.0 - 30.0 * c; }
    static double ratio(double c){ return 1.0 + 9.0 * c; }
    // the static curve: level out for level in, in dBFS
    static double curve(double xDb, double c){
        const double t = thresholdDb(c), r = ratio(c), w = kneeDb;
        if (xDb <= t - 0.5 * w) return xDb;
        if (xDb >= t + 0.5 * w) return t + (xDb - t) / r;
        const double over = xDb - t + 0.5 * w;
        return xDb + (1.0 / r - 1.0) * over * over / (2.0 * w);
    }
    static double makeupDb(double c){ return referenceDb - curve(referenceDb, c); }

    // the louder channel's level in, the gain for both channels out (make-up
    // included). Runs at every setting, so the detector is always current
    inline double gain(double level, double c){
        if (level >= held_){ held_ = level; left_ = hold_; }
        else if (left_ > 0) --left_;
        else held_ *= fall_;
        const double xDb = 20.0 * std::log10(held_ > 1.0e-6 ? held_ : 1.0e-6);
        const double want = xDb - curve(xDb, c);            // reduction wanted, >= 0
        red_ = want > red_ ? att_ * red_ + (1.0 - att_) * want
                           : rel_ * red_ + (1.0 - rel_) * want;
        return std::pow(10.0, (makeupDb(c) - red_) / 20.0);
    }
    double reductionDb() const { return red_; }
    double levelDb() const { return 20.0 * std::log10(held_ > 1.0e-5 ? held_ : 1.0e-5); }   // what it hears

private:
    double att_ = 0.99, rel_ = 0.9998, fall_ = 0.99, red_ = 0.0, held_ = 0.0;
    int hold_ = 480, left_ = 0;
};

// --------------------------------------------------------------- 2. the surface
// At Noise n (0 to 1): hiss rises as n², crackle comes n² x 45 times a second
// with each click's size cubed from a uniform draw (mostly small ticks, now and
// then a loud one), the scratch ticks once a revolution at n³, and rumble rises
// with n. The same random numbers are drawn every sample whatever the settings,
// so moving Noise makes the same crackles louder and more of them, rather than
// a different pattern.
class Surface {
public:
    void prepare(double sampleRate){
        sr_ = sampleRate;
        for (int ch = 0; ch < 2; ++ch){
            hissHp_[ch].set(Biquad::HighPass, 700.0, sr_);
            hissLp_[ch].set(Biquad::LowPass, 7000.0, sr_);
            clickHp_[ch].set(Biquad::HighPass, 400.0, sr_);
        }
        rumble_[0].set(Biquad::LowPass, 20.0, sr_);
        rumble_[1].copyCoeffs(rumble_[0]);
        clickDecay_ = std::exp(-1.0 / (0.00025 * sr_));
        tickDecay_ = std::exp(-1.0 / (0.0007 * sr_));
        reset();
    }
    void reset(){
        for (int ch = 0; ch < 2; ++ch){
            hissHp_[ch].reset(); hissLp_[ch].reset(); clickHp_[ch].reset();
            click_[ch] = 0.0;
        }
        rumble_[0].reset(); rumble_[1].reset();
        tick_ = 0.0;
        seedFrom(0);
    }
    void seedFrom(int64_t playheadSamples){
        rng_.seed(0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(playheadSamples) * 0xBF58476D1CE4E5B9ull));
    }
    void configure(double n){
        n_ = n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
        hiss_ = n_ * n_ * 0.022;
        rate_ = n_ * n_ * 45.0 / sr_;
        crackle_ = n_ * 0.45;
        scratch_ = n_ * n_ * n_ * 0.5;
        rumbleGain_ = n_ * 0.15;
    }

    // once a sample, before the channels; tickNow when the scratch comes round
    inline void step(bool tickNow){
        const double u = rng_.uniform(), size = rng_.uniform(), sign = rng_.uniform(), pan = rng_.uniform();
        const double jitter = rng_.uniform();
        rumbleNow_ = rumble_[1].process(rumble_[0].process(rng_.bipolar())) * rumbleGain_;
        for (int ch = 0; ch < 2; ++ch){
            hissNow_[ch] = hissLp_[ch].process(hissHp_[ch].process(rng_.bipolar())) * hiss_;
            burst_[ch] = rng_.bipolar();
        }
        if (u < rate_){
            const double a = size * size * size * crackle_ * (sign < 0.5 ? -1.0 : 1.0);
            // a crackle sits on one wall of the groove more than the other
            click_[0] = a * (1.0 - 0.8 * pan);
            click_[1] = a * (0.2 + 0.8 * pan);
        }
        // once a sample, whatever the number of channels: decaying it in
        // process() for the second channel left a mono track's scratch
        // ringing for ever (the mono test in test_core found it)
        tick_ *= tickDecay_;
        if (tickNow) tick_ = scratch_ * (0.8 + 0.4 * jitter);
    }
    inline double process(int ch){
        const double c = clickHp_[ch].process((click_[ch] + tick_) * burst_[ch]);
        click_[ch] *= clickDecay_;
        return hissNow_[ch] + c + rumbleNow_;
    }

private:
    double sr_ = 48000.0, n_ = 0.0, hiss_ = 0.0, rate_ = 0.0, crackle_ = 0.0, scratch_ = 0.0, rumbleGain_ = 0.0;
    double clickDecay_ = 0.9, tickDecay_ = 0.97;
    double click_[2] = { 0.0, 0.0 }, tick_ = 0.0, burst_[2] = { 0.0, 0.0 };
    double hissNow_[2] = { 0.0, 0.0 }, rumbleNow_ = 0.0;
    Biquad hissHp_[2], hissLp_[2], clickHp_[2], rumble_[2];
    Rng rng_;
};

// ------------------------------------------------------------ 3. the turntable
// A delay line read at a moving position. The speed of the audio is one minus
// the rate the delay changes, so a delay that swings by A samples as a sine at
// f Hz moves the pitch by 2 pi f A / sampleRate either way. At Wow flutter w
// (0 to 1), three things move it:
//
//   wow      a sine once a revolution, 1% either way at full (17 cents)
//   drift    the motor wandering: a new random speed every 0.6 s, smoothed by
//            two one-poles at 0.35 Hz, about 0.2% at full
//   flutter  two sines at 6.3 and 10.1 Hz, 0.15% each at full, their sizes
//            wandering between 40% and 100% every quarter of a second
//
// The line rests at 8 ms, the turntable's whole latency, reported to the host
// whether or not the section is on so the latency never changes. At w = 0 the
// read lands exactly on that sample and the audio comes through bit for bit.
// The platter takes a moment to change speed: the revolution rate glides
// through two quarter-second one-poles. One would not do: its rate of change
// jumps the moment the target moves, and since the wow's swing depends on the
// speed, so would the pitch (the first version jumped 20 cents on a switch).
// The flutter's wandering sizes are smoothed the same way, for the same reason.
class Turntable {
public:
    static constexpr double restSeconds = 0.008;
    static constexpr double wowDepth = 0.010, driftDepth = 0.0025, flutterDepth = 0.0015;
    static constexpr double driftHz = 0.35, flutterHz1 = 6.3, flutterHz2 = 10.1;
    static constexpr double scratchAngle = 0.25;     // where on the revolution the scratch is

    void prepare(double sampleRate){
        sr_ = sampleRate;
        rest_ = std::round(restSeconds * sampleRate);
        for (auto& l : line_) l.prepare(static_cast<int>(2.0 * rest_) + 8);
        driftCoef_ = 1.0 - std::exp(-2.0 * M_PI * driftHz / sr_);
        sizeCoef_ = 1.0 - std::exp(-2.0 * M_PI * 3.0 / sr_);
        glide_ = 1.0 - std::exp(-1.0 / (0.25 * sr_));
        driftEvery_ = static_cast<int>(std::lround(0.6 * sr_));
        sizeEvery_ = static_cast<int>(std::lround(0.25 * sr_));
        reset();
    }
    void reset(){
        for (auto& l : line_) l.reset();
        rev_ = fl1_ = fl2_ = 0.0;
        d1_ = d2_ = driftTarget_ = 0.0;
        size1_ = size2_ = size1Target_ = size2Target_ = sizeMid1_ = sizeMid2_ = 0.7;
        count_ = 0;
        crossed_ = false;
        offset_ = 0.0; speed_ = 1.0;
        seedFrom(0, revHz_);
    }
    // the transport jumped: the record is where the song position says, and the
    // drift and flutter start again from a seed taken from it
    void seedFrom(int64_t playheadSamples, double revHz){
        rng_.seed(0xD1B54A32D192ED03ull ^ (static_cast<uint64_t>(playheadSamples) * 0x94D049BB133111EBull));
        const double turns = static_cast<double>(playheadSamples) * revHz / sr_;
        rev_ = turns - std::floor(turns);
    }
    void snapSpeed(double revHz){ revHz_ = revMid_ = revHz; }

    // once a sample, before the channels
    inline void step(double revHzTarget, double w){
        revMid_ += (revHzTarget - revMid_) * glide_;
        revHz_ += (revMid_ - revHz_) * glide_;
        const double was = rev_;
        rev_ += revHz_ / sr_;
        if (rev_ >= 1.0) rev_ -= 1.0;
        crossed_ = was < rev_ ? (was < scratchAngle && rev_ >= scratchAngle)
                              : (was < scratchAngle || rev_ >= scratchAngle);
        // the random parts run at every setting, so their pattern never depends on the knob
        if (count_ % driftEvery_ == 0) driftTarget_ = rng_.bipolar();
        if (count_ % sizeEvery_ == 0){ size1Target_ = 0.4 + 0.6 * rng_.uniform(); size2Target_ = 0.4 + 0.6 * rng_.uniform(); }
        ++count_;
        d1_ += (driftTarget_ - d1_) * driftCoef_;
        d2_ += (d1_ - d2_) * driftCoef_;
        sizeMid1_ += (size1Target_ - sizeMid1_) * sizeCoef_;
        sizeMid2_ += (size2Target_ - sizeMid2_) * sizeCoef_;
        size1_ += (sizeMid1_ - size1_) * sizeCoef_;
        size2_ += (sizeMid2_ - size2_) * sizeCoef_;
        fl1_ += flutterHz1 / sr_; if (fl1_ >= 1.0) fl1_ -= 1.0;
        fl2_ += flutterHz2 / sr_; if (fl2_ >= 1.0) fl2_ -= 1.0;

        const double k = sr_ / (2.0 * M_PI);              // samples of swing per unit of speed, per Hz
        const double wow = wowDepth * k / revHz_ * std::sin(2.0 * M_PI * rev_);
        const double drift = driftDepth * k / driftHz * d2_;
        const double flutter = flutterDepth * k * (size1_ / flutterHz1 * std::sin(2.0 * M_PI * fl1_)
                                                 + size2_ / flutterHz2 * std::sin(2.0 * M_PI * fl2_));
        const double prev = offset_;
        offset_ = w * (wow + drift + flutter);
        speed_ = 1.0 - (offset_ - prev);
    }
    inline double process(int ch, double x){
        FracDelay& l = line_[ch & 1];
        l.write(x);
        double d = rest_ + offset_;
        d = d < 2.0 ? 2.0 : (d > 2.0 * rest_ ? 2.0 * rest_ : d);
        return l.read(d);
    }
    inline void advance(){ for (auto& l : line_) l.advance(); }

    int latencySamples() const { return static_cast<int>(rest_); }
    bool scratchNow() const { return crossed_; }
    double revolution() const { return rev_; }             // 0..1, for the panel
    double offsetSamples() const { return offset_; }
    // the speed the audio is playing at right now, against the true speed, in
    // cents: what the panel's readout shows
    double centsNow() const { return 1200.0 * std::log2(std::max(0.5, speed_)); }

private:
    double sr_ = 48000.0, rest_ = 384.0, revHz_ = 100.0 / 3.0 / 60.0, revMid_ = 100.0 / 3.0 / 60.0;
    double rev_ = 0.0, fl1_ = 0.0, fl2_ = 0.0;
    double d1_ = 0.0, d2_ = 0.0, driftTarget_ = 0.0, driftCoef_ = 0.0;
    double size1_ = 0.7, size2_ = 0.7, size1Target_ = 0.7, size2Target_ = 0.7, sizeCoef_ = 0.0;
    double sizeMid1_ = 0.7, sizeMid2_ = 0.7;
    double glide_ = 0.0, offset_ = 0.0, speed_ = 1.0;
    int driftEvery_ = 28800, sizeEvery_ = 12000;
    long count_ = 0;
    bool crossed_ = false;
    FracDelay line_[2];
    Rng rng_;
};

} // namespace stoop
