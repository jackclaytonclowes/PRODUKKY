// StoopCore.h — the whole processor, with no dependency on JUCE, so it builds
// and is measured with a bare compiler (stoop/tests/test_core.cpp).
//
//   in ─ gain ─ mono ─┬─ dry ─ delay (matches the wet path) ───────────────────┐
//                     └─ pitch ─ filter + drive (4x) ─ lo-fi ─ vinyl sim ─ wet ─┤
//                                                                        mix ─ out ─ clip
//
//   vinyl sim:  comp (the cutting) ─ + noise (the surface) ─ wow flutter (the turntable)
//
// The original sampler ran one effect at a time, and the way to stack them was
// to resample: play the pad through one effect, record the result, play that
// through the next. This runs the four of them at once, in the order that
// workflow usually went: pitch the sample, colour it, crush it, and put it on a
// record last, so the vinyl's compression and wow sit over everything.
//
// Each section has its own switch, as each effect had its button. A section
// that is off is still running, silently, so switching it on mid-bar picks up
// the audio where it is, and the switch crossfades over 10 ms rather than
// clicking. Off, it is multiplied by exactly zero and its controls cannot
// change a single bit of the output, which the tests check.
//
// The latency is fixed: the filter's oversampling (48 samples at 4x) plus the
// turntable's resting delay (8 ms), whatever is switched on, so the host's
// compensation never has to change and the dry path is delayed to match. The
// pitch shifter's delay is part of its sound and is not reported.
//
// Everything that moves (the wow, the drift, the flutter, the crackle, the
// pitch shifter's heads, the switches) moves once a sample, counted from
// reset, so the host's block size cannot change the sound.
#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "ParamTable.h"
#include "PitchShifter.h"
#include "FilterDrive.h"
#include "LoFi.h"
#include "Vinyl.h"
#include "Oversampler.h"

namespace stoop {

inline double dbToGain(double db){ return std::pow(10.0, db / 20.0); }

struct Ramp {
    double cur = 0.0, inc = 0.0;
    void target(double t, int n){ inc = (t - cur) / (n > 0 ? n : 1); }
    void snap(double v){ cur = v; inc = 0.0; }
    inline double next(){ cur += inc; return cur; }
};

// a section's switch: from 0 to 1 and back over 10 ms
struct Fade {
    double a = 0.0, to = 0.0;
    inline double next(double step){
        if (a < to) a = std::min(to, a + step);
        else if (a > to) a = std::max(to, a - step);
        return a;
    }
};

class Engine {
public:
    static constexpr int maxChannels = 2;

    void prepare(double sampleRate, int maxBlock){
        sr_ = sampleRate;
        (void) maxBlock;
        const Params& P = Params::get();
        v_.assign(static_cast<size_t>(P.count()), 0.0f);
        for (int i = 0; i < P.count(); ++i) v_[static_cast<size_t>(i)] = P[i].def;
        pitch_.prepare(sampleRate);
        os_[0].prepare(osFactor_); os_[1].prepare(osFactor_);
        filter_.prepare(sampleRate * osFactor_);
        lofi_.prepare(sampleRate);
        comp_.prepare(sampleRate);
        surface_.prepare(sampleRate);
        turntable_.prepare(sampleRate);
        for (auto& d : osBypass_){ d.prepare(64); d.setDelay(os_[0].latencySamples()); }
        for (auto& d : vinylBypass_){ d.prepare(turntable_.latencySamples() + 2); d.setDelay(turntable_.latencySamples()); }
        for (auto& d : dry_){ d.prepare(latencySamples() + 2); d.setDelay(latencySamples()); }
        fadeStep_ = 1.0 / (0.010 * sampleRate);
        reset();
    }
    void reset(){
        pitch_.reset(); filter_.reset(); lofi_.reset(); comp_.reset(); surface_.reset(); turntable_.reset();
        for (auto& o : os_) o.reset();
        for (auto& d : osBypass_) d.reset();
        for (auto& d : vinylBypass_) d.reset();
        for (auto& d : dry_) d.reset();
        first_ = true;
        inPeak = outPeak = 0.0f; reduction = 0.0f;
    }

