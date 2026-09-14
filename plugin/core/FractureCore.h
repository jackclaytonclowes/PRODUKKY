// FractureCore.h — the whole processor, with no dependency on JUCE or anything
// else, so it can be built and tested from a plain compiler (see
// plugin/tests/test_core.cpp, run by `npm run test:core`).
//
// Signal flow, matching fx/fracture.html:
//
//   in ─ input gain ─┬─ dry (delayed to match the oversampling latency) ───────┐
//                    └─ pre HP ─ pre LP ─ LR4 split (1, 2 or 3 bands)          │
//                          band n:  ┌── oversampled ─────────────────────────┐ │
//                                   │ stage A ─ DC ─ tilt ─ stage B ─ DC     │ │
//                                   │ crossfaded against the band's own dry  │ │
//                                   └────────────────────────────────────────┘ │
//                          summed ─┬─ crush ─ post filter ─ wet ───────────────┤
//                                  └─ feedback: delay ─ tone ─ saturator ──────┘
//                    dry/wet ─ width (M/S) ─ tremolo ─ output gain ─ safety ─ out
//
// The post filter is either the clean biquad pair or the nonlinear ladder in
// AnalogFilter.h, decided by the Circuit control; the tremolo is last, after
// the dry/wet, because an insert tremolo modulates the whole track.
//
// Differences from the browser version, all deliberate:
//   * drive evaluates f(x * drive) per sample instead of indexing a fixed curve
//   * the band's dry/wet crossfade happens INSIDE the oversampled region, so
//     the two paths share the up/downsampling filters exactly and need no
//     compensation delay
//   * the post filter's Q is a real Q (Web Audio reads it in dB for LP/HP)
//   * "off" on the post filter bypasses it rather than parking an allpass at
//     20 kHz, and the safety clip's ceiling is 0 dBFS rather than -0.9, because
//     there is no oversampled shaper left to ring past it
//   * the 3-band split allpass-compensates the low band, so the three bands sum
//     flat (the browser version splits sequentially without it)
#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <algorithm>
#include "Shapers.h"
#include "Biquad.h"
#include "Oversampler.h"
#include "Crusher.h"
#include "ParamTable.h"
#include "Modulation.h"
#include "AnalogFilter.h"
#include "Tremolo.h"
#include "Sync.h"

namespace fracture {

struct Ramp {
    double cur = 0.0, inc = 0.0;
    void target(double t, int n){ inc = (t - cur) / (n > 0 ? n : 1); }
    void snap(double v){ cur = v; inc = 0.0; }
    inline double next(){ cur += inc; return cur; }
};

inline double dbToGain(double db){ return std::pow(10.0, db / 20.0); }
// Soft limiter with a unity knee: transparent below 0.7, smoothly saturating to
// exactly 1.0 above it, C1-continuous at the knee. The browser version used
// tanh(1.4x)/tanh(1.4), whose slope at the origin is 1.58 — it quietly added
// ~4 dB and made the feedback loop gain more than its own amount setting. This
// shape does neither, so `feedback 85%` really is a decaying loop.
inline double softLimit(double x){
    const double t = 0.7, a = std::fabs(x);
    if (a <= t) return x;
    return (x < 0 ? -1.0 : 1.0) * (t + (1.0 - t) * std::tanh((a - t) / (1.0 - t)));
}

class Engine {
public:
    static constexpr int maxChannels = 2;

    void prepare(double sampleRate, int maxBlock){
        sr_ = sampleRate;
        maxBlock_ = std::max(16, maxBlock);
        const Params& P = Params::get();
        base_.assign(P.count(), 0.0f);
        mv_.assign(P.count(), 0.0f);
        for (int i = 0; i < P.count(); ++i) base_[i] = P[i].def;

        mod_.prepare(sampleRate);
        trem_.prepare(sampleRate);
        for (int ch = 0; ch < maxChannels; ++ch){
            // a different seed per channel, or both channels drift in step and
            // the whole point of the drift is lost
            ladder_[ch].prepare(sampleRate, 0x9E3779B9u + 0x7F4A7C15u * static_cast<uint32_t>(ch + 1));
            fb_[ch].prepare(sampleRate, 300.0);
            dry_[ch].prepare(static_cast<int>(sampleRate * 0.05) + 8);
        }
        setOversampling(static_cast<int>(base_[Ids::get().osFactor]));
        reset();
    }

