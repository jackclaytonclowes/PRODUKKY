// Tremolo.h — the amplitude end of the box, in the Tremolator mould: a shape
// you can morph from a sine to a hard chop, a duty control that moves where
// the beat falls, edge softening so the chop is a fade rather than a click,
// and a stereo spread that goes all the way to auto-pan at 180 degrees.
//
// Two things it does that a gain-node-and-an-LFO does not:
//
//   * It locks to the bar, not to the moment you pressed play. When a division
//     is chosen the phase comes from the host's position, so a 1/8 tremolo is
//     on the eighths of the song wherever you drop the playhead — and two
//     bounces of the same bar are identical, which they are not if the phase
//     free-runs from wherever the transport happened to start.
//   * Edge is a real slew, applied after the shape. A square wave with hard
//     edges is a click at every transition; hardware tremolos have a lamp or a
//     transformer in the way and none of them switch instantly.
//
// It sits at the very end, after the dry/wet mix, because an insert tremolo
// modulates everything on the track — including whatever of the dry signal you
// kept.
#pragma once
#include <cmath>
#include <algorithm>
#include "Sync.h"

namespace fracture {

class Tremolo {
public:
    static constexpr int maxChannels = 2;

    void prepare(double sampleRate){
        sr_ = sampleRate > 0 ? sampleRate : 48000.0;
        reset();
    }

    void reset(){
        phase_ = 0.0;
        for (int ch = 0; ch < maxChannels; ++ch) smooth_[ch] = 1.0;
        value_ = 0.0;
        first_ = true;
    }

    // Once per block. `div` is an index into the division table (0 = free), and
    // everything else is in the units the panel shows.
    void configure(bool on, double rateHz, int div, double depthPct, double shapePct,
                   double edgePct, double dutyPct, double spreadDeg, const Transport& t){
        on_ = on;
        // depth is the one control that would zipper if it stepped per block,
        // because it scales the signal directly rather than the shape
        depthTarget_ = std::clamp(depthPct, 0.0, 100.0) / 100.0;
        if (first_){ depth_ = depthTarget_; first_ = false; }
        depthK_ = 1.0 - std::exp(-1.0 / (0.02 * sr_));
        shape_ = std::clamp(shapePct, 0.0, 100.0) / 100.0;
        duty_  = std::clamp(dutyPct, 5.0, 95.0) / 100.0;
        spread_ = std::clamp(spreadDeg, 0.0, 180.0) / 360.0;
        const double edge = std::clamp(edgePct, 0.0, 100.0) / 100.0;
        // 0.35 ms at the hard end (a chop, but not a click), 16 ms at the soft
        // end (a shape whose corners have all gone)
        const double tau = 0.00035 + 0.0157 * (1.0 - edge) * (1.0 - edge);
        slew_ = 1.0 - std::exp(-1.0 / (tau * sr_));

        const double beats = beatsForDiv(div);
        if (beats > 0.0 && t.valid && t.playing){
            // grid-locked: the phase is a property of the song position, so it
            // is the same on every pass over the same bar
            const double cycles = t.ppq / beats;
            phase_ = cycles - std::floor(cycles);
            inc_ = (t.bpm > 1.0 ? t.bpm : 120.0) / 60.0 / beats / sr_;
        } else if (beats > 0.0){
            inc_ = hzForDiv(div, t.valid ? t.bpm : 120.0) / sr_;   // keep moving while stopped
        } else {
            inc_ = std::clamp(rateHz, 0.02, 24.0) / sr_;
        }
    }

    bool active() const { return on_ && (depth_ > 0.0 || depthTarget_ > 0.0); }

    // per sample, per channel; call advance() once the channels are done
    double gain(int ch){
        if (!on_) return 1.0;
        const double off = ch == 1 ? spread_ : 0.0;
        double p = phase_ + off;
        p -= std::floor(p);
        const double w = wave(p);
        double& s = smooth_[ch & 1];
        s += (w - s) * slew_;
        if (ch == 0) value_ = s * 2.0 - 1.0;              // for the matrix and the panel
        return 1.0 - depth_ + depth_ * s;
    }

    void advance(){
        depth_ += (depthTarget_ - depth_) * depthK_;
        phase_ += inc_;
        if (phase_ >= 1.0) phase_ -= std::floor(phase_);
    }

    // the shaped wave as a bipolar modulation source, so the filter can be
    // swept by the same rhythm that is chopping the level
    double value() const { return on_ ? value_ : 0.0; }

private:
    // 1 on the beat and 0 at the bottom of the dip — every shape starts loud, so
    // changing shape moves the sound and not the timing. Duty decides how much
    // of the cycle the loud half gets.
    double wave(double p) const {
        const double warped = p < duty_ ? 0.5 * p / duty_
                                        : 0.5 + 0.5 * (p - duty_) / (1.0 - duty_);
        const double sine = 0.5 + 0.5 * std::cos(2.0 * M_PI * warped);
        const double tri  = warped < 0.5 ? 1.0 - 2.0 * warped : 2.0 * warped - 1.0;
        const double sqr  = warped < 0.5 ? 1.0 : 0.0;
        // the triangle is the midpoint of the morph, not a third option
        return shape_ <= 0.5 ? sine + (tri - sine) * (shape_ * 2.0)
                             : tri + (sqr - tri) * ((shape_ - 0.5) * 2.0);
    }

    double sr_ = 48000.0;
    double phase_ = 0.0, inc_ = 0.0;
    double depth_ = 0.0, depthTarget_ = 0.0, depthK_ = 1.0;
    double shape_ = 0.0, duty_ = 0.5, spread_ = 0.0, slew_ = 1.0;
    double smooth_[maxChannels] = { 1.0, 1.0 };
    double value_ = 0.0;
    bool on_ = false, first_ = true;
};

} // namespace fracture
