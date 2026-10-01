// Feel.h — swing, locked to the host grid.
//
// The whole plugin sits behind a fixed delay (the base), which is reported to
// the host as latency and therefore compensated away. Everything the feel
// section does is a deviation from that base: an off-beat sixteenth is read a
// few milliseconds later than the base, so it lands late; push reads everything
// later or earlier. Net zero at 50% swing with no push.
//
// The part that needed care is WHEN the read pointer moves, and it is subtler
// than it looks. Moving a delay pointer does not shift an event: LENGTHENING
// the delay re-reads audio that already came out, and SHORTENING it skips audio
// that never will. The first version scheduled the move against the output
// clock and, on an impulse train, played every hit twice — once on the beat and
// again a swung sixteenth later. The test caught it; ears would have too.
//
// So the move is scheduled against the position being READ, not the position
// being written, and it happens in the gap before that step's hit arrives. The
// guard is the size of the jump plus a few milliseconds, because a shortening
// jump has to land BEFORE the hit or it steps straight over it. Both moves are
// crossfaded over 8 ms, so what gets smeared is the tail of the previous hit
// rather than the attack of the next one. That smearing is the honest cost of
// swinging finished audio; a sampler moves the note instead and pays nothing.
//
// With the transport stopped there is no grid, so swing does nothing and only
// push applies. That is worth knowing rather than debugging.
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace crate {

class Feel {
public:
    void prepare(double sampleRate, double maxDelayMs){
        sr_ = sampleRate;
        const int cap = static_cast<int>(sampleRate * maxDelayMs / 1000.0) + 8;
        for (auto& b : buf_) b.assign(static_cast<size_t>(cap), 0.0f);
        cap_ = cap;
        reset();
    }
    void reset(){
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
        write_ = 0;
        cur_ = prev_ = static_cast<double>(baseSamples_);
        xfade_ = 1.0;
    }

    // grid: 2 = eighths, 4 = sixteenths, 8 = thirty-seconds. The base delay is
    // sized from the grid, because that is what bounds the largest swing offset
    void setGrid(int stepsPerBeat){
        if (stepsPerBeat == steps_) return;
        steps_ = stepsPerBeat;
        baseMs_ = stepsPerBeat <= 2 ? 200.0 : (stepsPerBeat >= 8 ? 60.0 : 120.0);
        baseSamples_ = static_cast<int>(sr_ * baseMs_ / 1000.0);
        cur_ = prev_ = static_cast<double>(baseSamples_);
    }
    int latencySamples() const { return baseSamples_; }

    void setSwing(double percent){ swing_ = std::clamp(percent, 50.0, 80.0); }
    void setPush(double ms){ push_ = std::clamp(ms, -30.0, 30.0); }

    void beginBlock(bool playing, double ppqAtStart, double bpm){
        playing_ = playing && bpm > 1.0;
        ppq_ = ppqAtStart;
        bpm_ = bpm > 1.0 ? bpm : 120.0;
        ppqPerSample_ = bpm_ / 60.0 / sr_;
    }

    inline void advance(){
        if (playing_) ppq_ += ppqPerSample_;
        // where in the bar the sample being read out right now sits
        const double readPpq = ppq_ - readPos() * ppqPerSample_;
        // the jump plus a margin: a shortening jump must land before the hit
        const double guard = (swingOffsetSamples() + 0.012 * sr_) * ppqPerSample_;
        const double target = targetFor(readPpq + guard);
        if (std::fabs(target - cur_) > 0.5){
            prev_ = readPos();                 // carry on from wherever we are
            cur_ = target;
            xfade_ = 0.0;
        }
        if (xfade_ < 1.0) xfade_ = std::min(1.0, xfade_ + 1.0 / (sr_ * 0.008));
    }

    inline void write(int ch, double x){
        buf_[ch & 1][static_cast<size_t>(write_)] = static_cast<float>(x);
    }
    inline double read(int ch) const {
        if (xfade_ >= 1.0) return tap(ch, cur_);
        // equal power, so the overlap does not dip
        const double a = std::cos(xfade_ * M_PI * 0.5), b = std::sin(xfade_ * M_PI * 0.5);
        return tap(ch, prev_) * a + tap(ch, cur_) * b;
    }
    inline void step(){ if (++write_ >= cap_) write_ = 0; }

    double currentDelaySamples() const { return readPos(); }

private:
    double readPos() const { return xfade_ >= 1.0 ? cur_ : prev_ + (cur_ - prev_) * xfade_; }
    double swingOffsetSamples() const {
        if (!playing_ || swing_ <= 50.0) return 0.0;
        return (swing_ / 100.0 - 0.5) * 2.0 * (60.0 / bpm_ / steps_ * sr_);
    }

    double targetFor(double ppq) const {
        double d = static_cast<double>(baseSamples_) + push_ * sr_ / 1000.0;
        if (playing_ && swing_ > 50.0){
            const double stepPos = ppq * steps_;
            const long stepIndex = static_cast<long>(std::floor(stepPos));
            const bool offBeat = ((stepIndex % 2) + 2) % 2 == 1;
            if (offBeat) d += swingOffsetSamples();
        }
        return std::clamp(d, 1.0, static_cast<double>(cap_ - 2));
    }
    inline double tap(int ch, double delay) const {
        double rp = static_cast<double>(write_) - delay;
        while (rp < 0) rp += cap_;
        // a position a hair under zero, plus cap_, rounds to exactly cap_: one
        // past the end. That read one float beyond the buffer, whatever was in
        // memory there (AddressSanitizer found it; npm run test:sanitize)
        int i0 = static_cast<int>(rp);
        const double fr = rp - i0;
        if (i0 >= cap_) i0 -= cap_;
        const int i1 = (i0 + 1) % cap_;
        const auto& b = buf_[ch & 1];
        return b[static_cast<size_t>(i0)] * (1.0 - fr) + b[static_cast<size_t>(i1)] * fr;
    }

    std::vector<float> buf_[2];
    int cap_ = 1, write_ = 0, steps_ = 0, baseSamples_ = 0;   // 0 so the first setGrid always configures
    double sr_ = 48000.0, baseMs_ = 120.0;
    double swing_ = 50.0, push_ = 0.0;
    double cur_ = 0.0, prev_ = 0.0, xfade_ = 1.0;
    bool playing_ = false;
    double ppq_ = 0.0, bpm_ = 120.0, ppqPerSample_ = 0.0;
};

} // namespace crate