    void reset(){
        for (int ch = 0; ch < maxChannels; ++ch){
            for (auto* f : { &preHP_[ch], &preLP_[ch], &fltA_[ch], &fltB_[ch], &fbTone_[ch], &fbHP_[ch] })
                f->reset();
            for (int b = 0; b < numBands; ++b){
                Band& bd = bands_[b];
                for (auto* f : { &bd.dcA[ch], &bd.dcB[ch], &bd.tiltLo[ch], &bd.tiltHi[ch] }) f->reset();
                bd.os[ch].reset();
            }
            for (auto* f : { &xLP1a_[ch], &xLP1b_[ch], &xHP1a_[ch], &xHP1b_[ch],
                             &xLP2a_[ch], &xLP2b_[ch], &xHP2a_[ch], &xHP2b_[ch], &xAP_[ch] })
                f->reset();
            fb_[ch].reset();
            dry_[ch].reset();
            ladder_[ch].reset();
        }
        crusher_.reset();
        mod_.reset();
        trem_.reset();
        first_ = true;
        inPeak = outPeak = 0.0f;
    }

    void setParam(int i, float v){ if (i >= 0 && i < static_cast<int>(base_.size())) base_[i] = v; }
    float getParam(int i) const { return base_[static_cast<size_t>(i)]; }
    void setParamById(const char* id, float v){ setParam(Params::get().index(id), v); }

    void setOversampling(int choice){          // 0 = off, 1 = 2x, 2 = 4x
        const int f = choice <= 0 ? 1 : (choice == 1 ? 2 : 4);
        if (f == osFactor_) return;
        osFactor_ = f;
        for (int b = 0; b < numBands; ++b)
            for (int ch = 0; ch < maxChannels; ++ch) bands_[b].os[ch].prepare(f);
        latency_ = bands_[0].os[0].latencySamples();
        for (int ch = 0; ch < maxChannels; ++ch) dry_[ch].setDelay(latency_);
    }
    int latencySamples() const { return latency_; }
    void seedFrom(int64_t playheadSamples){ mod_.seedFrom(playheadSamples); }
    // the host's clock, for the synced LFOs and the tremolo
    void setTransport(const Transport& t){ transport_ = t; }

    // meters and modulation readouts, for the editor
    float inPeak = 0.0f, outPeak = 0.0f;
    float lfo1() const { return static_cast<float>(mod_.lfo(0)); }
    float lfo2() const { return static_cast<float>(mod_.lfo(1)); }
    float envOut() const { return static_cast<float>(mod_.env()); }
    float tremOut() const { return static_cast<float>(trem_.value()); }

