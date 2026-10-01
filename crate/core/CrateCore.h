// CrateCore.h — the whole processor, with no dependency on JUCE, so it builds
// and is measured with a bare compiler (crate/tests/test_core.cpp).
//
//   in ─ gain ─┬─ dry ─ delay (matches the 4x) ───────────────────┐
//              ├─ high ─ + dust ─ converter ─ [ four-pole at 4x ] ─┤ wet
//              │  low (Keep sub) ─ delay (matches the 4x) ─────────┘
//              └─ hit detector ─ envelope ──┤ (cutoff)             │
//                 rhythm (host position) ───┘                      │
//                                       mix ─ feel (swing, push) ─ out gain ─ clip
//
// Keep sub splits the wet path with a Linkwitz-Riley crossover, the one a
// drum bus wants most: below it the kick stays clean and full, and only the
// rest is crushed. The two halves sum flat (LR4), with the crossover's phase
// turn and nothing else. At 20 Hz it is off and out of the signal path
// altogether, so a session saved before it existed renders bit for bit as it
// did.
//
// Lookahead holds all of the audio back 5 ms while the hit detector hears it
// as it comes, so the envelope knows each hit before the sound reaches the
// filter, as the hardware did (the note came first there). The envelope is
// itself delayed by 4 ms of that, so it opens about a millisecond before the
// hit arrives rather than spending its decay waiting. The rhythm and the feel
// are given the song position 5 ms earlier, so they stay on the audio they
// act on. Off, none of it is in the path.
//
// Two ordering decisions worth knowing:
//
//   Dust goes in BEFORE the converter, because that is the order it happened in
//   on the records this is after: someone sampled a noisy pressing, and the
//   sampler crushed the noise along with the drums.
//
//   Feel goes AFTER the mix, not on the wet path. Swinging only the wet signal
//   against an unmoved dry signal would flam every hit; the timing has to move
//   the finished thing.
#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "ParamTable.h"
#include "Converter.h"
#include "Ladder.h"
#include "Feel.h"
#include "Dust.h"
#include "HitEnv.h"
#include "Oversampler.h"
#include "RhythmMod.h"

namespace crate {

inline double dbToGain(double db){ return std::pow(10.0, db / 20.0); }

// transparent below 0.7, saturating to exactly 1.0 above it, C1 at the knee
inline double softLimit(double x){
    const double t = 0.7, a = std::fabs(x);
    if (a <= t) return x;
    return (x < 0 ? -1.0 : 1.0) * (t + (1.0 - t) * std::tanh((a - t) / (1.0 - t)));
}

// How many of the converter's bits a signal uses. A sampler only uses all of
// them at full scale: each 6.02 dB below it costs one, which is why hitting the
// converter hard was the technique. A drum peaking at -12 dBFS into twelve
// bits is using ten. At or past full scale it is all of them, and clipping.
inline double bitsInUse(double bits, double peak){
    if (peak <= 0.0) return 0.0;
    return std::clamp(bits + std::log2(peak), 0.0, bits);
}

struct Ramp {
    double cur = 0.0, inc = 0.0;
    void target(double t, int n){ inc = (t - cur) / (n > 0 ? n : 1); }
    void snap(double v){ cur = v; inc = 0.0; }
    inline double next(){ cur += inc; return cur; }
};

class Engine {
public:
    static constexpr int maxChannels = 2;