    void setParam(int i, float value){ if (i >= 0 && i < static_cast<int>(v_.size())) v_[static_cast<size_t>(i)] = value; }
    float getParam(int i) const { return v_[static_cast<size_t>(i)]; }
    int latencySamples() const { return os_[0].latencySamples() + turntable_.latencySamples(); }
    // 1, 2 or 4. Takes effect at the next prepare(); the tests use it to
    // measure what the oversampling is buying
    void setOversampling(int factor){ osFactor_ = factor == 1 ? 1 : (factor == 2 ? 2 : 4); }
    // the transport jumped: the crackle, the drift and where the record is in
    // its revolution all follow the song position, so a bar renders the same
    // way every time it is played from the same place
    void seedFrom(int64_t playheadSamples){
        surface_.seedFrom(playheadSamples);
        turntable_.seedFrom(playheadSamples, revHz());
    }

    float inPeak = 0.0f, outPeak = 0.0f;
    float reduction = 0.0f;                     // the block's deepest gain reduction, dB
    double revolution() const { return turntable_.revolution(); }   // for the panel's record
    double centsNow() const { return turntable_.centsNow(); }       // how far off speed, now
    double levelDb() const { return comp_.levelDb(); }              // what the compressor hears

    void process(float* const* io, int numChannels, int n){
        if (n <= 0) return;
        const int nch = std::clamp(numChannels, 1, maxChannels);
        const Ids& id = Ids::get();
        auto val = [&](int p){ return static_cast<double>(v_[static_cast<size_t>(p)]); };
        auto on = [&](int p){ return v_[static_cast<size_t>(p)] > 0.5f; };

        auto setR = [&](Ramp& r, double t){ if (first_) r.snap(t); else r.target(t, n); };
        setR(inG_, dbToGain(val(id.inGain)));
        setR(outG_, dbToGain(val(id.outGain)));
        setR(mix_, val(id.mix) / 100.0);
        setR(semis_, val(id.pitch));
        setR(fb_, 0.9 * val(id.pitchFb) / 100.0);
        setR(bal_, val(id.pitchBal) / 100.0);
        setR(logFreq_, std::log(val(id.fltFreq)));
        setR(reso_, val(id.fltReso) / 100.0);
        setR(drive_, val(id.fltDrive) / 100.0);
        setR(logRate_, std::log(val(id.lofiRate)));
        setR(bits_, val(id.lofiBits));
        setR(comp_c_, val(id.comp) / 100.0);
        setR(noise_, val(id.noise) / 100.0);
        setR(wow_, val(id.wow) / 100.0);
        Fade* fades[] = { &fPitch_, &fFilter_, &fLofi_, &fVinyl_ };
        const int switches[] = { id.pitchOn, id.fltOn, id.lofiOn, id.vinylOn };
        for (int k = 0; k < 4; ++k){
            fades[k]->to = on(switches[k]) ? 1.0 : 0.0;
            if (first_) fades[k]->a = fades[k]->to;
        }
        filter_.setType(static_cast<int>(val(id.fltType)));
        const double revTarget = revHz();
        if (first_){
            turntable_.snapSpeed(revTarget);
            filter_.tune(std::exp(logFreq_.cur), reso_.cur);
            tunedF_ = logFreq_.cur; tunedR_ = reso_.cur;
            filter_.setDrive(drive_.cur);
            driveSet_ = drive_.cur;
        }
        first_ = false;
        const bool mono = on(id.mono), safety = on(id.safety);

        double inPk = 0.0, outPk = 0.0, redPk = 0.0;
        for (int i = 0; i < n; ++i){
            const double gIn = inG_.next(), gOut = outG_.next(), m = mix_.next();
            double x[maxChannels], y[maxChannels], dry[maxChannels];
            for (int ch = 0; ch < nch; ++ch){
                x[ch] = static_cast<double>(io[ch][i]) * gIn;
                inPk = std::max(inPk, std::fabs(x[ch]));
            }
            if (mono && nch == 2){ const double s = 0.5 * (x[0] + x[1]); x[0] = x[1] = s; }
            for (int ch = 0; ch < nch; ++ch) dry[ch] = dry_[ch].process(x[ch]);

            // ---- pitch
            {
                const double ratio = std::exp2(semis_.next() / 12.0);
                const double fb = fb_.next(), bal = bal_.next(), a = fPitch_.next(fadeStep_);
                for (int ch = 0; ch < nch; ++ch){
                    const double s = pitch_.process(ch, x[ch], fb);
                    const double e = x[ch] * (1.0 - bal) + s * bal;
                    y[ch] = x[ch] * (1.0 - a) + e * a;
                }
                pitch_.step(ratio);
            }
            // ---- filter + drive, at four times the rate; retuned every 16 samples
            {
                const double lf = logFreq_.next(), rs = reso_.next(), dr = drive_.next();
                if ((i & 15) == 0 && (lf != tunedF_ || rs != tunedR_)){
                    filter_.tune(std::exp(lf), rs);
                    tunedF_ = lf; tunedR_ = rs;
                }
                if (dr != driveSet_){ filter_.setDrive(dr); driveSet_ = dr; }
                const double a = fFilter_.next(fadeStep_);
                for (int ch = 0; ch < nch; ++ch){
                    const double b = osBypass_[ch].process(y[ch]);
                    const double e = os_[ch].process(y[ch], [&](double v){ return filter_.process(ch, v); });
                    y[ch] = b * (1.0 - a) + e * a;
                }
            }
            // ---- lo-fi
            {
                lofi_.step(std::exp(logRate_.next()));
                const double bits = bits_.next(), a = fLofi_.next(fadeStep_);
                for (int ch = 0; ch < nch; ++ch){
                    const double e = lofi_.process(ch, y[ch], bits);
                    y[ch] = y[ch] * (1.0 - a) + e * a;
                }
            }
            // ---- vinyl sim: the cutting, the surface, the turntable
            {
                const double c = comp_c_.next();
                double level = std::fabs(y[0]);
                if (nch == 2) level = std::max(level, std::fabs(y[1]));
                const double g = comp_.gain(level, c);
                surface_.configure(noise_.next());
                turntable_.step(revTarget, wow_.next());
                surface_.step(turntable_.scratchNow());
                const double a = fVinyl_.next(fadeStep_);
                for (int ch = 0; ch < nch; ++ch){
                    double e = c > 0.0 ? softLimit(y[ch] * g) : y[ch];
                    e = turntable_.process(ch, e + surface_.process(ch));
                    const double b = vinylBypass_[ch].process(y[ch]);
                    y[ch] = b * (1.0 - a) + e * a;
                }
                turntable_.advance();
                if (c > 0.0 && a > 0.0) redPk = std::max(redPk, comp_.reductionDb());
            }
            // ---- out
            for (int ch = 0; ch < nch; ++ch){
                double o = (dry[ch] * (1.0 - m) + y[ch] * m) * gOut;
                if (safety) o = softLimit(o);
                outPk = std::max(outPk, std::fabs(o));
                io[ch][i] = static_cast<float>(o);
            }
        }
        inPeak = static_cast<float>(inPk);
        outPeak = static_cast<float>(outPk);
        reduction = static_cast<float>(redPk);
    }

private:
    double revHz() const {
        const int s = std::clamp(static_cast<int>(v_.empty() ? 0.0f : v_[static_cast<size_t>(Ids::get().speed)]), 0, 2);
        return speedRpm[s] / 60.0;
    }

    double sr_ = 48000.0, fadeStep_ = 1.0 / 480.0, driveSet_ = -1.0, tunedF_ = 0.0, tunedR_ = 0.0;
    bool first_ = true;
    std::vector<float> v_;
    PitchShifter pitch_;
    FilterDrive filter_;
    Oversampler os_[maxChannels];
    int osFactor_ = 4;
    LoFi lofi_;
    VinylComp comp_;
    Surface surface_;
    Turntable turntable_;
    DelayLine osBypass_[maxChannels], vinylBypass_[maxChannels], dry_[maxChannels];
    Ramp inG_, outG_, mix_, semis_, fb_, bal_, logFreq_, reso_, drive_, logRate_, bits_, comp_c_, noise_, wow_;
    Fade fPitch_, fFilter_, fLofi_, fVinyl_;
};

} // namespace stoop
