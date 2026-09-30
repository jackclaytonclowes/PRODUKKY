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
//                                     (or, with FB through drive, back into
//                                      the split, so every repeat is driven again)
//                    dry/wet ─ width (M/S) ─ tremolo ─ output gain ─ safety ─ out
//
// The post filter is either a cascade of clean biquads (one per 12 dB of slope)
// or the nonlinear ladder in AnalogFilter.h, decided by the Circuit control. A
// rhythm (RhythmMod.h) can move its cutoff in time with the host, and it has
// its own mix against what went into it. The tremolo is last, after the
// dry/wet, because an insert tremolo modulates the whole track.
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
#include <complex>
#include "Shapers.h"
#include "Biquad.h"
#include "Oversampler.h"
#include "Crusher.h"
#include "ParamTable.h"
#include "Modulation.h"
#include "AnalogFilter.h"
#include "Tremolo.h"
#include "Sync.h"
#include "RhythmMod.h"
#include "FilterResponse.h"

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
        rhythm_.prepare(sampleRate);
        trem_.prepare(sampleRate);
        for (int ch = 0; ch < maxChannels; ++ch){
            // a different seed per channel, or both channels drift in step and
            // the whole point of the drift is lost
            ladder_[ch].prepare(sampleRate, 0x9E3779B9u + 0x7F4A7C15u * static_cast<uint32_t>(ch + 1));
            // long enough for a synced division: a 1/4 at 60 BPM is a second
            fb_[ch].prepare(sampleRate, 2100.0);
            dry_[ch].prepare(static_cast<int>(sampleRate * 0.05) + 8);
        }
        setOversampling(static_cast<int>(base_[Ids::get().osFactor]));
        reset();
    }

    void reset(){
        for (int ch = 0; ch < maxChannels; ++ch){
            for (auto* f : { &preHP_[ch], &preLP_[ch], &fbTone_[ch], &fbHP_[ch] })
                f->reset();
            for (auto& f : flt_[ch]) f.reset();
            for (int b = 0; b < numBands; ++b){
                Band& bd = bands_[b];
                for (auto* f : { &bd.dcA[ch], &bd.dcB[ch], &bd.tiltLo[ch], &bd.tiltHi[ch] }) f->reset();
                bd.os[ch].reset();
                bd.shA[ch].reset(); bd.shB[ch].reset();
                bd.tbA[ch].reset(); bd.tbB[ch].reset();
            }
            for (auto* f : { &xLP1a_[ch], &xLP1b_[ch], &xHP1a_[ch], &xHP1b_[ch],
                             &xLP2a_[ch], &xLP2b_[ch], &xHP2a_[ch], &xHP2b_[ch], &xAP_[ch] })
                f->reset();
            fb_[ch].reset();
            dry_[ch].reset();
            ladder_[ch].reset();
        }
        tableReady_ = false;
        crusher_.reset();
        mod_.reset();
        rhythm_.reset();
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
    // the tests compare against the shapers without ADAA; nothing else turns it off
    void setAntialiasing(bool on){
        antialias_ = on;
        for (auto& bd : bands_) for (int ch = 0; ch < maxChannels; ++ch){
            bd.shA[ch].setEnabled(on); bd.shB[ch].setEnabled(on);
        }
    }
    void seedFrom(int64_t playheadSamples){ mod_.seedFrom(playheadSamples); }
    // the host's clock, for the synced LFOs and the tremolo
    void setTransport(const Transport& t){ transport_ = t; }

    // meters and modulation readouts, for the editor
    float inPeak = 0.0f, outPeak = 0.0f;
    float lfo1() const { return static_cast<float>(mod_.lfo(0)); }
    float lfo2() const { return static_cast<float>(mod_.lfo(1)); }
    float envOut() const { return static_cast<float>(mod_.env()); }
    float tremOut() const { return static_cast<float>(trem_.value()); }
    float rhythmOut() const { return static_cast<float>(rhythm_.value(0)); }
    // the post filter as it is right now, cutoff modulation and rhythm included,
    // for the panel to draw its response (FilterResponse.h). Written once a block
    // on the audio thread; the processor copies it into atomics for the editor
    FilterState filterState() const { return filterNow_; }
    // where the Table is across its frames right now, modulation included
    // (0..1), for the editor to mark; -1 until a band has used it
    double tablePosition() const { return tableReady_ ? curvePos_ : -1.0; }

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
        const bool wasFirst = first_;          // setR below clears first_
        int solo = 0;
        for (int b = 0; b < numBands; ++b) if (mv_[id.bandSolo[b]] > 0.5f) { solo = b + 1; break; }

        for (int ch = 0; ch < nch; ++ch){
            preHP_[ch].set(Biquad::HighPass, mv_[id.preHP], sr_);
            preLP_[ch].set(Biquad::LowPass,  mv_[id.preLP], sr_);
            // the crossovers and tilts start the block where the last one left
            // them and glide to the new values inside it (see the sample loop)
            setSplit(ch, wasFirst ? mv_[id.x1] : x1Was_, wasFirst ? mv_[id.x2] : x2Was_);
            fbTone_[ch].set(Biquad::LowPass, mv_[id.fbTone], sr_);
            // the loop's DC blocker. At 40 Hz it bends the phase of anything
            // near it, which on a tuned loop pulls the partials of a low note
            // apart, so Pitch mode moves it down out of the musical range
            fbHP_[ch].set(Biquad::HighPass, static_cast<int>(mv_[id.fbMode]) == 1 ? 10.0 : 40.0, sr_);
            const int ft = static_cast<int>(mv_[id.fltType]);
            if (ft > 0){
                if (static_cast<int>(mv_[id.fltCirc]) == Ladder::Clean){
                    setCleanFilter(ch, ft, mv_[id.fltFreq]);
                } else {
                    ladder_[ch].set(ft, polesFor(mv_[id.fltPoles]),
                                    static_cast<int>(mv_[id.fltCirc]),
                                    mv_[id.fltFreq], mv_[id.fltQ],
                                    mv_[id.fltDrive], mv_[id.fltDrift] / 100.0);
                }
            }
            for (int b = 0; b < numBands; ++b){
                Band& bd = bands_[b];
                setTilt(bd, ch, wasFirst ? mv_[id.bandTone[b]] : toneWas_[b], osSr);
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
        // ---- the Table mode (HarmonicTable.h), only when a band is using it.
        // The bars are not modulated; they are rebuilt when someone draws.
        // Position is, and an LFO on it arrives once a block like everything
        // else, so it glides across the block (see the sample loop) rather
        // than stepping the harmonics at each block edge
        const int tableMode = static_cast<int>(Mode::Table);
        bool tableOn = false;
        for (int b = 0; b < nb; ++b){
            const Band& bd = bands_[b];
            tableOn = tableOn || bd.modeA == tableMode || (bd.stageB && bd.modeB == tableMode);
        }
        const double tblPosNow = mv_[id.tblPos] / 100.0;
        bool tableGliding = false;
        if (tableOn){
            bool barsMoved = !tableReady_;
            for (int fr = 0; fr < tableFrames; ++fr)
                for (int k = 0; k < tableHarmonics; ++k){
                    const double v = mv_[id.tblBar[fr][k]] / 100.0;
                    if (v != frames_.bars[fr][k]){ frames_.bars[fr][k] = v; barsMoved = true; }
                }
            const double from = (wasFirst || !tableReady_) ? tblPosNow : tblPosWas_;
            if (barsMoved || from != curvePos_){
                frames_.curveAt(from, curve_);
                curvePos_ = from;
                ++curveVersion_;
            }
            tableGliding = tblPosNow != from;
            tblPosWas_ = from;
            tableReady_ = true;
        }
        setR(inG_, dbToGain(mv_[id.inGain]));
        setR(outG_, dbToGain(mv_[id.outGain]));
        setR(wet_, mv_[id.mix] / 100.0);
        setR(widthR_, mv_[id.width] / 100.0);
        setR(fbAmt_, mv_[id.fbAmt] / 100.0);
        setR(crMix_, mv_[id.crMix] / 100.0);
        first_ = false;
        const int fbMode = static_cast<int>(mv_[id.fbMode]);
        const bool fbThru = mv_[id.fbThru] > 0.5f;
        {
            double d = 0.0;
            if (fbMode == 1) d = tunedLoopSamples(mv_[id.fbNote], fbThru, nb, solo);
            else if (fbMode == 2){
                const double bpm = transport_.valid && transport_.bpm > 1.0 ? transport_.bpm : 120.0;
                // the whole loop is one division; through the drive, the
                // oversampler's latency is part of it
                d = beatsForDiv(static_cast<int>(mv_[id.fbDiv]) + 1) * 60.0 / bpm * sr_
                    - (fbThru ? latency_ : 0) - 1.0;
            }
            for (int ch = 0; ch < nch; ++ch){
                if (fbMode == 0) fb_[ch].setMs(mv_[id.fbTime]);
                else if (first_ || fbModeWas_ != fbMode) fb_[ch].rampToSamples(d, 0);
                else fb_[ch].rampToSamples(d, n);
            }
            fbModeWas_ = fbMode;
        }

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
        // one clean section per 12 dB, except that Peak stops at two: cascading
        // a 9 dB boost four times would be a 36 dB spike, not a steeper shape
        const int sections = ft == 5 ? std::min(2, polesFor(mv_[id.fltPoles]) / 2)
                                     : polesFor(mv_[id.fltPoles]) / 2;
        const double fmix = mv_[id.fltMix] / 100.0;
        const double rhDepth = mv_[id.rhDepth];
        const bool rhythmOn = ft > 0 && rhDepth != 0.0;
        {
            double steps[RhythmMod::numSteps];
            for (int k = 0; k < RhythmMod::numSteps; ++k) steps[k] = base_[id.rhStep[k]] / 100.0;
            rhythm_.configure(beatsForDiv(static_cast<int>(mv_[id.rhDiv])), mv_[id.rhRate],
                              static_cast<int>(mv_[id.rhShape]), mv_[id.rhGroove],
                              mv_[id.rhPhase], mv_[id.rhGlide] / 100.0, steps);
            rhythm_.beginBlock(transport_.valid && transport_.playing, transport_.ppq,
                               transport_.valid ? transport_.bpm : 120.0);
        }
        const double cutoffBase = mv_[id.fltFreq];
        filterNow_.type = ft;
        filterNow_.circuit = circuit;
        filterNow_.poles = polesFor(mv_[id.fltPoles]);
        filterNow_.freq = rhythmOn ? cutoffBase * std::exp2(rhDepth * rhythm_.value(0)) : cutoffBase;
        filterNow_.q = mv_[id.fltQ];
        filterNow_.drive = mv_[id.fltDrive];
        filterNow_.mix = fmix;
        trem_.configure(mv_[id.trOn] > 0.5f, mv_[id.trRate], static_cast<int>(mv_[id.trDiv]),
                        mv_[id.trDepth], mv_[id.trShape], mv_[id.trEdge],
                        mv_[id.trDuty], mv_[id.trSpread], transport_);
        const bool tremOn = trem_.active();

        bool gliding = false;
        if (!wasFirst){
            gliding = mv_[id.x1] != x1Was_ || mv_[id.x2] != x2Was_;
            for (int b = 0; b < numBands; ++b) gliding = gliding || mv_[id.bandTone[b]] != toneWas_[b];
        }
        // ---- per sample
        double inPk = 0.0, outPk = 0.0;
        for (int i = 0; i < n; ++i){
            // the rhythm runs whether or not it is turned up, so it stays in
            // phase; it retunes the filter every 16 samples (a third of a
            // millisecond at 48 kHz), which is smooth and a sixteenth the cost
            rhythm_.step();
            // A knob moved by the host arrives once a block. The crossovers and
            // the band tilts are filters whose coefficients would otherwise jump
            // at each block edge; measured on a sine under a host-rate sweep
            // that is +6 to +8 dB of inharmonic zipper. So they glide across the
            // block, retuned every 16 samples, and only when they are moving
            if (gliding && (i & 15) == 0){
                const double t = std::min(1.0, (i + 16) / static_cast<double>(n));
                const double x1 = x1Was_ + (mv_[id.x1] - x1Was_) * t;
                const double x2 = x2Was_ + (mv_[id.x2] - x2Was_) * t;
                for (int ch = 0; ch < nch; ++ch){
                    setSplit(ch, x1, x2);
                    for (int b = 0; b < numBands; ++b)
                        setTilt(bands_[b], ch, toneWas_[b] + (mv_[id.bandTone[b]] - toneWas_[b]) * t, osSr);
                }
            }
            if (tableGliding && (i & 15) == 0){
                const double t = std::min(1.0, (i + 16) / static_cast<double>(n));
                curvePos_ = tblPosWas_ + (tblPosNow - tblPosWas_) * t;
                frames_.curveAt(curvePos_, curve_);
                ++curveVersion_;
            }
            if (rhythmOn && (i & 15) == 0){
                for (int ch = 0; ch < nch; ++ch){
                    const double f = cutoffBase * std::exp2(rhDepth * rhythm_.value(ch));
                    if (circuit == Ladder::Clean) setCleanFilter(ch, ft, f);
                    else ladder_[ch].retune(f);
                }
            }
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
                // through the drive, the repeats go in after the pre-filters and
                // before the split, so each one is split and driven again
                const double fbRead = fb_[ch].read() * amt;
                if (fbThru) pre += fbRead;

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
                    AntialiasedShaper& shA = bd.shA[ch]; AntialiasedShaper& shB = bd.shB[ch];
                    TableShaper& tbA = bd.tbA[ch]; TableShaper& tbB = bd.tbB[ch];
                    const double out = bd.os[ch].process(bandIn[b], [&](double s){
                        double v = (mA == tableMode ? tbA.process(curve_, curveVersion_, s * driveA)
                                                    : shA.process(mA, s * driveA)) * postA;
                        v = dcA.process(v);
                        v = tHi.process(tLo.process(v));
                        if (sB){
                            v = (mB == tableMode ? tbB.process(curve_, curveVersion_, v * driveB)
                                                 : shB.process(mB, v * driveB)) * postB;
                            v = dcB.process(v);
                        }
                        return s + (v - s) * mix;      // the band's own dry/wet
                    });
                    sum += out * bl[b];
                }

                // feedback around the drive section
                double node = fbThru ? sum : sum + fbRead;
                fb_[ch].write(softLimit(fbHP_[ch].process(fbTone_[ch].process(node))));

                double v = crusher_.process(ch, node, bits, redux, cm);
                if (ft > 0){
                    const double into = v;
                    if (circuit == Ladder::Clean){
                        for (int k = 0; k < sections; ++k) v = flt_[ch][k].process(v);
                    } else {
                        v = ladder_[ch].process(v);
                    }
                    v = into + (v - into) * fmix;            // the filter's own mix
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
        x1Was_ = mv_[id.x1]; x2Was_ = mv_[id.x2];
        if (tableOn) tblPosWas_ = tblPosNow;
        for (int b = 0; b < numBands; ++b) toneWas_[b] = mv_[id.bandTone[b]];
    }

private:
    struct Band {
        Biquad dcA[maxChannels], dcB[maxChannels], tiltLo[maxChannels], tiltHi[maxChannels];
        Oversampler os[maxChannels];
        AntialiasedShaper shA[maxChannels], shB[maxChannels];   // ADAA: see Shapers.h
        TableShaper tbA[maxChannels], tbB[maxChannels];         // the Table mode
        Ramp driveA, driveB, postA, postB, mix, level;
        int modeA = 0, modeB = 4;
        bool stageB = false;
    };
    double sr_ = 44100.0;
    int maxBlock_ = 512, osFactor_ = 0, latency_ = 0;
    bool first_ = true;
    bool antialias_ = true;
    // the Table mode: the frames as last read, the curve now, where it is
    HarmonicFrames frames_;
    TableCurve curve_;
    unsigned curveVersion_ = 0;
    double curvePos_ = -1.0, tblPosWas_ = 0.0;
    bool tableReady_ = false;
    std::vector<float> base_, mv_;
    Band bands_[numBands];
    Biquad preHP_[maxChannels], preLP_[maxChannels];
    Biquad xLP1a_[maxChannels], xLP1b_[maxChannels], xHP1a_[maxChannels], xHP1b_[maxChannels];
    Biquad xLP2a_[maxChannels], xLP2b_[maxChannels], xHP2a_[maxChannels], xHP2b_[maxChannels];
    Biquad xAP_[maxChannels];
    Biquad flt_[maxChannels][4], fbTone_[maxChannels], fbHP_[maxChannels];
    RhythmMod rhythm_;
    FilterState filterNow_;
    double x1Was_ = 220.0, x2Was_ = 2200.0, toneWas_[numBands] = { 0, 0, 0 };

    void setSplit(int ch, double x1, double x2){
        xLP1a_[ch].set(Biquad::LowPass,  x1, sr_); xLP1b_[ch].copyCoeffs(xLP1a_[ch]);
        xHP1a_[ch].set(Biquad::HighPass, x1, sr_); xHP1b_[ch].copyCoeffs(xHP1a_[ch]);
        xLP2a_[ch].set(Biquad::LowPass,  x2, sr_); xLP2b_[ch].copyCoeffs(xLP2a_[ch]);
        xHP2a_[ch].set(Biquad::HighPass, x2, sr_); xHP2b_[ch].copyCoeffs(xHP2a_[ch]);
        xAP_[ch].set(Biquad::AllPass,    x2, sr_);   // keeps the low band in phase
    }
    template <typename B> static void setTilt(B& bd, int ch, double dB, double osSr){
        bd.tiltLo[ch].set(Biquad::LowShelf,  320.0,  osSr, 0.70710678, -dB);
        bd.tiltHi[ch].set(Biquad::HighShelf, 3200.0, osSr, 0.70710678,  dB);
    }
    int fbModeWas_ = 0;

    // The delay, in samples, that makes the whole loop ring at `note`. The
    // loop is the delay line plus everything else between its output and its
    // input, and each of those has a phase at the target pitch that moves the
    // note it rings at. The tone and DC filters alone put an A3 about 4 cents
    // flat; through the drive, the oversampler's latency is 48 samples at 4x,
    // which is almost a whole cycle of a 1 kHz tone. So the phase of the loop
    // at that frequency is computed from the same filters the audio runs
    // through, and the delay is shortened by exactly that much.
    //
    // Two parts are not linear, and are not compensated: the shapers, whose
    // phase at small signal is zero but which add harmonics when driven, and
    // the saturator at the loop's input. Both are memoryless, so they cannot
    // retune the fundamental; they colour it.
    double tunedLoopSamples(double note, bool thru, int nb, int solo){
        const double f = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
        const double w = 2.0 * M_PI * f / sr_;
        const double period = sr_ / f;
        std::complex<double> h = fbTone_[0].at(w) * fbHP_[0].at(w);
        double pure = 0.0;
        if (thru){
            h *= splitResponse(w, nb, solo);
            pure += latency_;
        }
        // the phase delay of the filters, taken the short way round: the loop
        // only has to be in phase modulo a whole cycle
        const double phaseDelay = -std::arg(h) / w;
        double d = period - phaseDelay - pure;
        // linear interpolation is not a pure fractional delay: its phase delay
        // at w differs from the fraction asked for. Two passes of correcting
        // for that is well inside a cent
        for (int k = 0; k < 2; ++k){
            if (d < 1.0) break;
            const double a = std::ceil(d) - d;             // weight on the older sample
            const std::complex<double> interp = a + (1.0 - a) * std::polar(1.0, -w);
            const double got = std::floor(d) + (-std::arg(interp) / w);
            if (a > 0.0 && a < 1.0) d += d - got;
        }
        // too short to fit (a high note through the oversampler): ring an
        // octave down rather than out of tune
        while (d < 1.0) d += period;
        return d;
    }

    // what the split and the bands do to a small signal at w: the crossover's
    // sum, each band's DC blockers on its wet share, its level, mute and solo.
    // At unity drive and 0 dB tilt this is exactly the linear part of the path
    std::complex<double> splitResponse(double w, int nb, int solo) const {
        const Ids& id = Ids::get();
        const double wOs = w / osFactor_;
        std::complex<double> xo[numBands];
        if (nb == 1){ xo[0] = 1.0; xo[1] = xo[2] = 0.0; }
        else if (nb == 2){
            xo[0] = xLP1a_[0].at(w) * xLP1b_[0].at(w);
            xo[1] = xHP1a_[0].at(w) * xHP1b_[0].at(w);
            xo[2] = 0.0;
        } else {
            const std::complex<double> h1 = xHP1a_[0].at(w) * xHP1b_[0].at(w);
            xo[0] = xAP_[0].at(w) * xLP1a_[0].at(w) * xLP1b_[0].at(w);
            xo[1] = h1 * xLP2a_[0].at(w) * xLP2b_[0].at(w);
            xo[2] = h1 * xHP2a_[0].at(w) * xHP2b_[0].at(w);
        }
        std::complex<double> sum = 0.0;
        for (int b = 0; b < nb; ++b){
            const bool audible = mv_[id.bandMute[b]] < 0.5f && (solo == 0 || solo == b + 1);
            if (!audible) continue;
            const Band& bd = bands_[b];
            // an anti-aliased stage averages this sample with the last one
            // (Shapers.h), which to a small signal is (1 + z^-1) / 2 at the
            // oversampled rate: half a sample of delay the loop has to allow for
            const std::complex<double> adaa = 0.5 * (1.0 + std::polar(1.0, -wOs));
            const bool stageB = mv_[id.bandStageB[b]] > 0.5f;
            std::complex<double> wet = bd.dcA[0].at(wOs);
            if (antiAliased(bd.modeA)) wet *= adaa;
            if (stageB){
                wet *= bd.dcB[0].at(wOs);
                if (antiAliased(bd.modeB)) wet *= adaa;
            }
            const double m = mv_[id.bandMix[b]] / 100.0;
            sum += xo[b] * ((1.0 - m) + m * wet) * dbToGain(mv_[id.bandLevel[b]]);
        }
        return std::abs(sum) > 1e-12 ? sum : std::complex<double>(1.0);
    }

    // stages that average over the step (half a sample of delay at the
    // oversampled rate): every shaper with an antiderivative, and the Table
    bool antiAliased(int mode) const {
        return mode == static_cast<int>(Mode::Table) || (antialias_ && hasAntiderivative(mode));
    }
    static int polesFor(float choice){ return 2 * (std::clamp(static_cast<int>(choice), 0, 3) + 1); }
    void setCleanFilter(int ch, int type, double freq){
        static const Biquad::Type map[] = { Biquad::AllPass, Biquad::LowPass, Biquad::HighPass,
                                            Biquad::BandPass, Biquad::Notch, Biquad::Peaking };
        flt_[ch][0].set(map[type], freq, sr_, mv_[Ids::get().fltQ], 9.0);
        for (int k = 1; k < 4; ++k) flt_[ch][k].copyCoeffs(flt_[ch][0]);
    }
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