    void prepare(double sampleRate, int maxBlock){
        sr_ = sampleRate;
        juceUnused(maxBlock);
        const Params& P = Params::get();
        v_.assign(static_cast<size_t>(P.count()), 0.0f);
        for (int i = 0; i < P.count(); ++i) v_[static_cast<size_t>(i)] = P[i].def;
        conv_.prepare(sampleRate);
        os_[0].prepare(osFactor_); os_[1].prepare(osFactor_);
        ladder_.prepare(sampleRate * osFactor_);
        for (auto& d : dryDelay_){ d.prepare(64); d.setDelay(os_[0].latencySamples()); }
        for (auto& d : subDelay_){ d.prepare(64); d.setDelay(os_[0].latencySamples()); }
        lookMax_ = static_cast<int>(std::lround(0.005 * sampleRate));
        lookLead_ = static_cast<int>(std::lround(0.001 * sampleRate));
        for (auto& d : lookAudio_){ d.prepare(lookMax_ + 2); d.setDelay(lookMax_); }
        lookEnv_.prepare(lookMax_ + 2); lookEnv_.setDelay(lookMax_ - lookLead_);
        dust_.prepare(sampleRate);
        hit_.prepare(sampleRate);
        rhythm_.prepare(sampleRate);
        feel_.prepare(sampleRate, 320.0);
        feel_.setGrid(gridSteps(static_cast<int>(v_[static_cast<size_t>(Ids::get().grid)])));
        reset();
    }
    void reset(){
        conv_.reset(); ladder_.reset(); feel_.reset(); hit_.reset(); rhythm_.reset();
        for (auto& o : os_) o.reset();
        for (auto& d : dryDelay_) d.reset();
        for (auto& d : subDelay_) d.reset();
        for (auto& d : lookAudio_) d.reset();
        lookEnv_.reset();
        for (int ch = 0; ch < maxChannels; ++ch) for (auto& f : xo_[ch]) f.reset();
        subWas_ = 0.0;
        first_ = true;
        inPeak = outPeak = 0.0f;
    }

    void setParam(int i, float value){ if (i >= 0 && i < static_cast<int>(v_.size())) v_[static_cast<size_t>(i)] = value; }
    float getParam(int i) const { return v_[static_cast<size_t>(i)]; }
    // the feel section's fixed delay, plus the four-pole's oversampling
    int latencySamples() const { return feel_.latencySamples() + os_[0].latencySamples() + lookSamples(); }
    // the samples Lookahead holds the audio back (0 when it is off)
    int lookSamples() const { return v_.empty() || v_[static_cast<size_t>(Ids::get().look)] < 0.5f ? 0 : lookMax_; }
    // 1, 2 or 4. Takes effect at the next prepare(); the tests use it to
    // measure what the oversampling is buying
    void setOversampling(int factor){ osFactor_ = factor == 1 ? 1 : (factor == 2 ? 2 : 4); }
    void seedFrom(int64_t playheadSamples){ dust_.seedFrom(playheadSamples); }
    long hitCount() const { return hit_.hits; }            // for the tests
    double hitEnvelope() const { return hit_.value(); }    // what the editor could draw
    double rhythmValue(int ch = 0) const { return rhythm_.value(ch); }

    // the transport, for the grid. With nothing playing, swing does nothing.
    void setTransport(bool playing, double ppqAtBlockStart, double bpm){
        playing_ = playing; ppq_ = ppqAtBlockStart; bpm_ = bpm;
    }

    float inPeak = 0.0f, outPeak = 0.0f;
    float convPeak = 0.0f;              // the block's peak into the converter (bitsInUse)
    double swingOffsetMs() const {                       // what the editor draws
        return (feel_.currentDelaySamples() - feel_.latencySamples()) * 1000.0 / sr_;
    }

