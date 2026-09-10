// Dust.h — the noise floor of the record the break came off: hiss, and the
// occasional pop. It is added BEFORE the converter, because that is the order
// it happened in — someone sampled a noisy record and the sampler crushed the
// noise along with the drums.
//
// Seeded from the transport position, so two bounces of the same bar have the
// same crackle in the same places. Nothing is more annoying than a "random"
// element that changes every time you render.
#pragma once
#include <cstdint>
#include <cmath>
#include "Biquad.h"

namespace crate {

class Dust {
public:
    void prepare(double sampleRate){
        sr_ = sampleRate;
        for (int ch = 0; ch < 2; ++ch){ hp_[ch].reset(); lp_[ch].reset(); pop_[ch].reset(); }
        seedFrom(0);
    }
    void seedFrom(int64_t playheadSamples){
        rng_ = 0x243F6A8885A308D3ull ^ (static_cast<uint64_t>(playheadSamples) * 0x9E3779B97F4A7C15ull);
        if (rng_ == 0) rng_ = 1;
        for (int ch = 0; ch < 2; ++ch){ env_[ch] = 0.0; }
    }
    void configure(double amount, double toneHz){
        amt_ = amount;
        for (int ch = 0; ch < 2; ++ch){
            hp_[ch].set(Biquad::HighPass, 900.0, sr_);
            lp_[ch].set(Biquad::LowPass, toneHz, sr_);
            pop_[ch].set(Biquad::BandPass, toneHz * 0.8, sr_, 1.6);
        }
        // pops per second, and the decay of each one
        rate_ = amt_ * amt_ * 55.0 / sr_;
        decay_ = std::exp(-1.0 / (sr_ * 0.004));
    }

    inline double process(int ch){
        if (amt_ <= 0.0) return 0.0;
        const double hiss = lp_[ch].process(hp_[ch].process(white())) * amt_ * amt_ * 0.02;
        if (uniform() < rate_){
            // amplitude cubed: mostly small ticks, occasionally something louder
            const double u = uniform();
            env_[ch] = u * u * u * (uniform() < 0.5 ? -1.0 : 1.0);
        }
        const double crackle = pop_[ch].process(env_[ch]) * amt_ * 0.9;
        env_[ch] *= decay_;
        return hiss + crackle;
    }

private:
    inline double uniform(){
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17;
        return static_cast<double>(rng_ >> 11) / 9007199254740992.0;
    }
    inline double white(){ return uniform() * 2.0 - 1.0; }

    double sr_ = 48000.0, amt_ = 0.0, rate_ = 0.0, decay_ = 0.0;
    double env_[2] = { 0.0, 0.0 };
    Biquad hp_[2], lp_[2], pop_[2];
    uint64_t rng_ = 1;
};

} // namespace crate
