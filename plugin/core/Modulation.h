// Modulation.h — two LFOs, an envelope follower and the six-slot matrix.
//
// In the browser these ran at animation-frame rate (~60 Hz) because that is
// where the UI loop lives. Here they run once per audio block, so modulation is
// finally as fast as the audio, and the random shapes are seeded from the
// playhead so two bounces of the same bar come out identical.
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "ParamTable.h"

namespace fracture {

class ModEngine {
public:
    void prepare(double sampleRate){ sr_ = sampleRate; reset(); }

    void reset(){
        ph_[0] = ph_[1] = 0.0;
        last_[0] = last_[1] = 0.0;
        next_[0] = next_[1] = 0.0;
        env_ = 0.0;
        rng_ = 0x9E3779B97F4A7C15ull;
        advanceRandom(0); advanceRandom(1);
    }
    // deterministic bounces: reseed from the transport position
    void seedFrom(int64_t playheadSamples){
        rng_ = 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(playheadSamples) * 0x2545F4914F6CDD1Dull;
        if (rng_ == 0) rng_ = 1;
        ph_[0] = ph_[1] = 0.0;
        advanceRandom(0); advanceRandom(1);
    }

    // one call per block: n samples of time, the block's input RMS, and the
    // (unmodulated) parameter values that control the sources themselves
    void update(int n, double inputRms, const float* v){
        const Ids& id = Ids::get();
        const double dt = static_cast<double>(n) / sr_;
        for (int i = 0; i < 2; ++i){
            const double rate = i == 0 ? v[id.l1Rate] : v[id.l2Rate];
            const int shape   = static_cast<int>(i == 0 ? v[id.l1Shape] : v[id.l2Shape]);
            const double dep  = (i == 0 ? v[id.l1Depth] : v[id.l2Depth]) / 100.0;
            ph_[i] += dt * rate;
            while (ph_[i] >= 1.0){ ph_[i] -= 1.0; advanceRandom(i); }
            out_[i] = shapeOf(shape, i) * dep;
        }
        const double target = std::clamp(inputRms * 3.2, 0.0, 1.0);
        const double atk = std::max(0.001, v[id.envAtk] / 1000.0);
        const double rel = std::max(0.001, v[id.envRel] / 1000.0);
        const double k = 1.0 - std::exp(-dt / (target > env_ ? atk : rel));
        env_ += (target - env_) * k;
        envOut_ = env_ * v[id.envSens] / 100.0;
    }

    // matrix source index: 0 none, 1 lfo1, 2 lfo2, 3 env, 4 env inverted
    double source(int i) const {
        switch (i){
        case 1: return out_[0];
        case 2: return out_[1];
        case 3: return envOut_;
        case 4: return -envOut_;
        default: return 0.0;
        }
    }
    double lfo(int i) const { return out_[i & 1]; }
    double env() const { return envOut_; }

private:
    void advanceRandom(int i){
        last_[i] = next_[i];
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17;
        next_[i] = static_cast<double>(rng_ >> 11) / 9007199254740992.0 * 2.0 - 1.0;
    }
    double shapeOf(int shape, int i) const {
        const double ph = ph_[i];
        switch (shape){
        case 1: return 4.0 * std::fabs(ph - 0.5) - 1.0;          // triangle
        case 2: return ph * 2.0 - 1.0;                            // saw up
        case 3: return 1.0 - ph * 2.0;                            // saw down
        case 4: return ph < 0.5 ? 1.0 : -1.0;                     // square
        case 5: return last_[i];                                  // sample and hold
        case 6: return last_[i] + (next_[i] - last_[i]) * ph;     // random smooth
        default: return std::sin(ph * 2.0 * M_PI);
        }
    }
    double sr_ = 44100.0;
    double ph_[2] = { 0, 0 }, last_[2] = { 0, 0 }, next_[2] = { 0, 0 }, out_[2] = { 0, 0 };
    double env_ = 0.0, envOut_ = 0.0;
    uint64_t rng_ = 0x9E3779B97F4A7C15ull;
};

// applies the matrix to a copy of the parameter values, in normalised space and
// exactly as the browser version does, so a patch modulates the same way
inline void applyMatrix(const ModEngine& mod, const float* base, float* out){
    const Params& P = Params::get();
    const Ids& id = Ids::get();
    std::copy(base, base + P.count(), out);
    for (int k = 0; k < numSlots; ++k){
        const int src = static_cast<int>(base[id.slotSrc[k]]);
        const int dstChoice = static_cast<int>(base[id.slotDst[k]]);
        const double amt = base[id.slotAmt[k]] / 100.0;
        if (src <= 0 || dstChoice <= 0 || amt == 0.0) continue;
        const int dst = P.dests()[static_cast<size_t>(dstChoice - 1)];   // choice 0 is "—"
        const ParamInfo& info = P[dst];
        const size_t d = static_cast<size_t>(dst);
        const float n = Params::toNorm(info, out[d]) + static_cast<float>(mod.source(src) * amt);
        out[d] = Params::fromNorm(info, n);
    }
}

} // namespace fracture