    void process(float* const* io, int numChannels, int n){
        if (n <= 0) return;
        const int nch = std::clamp(numChannels, 1, maxChannels);
        const Ids& id = Ids::get();

        // Lookahead: the audio is this many samples behind the song position
        const int look = lookSamples();
        if (look != lookWas_){ for (auto& d : lookAudio_) d.reset(); lookEnv_.reset(); lookWas_ = look; }
        const double lookBeats = look > 0 ? look / sr_ * bpm_ / 60.0 : 0.0;
        const double tune = v_[static_cast<size_t>(id.tune)];
        conv_.configure(v_[static_cast<size_t>(id.clock)] * std::pow(2.0, tune / 12.0),
                        v_[static_cast<size_t>(id.aa)] / 100.0,
                        v_[static_cast<size_t>(id.bits)],
                        v_[static_cast<size_t>(id.compand)] / 100.0,
                        v_[static_cast<size_t>(id.machine)] > 0.5f ? Machine::S900 : Machine::SP,
                        v_[static_cast<size_t>(id.trick)]);
        const double drive = v_[static_cast<size_t>(id.fltDrive)];
        const double cutoff = v_[static_cast<size_t>(id.fltFreq)];
        static constexpr int polesFor[] = { 2, 4, 6, 8 };
        ladder_.configure(cutoff, v_[static_cast<size_t>(id.fltReso)] / 100.0, drive,
                          static_cast<int>(v_[static_cast<size_t>(id.fltShape)]),
                          polesFor[std::clamp(static_cast<int>(v_[static_cast<size_t>(id.fltPoles)]), 0, 3)]);
        const double fmix = v_[static_cast<size_t>(id.fltMix)] / 100.0;
        const double rhDepth = v_[static_cast<size_t>(id.rhDepth)];
        {
            double steps[RhythmMod::numSteps];
            for (int k = 0; k < RhythmMod::numSteps; ++k) steps[k] = v_[static_cast<size_t>(id.rhStep[k])] / 100.0;
            rhythm_.configure(rhythmDivBeats[std::clamp(static_cast<int>(v_[static_cast<size_t>(id.rhDiv)]), 0, rhythmNumDivs - 1)],
                              v_[static_cast<size_t>(id.rhRate)],
                              static_cast<int>(v_[static_cast<size_t>(id.rhShape)]),
                              v_[static_cast<size_t>(id.rhGroove)],
                              v_[static_cast<size_t>(id.rhPhase)],
                              v_[static_cast<size_t>(id.rhGlide)] / 100.0, steps);
            rhythm_.beginBlock(playing_, ppq_ - lookBeats, bpm_);
        }
        const double envOct = v_[static_cast<size_t>(id.fltEnv)];
        hit_.setDecay(v_[static_cast<size_t>(id.fltDecay)]);
        dust_.configure(v_[static_cast<size_t>(id.dust)] / 100.0,
                        v_[static_cast<size_t>(id.dustTone)]);
        feel_.setGrid(gridSteps(static_cast<int>(v_[static_cast<size_t>(id.grid)])));
        feel_.setSwing(v_[static_cast<size_t>(id.swing)]);
        feel_.setPush(v_[static_cast<size_t>(id.push)]);
        feel_.beginBlock(playing_, ppq_ - lookBeats, bpm_);

        const bool wasFirst = first_;
        auto setR = [&](Ramp& r, double t){ if (first_) r.snap(t); else r.target(t, n); };
        setR(inG_, dbToGain(v_[static_cast<size_t>(id.inGain)]));
        setR(outG_, dbToGain(v_[static_cast<size_t>(id.outGain)]));
        setR(mix_, v_[static_cast<size_t>(id.mix)] / 100.0);
        first_ = false;

        // Keep sub: on above 20 Hz. A moved frequency glides, retuned every 16
        // samples; switched on, it glides up from 20 Hz, where the crossover is
        // all but transparent, so it enters the path without a step. After a
        // reset (a preset, a session) it starts where it is set
        const double subNow = v_[static_cast<size_t>(id.subHz)];
        const bool subOn = subNow > 20.5 || subWas_ > 20.5;
        const double subFrom = wasFirst ? subNow : (subWas_ > 20.5 ? subWas_ : 20.0);
        if (subOn && subFrom == subNow) setSub(subNow);
        const bool mono = v_[static_cast<size_t>(id.mono)] > 0.5f;
        const bool safety = v_[static_cast<size_t>(id.safety)] > 0.5f;
        const double driveComp = 1.0 / std::sqrt(drive);   // so Drive is character, not level

        double inPk = 0.0, outPk = 0.0, convPk = 0.0;
        for (int i = 0; i < n; ++i){
            const double gIn = inG_.next(), gOut = outG_.next(), m = mix_.next();
            double dryIn[maxChannels];
            for (int ch = 0; ch < nch; ++ch){
                dryIn[ch] = static_cast<double>(io[ch][i]) * gIn;
                inPk = std::max(inPk, std::fabs(dryIn[ch]));
            }
            if (mono && nch == 2){
                const double s = (dryIn[0] + dryIn[1]) * 0.5;
                dryIn[0] = dryIn[1] = s;
            }
            // one envelope for both channels, so a hit opens both sides together.
            // It always runs, so turning Env up mid-bar lands on the right phase
            double e = hit_.process(nch == 2 ? (dryIn[0] + dryIn[1]) * 0.5 : dryIn[0]);
            if (look > 0){                                 // the detector heard it; now hold it back
                e = lookEnv_.process(e);
                for (int ch = 0; ch < nch; ++ch) dryIn[ch] = lookAudio_[ch].process(dryIn[ch]);
            }
            rhythm_.step();                                // runs even at zero depth, to stay in phase
            if (envOct > 0.0 || rhDepth != 0.0){
                for (int ch = 0; ch < nch; ++ch)
                    ladder_.setCutoff(cutoff * std::exp2(envOct * e + rhDepth * rhythm_.value(ch)), ch);
            }
            if (subOn && subFrom != subNow && (i & 15) == 0)
                setSub(subFrom + (subNow - subFrom) * std::min(1.0, (i + 16) / static_cast<double>(n)));
            for (int ch = 0; ch < nch; ++ch){
                const double dry = dryDelay_[ch].process(dryIn[ch]);   // in step with the wet path
                double low = 0.0, high = dryIn[ch];
                if (subOn){
                    Biquad* x = xo_[ch];
                    low  = subDelay_[ch].process(x[1].process(x[0].process(dryIn[ch])));
                    high = x[3].process(x[2].process(dryIn[ch]));
                }
                double wet = high + dust_.process(ch);
                convPk = std::max(convPk, std::fabs(wet));
                wet = conv_.process(ch, wet);
                // the filter's own mix happens inside the oversampled region, so
                // both halves share its filters and no extra delay is needed
                wet = os_[ch].process(wet, [&](double v){
                    return v + (ladder_.process(ch, v) * driveComp - v) * fmix;
                });
                wet += low;
                feel_.write(ch, dry * (1.0 - m) + wet * m);
            }
            feel_.advance();
            for (int ch = 0; ch < nch; ++ch){
                double o = feel_.read(ch) * gOut;
                if (safety) o = softLimit(o);
                outPk = std::max(outPk, std::fabs(o));
                io[ch][i] = static_cast<float>(o);
            }
            feel_.step();
        }
        inPeak = static_cast<float>(inPk);
        outPeak = static_cast<float>(outPk);
        convPeak = static_cast<float>(convPk);
        // off means off: back to 20 and the crossover leaves the path, with
        // its filters cleared for the next time
        if (subNow <= 20.5 && subWas_ > 20.5)
            for (int ch = 0; ch < maxChannels; ++ch){ for (auto& f : xo_[ch]) f.reset(); subDelay_[ch].reset(); }
        subWas_ = subNow;
    }

private:
    static void juceUnused(int){}
    // two cascaded Butterworth sections each way: Linkwitz-Riley, 24 dB an octave
    void setSub(double hz){
        for (int ch = 0; ch < maxChannels; ++ch){
            xo_[ch][0].set(Biquad::LowPass, hz, sr_);  xo_[ch][1].copyCoeffs(xo_[ch][0]);
            xo_[ch][2].set(Biquad::HighPass, hz, sr_); xo_[ch][3].copyCoeffs(xo_[ch][2]);
        }
    }
    DelayLine lookAudio_[maxChannels], lookEnv_;
    int lookMax_ = 240, lookLead_ = 48, lookWas_ = 0;
    Biquad xo_[maxChannels][4];
    DelayLine subDelay_[maxChannels];
    double subWas_ = 0.0;
    double sr_ = 48000.0;
    bool first_ = true, playing_ = false;
    double ppq_ = 0.0, bpm_ = 120.0;
    std::vector<float> v_;
    Converter conv_;
    Ladder ladder_;
    Dust dust_;
    HitEnv hit_;
    RhythmMod rhythm_;
    Oversampler os_[maxChannels];
    DelayLine dryDelay_[maxChannels];
    int osFactor_ = 4;
    Feel feel_;
    Ramp inG_, outG_, mix_;
};

} // namespace crate