    void process(float* const* io, int numChannels, int n){
        if (n <= 0) return;
        const int nch = std::clamp(numChannels, 1, maxChannels);
        const Ids& id = Ids::get();

        // ---- modulation, once per block
        double rms = 0.0;
        for (int ch = 0; ch < nch; ++ch)
            for (int i = 0; i < n; ++i) rms += static_cast<double>(io[ch][i]) * io[ch][i];
        rms = std::sqrt(rms / static_cast<double>(n * nch));
        mod_.setTrem(trem_.value());               // the tremolo is a matrix source too
        mod_.update(n, rms * dbToGain(base_[id.inGain]), base_.data(), transport_);
        applyMatrix(mod_, base_.data(), mv_.data());

        setOversampling(static_cast<int>(mv_[id.osFactor]));

        // ---- per-block configuration
        const int nb = static_cast<int>(mv_[id.bands]) + 1;
        const double osSr = sr_ * osFactor_;
        int solo = 0;
        for (int b = 0; b < numBands; ++b) if (mv_[id.bandSolo[b]] > 0.5f) { solo = b + 1; break; }

        for (int ch = 0; ch < nch; ++ch){
            preHP_[ch].set(Biquad::HighPass, mv_[id.preHP], sr_);
            preLP_[ch].set(Biquad::LowPass,  mv_[id.preLP], sr_);
            xLP1a_[ch].set(Biquad::LowPass,  mv_[id.x1], sr_); xLP1b_[ch].copyCoeffs(xLP1a_[ch]);
            xHP1a_[ch].set(Biquad::HighPass, mv_[id.x1], sr_); xHP1b_[ch].copyCoeffs(xHP1a_[ch]);
            xLP2a_[ch].set(Biquad::LowPass,  mv_[id.x2], sr_); xLP2b_[ch].copyCoeffs(xLP2a_[ch]);
            xHP2a_[ch].set(Biquad::HighPass, mv_[id.x2], sr_); xHP2b_[ch].copyCoeffs(xHP2a_[ch]);
            xAP_[ch].set(Biquad::AllPass,    mv_[id.x2], sr_);   // keeps the low band in phase
            fbTone_[ch].set(Biquad::LowPass, mv_[id.fbTone], sr_);
            fbHP_[ch].set(Biquad::HighPass, 40.0, sr_);
            const int ft = static_cast<int>(mv_[id.fltType]);
            if (ft > 0){
                if (static_cast<int>(mv_[id.fltCirc]) == Ladder::Clean){
                    static const Biquad::Type map[] = { Biquad::AllPass, Biquad::LowPass, Biquad::HighPass,
                                                        Biquad::BandPass, Biquad::Notch, Biquad::Peaking };
                    fltA_[ch].set(map[ft], mv_[id.fltFreq], sr_, mv_[id.fltQ], 9.0);
                    fltB_[ch].copyCoeffs(fltA_[ch]);
                } else {
                    ladder_[ch].set(ft, mv_[id.fltPoles] > 0.5f ? 4 : 2,
                                    static_cast<int>(mv_[id.fltCirc]),
                                    mv_[id.fltFreq], mv_[id.fltQ],
                                    mv_[id.fltDrive], mv_[id.fltDrift] / 100.0);
                }
            }
            for (int b = 0; b < numBands; ++b){
                Band& bd = bands_[b];
                bd.tiltLo[ch].set(Biquad::LowShelf,  320.0,  osSr, 0.70710678, -mv_[id.bandTone[b]]);
                bd.tiltHi[ch].set(Biquad::HighShelf, 3200.0, osSr, 0.70710678,  mv_[id.bandTone[b]]);
                bd.dcA[ch].set(Biquad::HighPass, 18.0, osSr);
                bd.dcB[ch].set(Biquad::HighPass, 18.0, osSr);
            }
        }

        // on the first block after a reset, jump to the target instead of
        // gliding up from zero, or the plugin fades in on every transport start
        auto setR = [&](Ramp& r, double t){ if (first_) r.snap(t); else r.target(t, n); };

        const bool ag = mv_[id.autoGain] > 0.5f;
        for (int b = 0; b < numBands; ++b){
            Band& bd = bands_[b];
            const double dA = mv_[id.bandDriveA[b]], dB = mv_[id.bandDriveB[b]];
            bd.modeA = static_cast<int>(mv_[id.bandModeA[b]]);
            bd.modeB = static_cast<int>(mv_[id.bandModeB[b]]);
            bd.stageB = mv_[id.bandStageB[b]] > 0.5f;
            const bool audible = b < nb && mv_[id.bandMute[b]] < 0.5f && (solo == 0 || solo == b + 1);
            setR(bd.driveA, dA);
            setR(bd.driveB, dB);
            setR(bd.postA, ag ? autoGainFor(dA) : 1.0);
            setR(bd.postB, ag ? autoGainFor(dB) : 1.0);
            setR(bd.mix, mv_[id.bandMix[b]] / 100.0);
            setR(bd.level, audible ? dbToGain(mv_[id.bandLevel[b]]) : 0.0);
        }
        setR(inG_, dbToGain(mv_[id.inGain]));
        setR(outG_, dbToGain(mv_[id.outGain]));
        setR(wet_, mv_[id.mix] / 100.0);
        setR(widthR_, mv_[id.width] / 100.0);
        setR(fbAmt_, mv_[id.fbAmt] / 100.0);
        setR(crMix_, mv_[id.crMix] / 100.0);
        first_ = false;
        for (int ch = 0; ch < nch; ++ch) fb_[ch].setMs(mv_[id.fbTime]);

        // the pre-filters drop out of circuit at their extremes: a 2nd-order
        // highpass parked at 20 Hz still costs 0.8 dB at 30 Hz, and the browser
        // version paid that on every default patch for nothing
        const bool preHPOn = mv_[id.preHP] > 20.5f;
        const bool preLPOn = mv_[id.preLP] < 19500.0f;

        const double bits = mv_[id.bits];
        const int redux = std::max(1, static_cast<int>(std::lround(mv_[id.redux])));
        const bool safety = mv_[id.safety] > 0.5f;
        const int ft = static_cast<int>(mv_[id.fltType]);
        const int circuit = static_cast<int>(mv_[id.fltCirc]);
        const bool twoPole = mv_[id.fltPoles] < 0.5f;      // the clean pair is 24 dB; one of it is 12
        trem_.configure(mv_[id.trOn] > 0.5f, mv_[id.trRate], static_cast<int>(mv_[id.trDiv]),
                        mv_[id.trDepth], mv_[id.trShape], mv_[id.trEdge],
                        mv_[id.trDuty], mv_[id.trSpread], transport_);
        const bool tremOn = trem_.active();

        // ---- per sample
        double inPk = 0.0, outPk = 0.0;
        for (int i = 0; i < n; ++i){
            const double gIn = inG_.next(), gOut = outG_.next(), w = wet_.next();
            const double width = widthR_.next(), amt = fbAmt_.next(), cm = crMix_.next();
            double dA[numBands], dB[numBands], pA[numBands], pB[numBands], bm[numBands], bl[numBands];
            for (int b = 0; b < numBands; ++b){
                dA[b] = bands_[b].driveA.next(); dB[b] = bands_[b].driveB.next();
                pA[b] = bands_[b].postA.next();  pB[b] = bands_[b].postB.next();
                bm[b] = bands_[b].mix.next();    bl[b] = bands_[b].level.next();
            }
            double y[maxChannels] = { 0.0, 0.0 };
            for (int ch = 0; ch < nch; ++ch){
                const double xin = static_cast<double>(io[ch][i]) * gIn;
                inPk = std::max(inPk, std::fabs(xin));
                const double dryS = dry_[ch].process(xin);
                double pre = xin;
                if (preHPOn) pre = preHP_[ch].process(pre);
                if (preLPOn) pre = preLP_[ch].process(pre);

                double lo = 0.0, mid = 0.0, hi = 0.0;
                if (nb == 1){
                    lo = pre;
                } else if (nb == 2){
                    lo = xLP1b_[ch].process(xLP1a_[ch].process(pre));
                    mid = xHP1b_[ch].process(xHP1a_[ch].process(pre));
                } else {
                    lo = xAP_[ch].process(xLP1b_[ch].process(xLP1a_[ch].process(pre)));
                    const double h1 = xHP1b_[ch].process(xHP1a_[ch].process(pre));
                    mid = xLP2b_[ch].process(xLP2a_[ch].process(h1));
                    hi  = xHP2b_[ch].process(xHP2a_[ch].process(h1));
                }
                const double bandIn[numBands] = { lo, mid, hi };

                double sum = 0.0;
                for (int b = 0; b < nb; ++b){
                    Band& bd = bands_[b];
                    const double driveA = dA[b], driveB = dB[b], postA = pA[b], postB = pB[b], mix = bm[b];
                    const int mA = bd.modeA, mB = bd.modeB;
                    const bool sB = bd.stageB;
                    Biquad& dcA = bd.dcA[ch]; Biquad& dcB = bd.dcB[ch];
                    Biquad& tLo = bd.tiltLo[ch]; Biquad& tHi = bd.tiltHi[ch];
                    const double out = bd.os[ch].process(bandIn[b], [&](double s){
                        double v = shape(mA, s * driveA) * postA;
                        v = dcA.process(v);
                        v = tHi.process(tLo.process(v));
                        if (sB){
                            v = shape(mB, v * driveB) * postB;
                            v = dcB.process(v);
                        }
                        return s + (v - s) * mix;      // the band's own dry/wet
                    });
                    sum += out * bl[b];
                }

                // feedback around the drive section
                const double fbRead = fb_[ch].read() * amt;
                double node = sum + fbRead;
                fb_[ch].write(softLimit(fbHP_[ch].process(fbTone_[ch].process(node))));

                double v = crusher_.process(ch, node, bits, redux, cm);
                if (ft > 0){
                    if (circuit == Ladder::Clean)
                        v = twoPole ? fltA_[ch].process(v) : fltB_[ch].process(fltA_[ch].process(v));
                    else
                        v = ladder_[ch].process(v);
                }
                y[ch] = dryS * (1.0 - w) + v * w;
            }
            if (nch == 2){                                   // mid/side width
                const double m = (y[0] + y[1]) * 0.5, s = (y[0] - y[1]) * 0.5 * width;
                y[0] = m + s; y[1] = m - s;
            }
            if (tremOn){
                for (int ch = 0; ch < nch; ++ch) y[ch] *= trem_.gain(ch);
            }
            trem_.advance();                                 // free-running, so it stays in phase
            for (int ch = 0; ch < nch; ++ch){
                double o = y[ch] * gOut;
                if (safety) o = softLimit(o);
                outPk = std::max(outPk, std::fabs(o));
                io[ch][i] = static_cast<float>(o);
            }
        }
        inPeak = static_cast<float>(inPk);
        outPeak = static_cast<float>(outPk);
    }

private:
    struct Band {
        Biquad dcA[maxChannels], dcB[maxChannels], tiltLo[maxChannels], tiltHi[maxChannels];
        Oversampler os[maxChannels];
        Ramp driveA, driveB, postA, postB, mix, level;
        int modeA = 0, modeB = 4;
        bool stageB = false;
    };
    double sr_ = 44100.0;
    int maxBlock_ = 512, osFactor_ = 0, latency_ = 0;
    bool first_ = true;
    std::vector<float> base_, mv_;
    Band bands_[numBands];
    Biquad preHP_[maxChannels], preLP_[maxChannels];
    Biquad xLP1a_[maxChannels], xLP1b_[maxChannels], xHP1a_[maxChannels], xHP1b_[maxChannels];
    Biquad xLP2a_[maxChannels], xLP2b_[maxChannels], xHP2a_[maxChannels], xHP2b_[maxChannels];
    Biquad xAP_[maxChannels];
    Biquad fltA_[maxChannels], fltB_[maxChannels], fbTone_[maxChannels], fbHP_[maxChannels];
    FracDelay fb_[maxChannels];
    DelayLine dry_[maxChannels];
    Ladder ladder_[maxChannels];
    Crusher crusher_;
    ModEngine mod_;
    Tremolo trem_;
    Transport transport_;
    Ramp inG_, outG_, wet_, widthR_, fbAmt_, crMix_;
};

} // namespace fracture
