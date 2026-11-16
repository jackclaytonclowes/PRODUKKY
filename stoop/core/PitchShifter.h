// PitchShifter.h — the pitch shifter of the sampler this box is after: Pitch
// over two octaves either way, Feedback and Balance, the three controls the
// original's Pitch effect had. How Roland built theirs is not published; this
// is the classic real-time design of that era, built from first principles.
//
// Two read heads run through a delay line at the new speed. A head reading
// faster than the audio is written (pitch up) catches up with the write
// position, so before it does it jumps back a window; one reading slower
// (pitch down) falls behind, and jumps forward. Each jump would click, so there
// are two heads half a window apart, each faded out by a sin² window as it
// reaches its jump and faded in after it. sin² of one plus sin² of the other is
// exactly one, so a steady tone keeps its level.
//
// That is also where the sound comes from: the splice repeats or skips a
// window's worth of audio every 1 / (|ratio - 1| x window) seconds, which is
// the warble and the slight doubling that this kind of shifter has and a
// modern phase vocoder does not. The window is 40 ms.
//
// Feedback sends the shifted output back into the delay line, so it is shifted
// again on the next pass: a fifth up with feedback climbs in fifths, each step
// quieter by the feedback amount. It stops short of 1 (0.9 at 100%) so the
// loop always dies away. Balance mixes the unshifted input with the shifted
// signal, 0% being only the input.
//
// The delay through the shifter moves between 2 samples and a window, so it
// has no fixed latency to report; with Balance under 100% the shifted copy
// sits up to 40 ms behind the original, as it does on any shifter like this.
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace stoop {

// four-point, third-order Hermite (Catmull-Rom) between y0 and y1 at t in [0, 1).
// At t = 0 it returns y0 exactly, which the turntable relies on: with no wow
// its read lands on a whole sample and must come out bit for bit.
inline double hermite(double ym1, double y0, double y1, double y2, double t){
    const double c1 = 0.5 * (y1 - ym1);
    const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
    const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

// a power-of-two ring buffer read at a fractional delay
class FracDelay {
public:
    void prepare(int maxDelay){
        int n = 16;
        while (n < maxDelay + 8) n <<= 1;
        buf_.assign(static_cast<size_t>(n), 0.0f);
        mask_ = n - 1;
        pos_ = 0;
    }
    void reset(){ std::fill(buf_.begin(), buf_.end(), 0.0f); pos_ = 0; }
    inline void write(double x){ buf_[static_cast<size_t>(pos_)] = static_cast<float>(x); }
    // the input d samples before the one at the write position (written
    // there now, or about to be): read(d) is x[n - d]. Needs d >= 3 if the
    // sample has not been written yet, d >= 2 if it has
    inline double read(double d) const {
        const double at = static_cast<double>(pos_) - d;
        const double fl = std::floor(at);
        const double t = at - fl;
        const int i = static_cast<int>(fl);
        auto s = [&](int k){ return static_cast<double>(buf_[static_cast<size_t>((i + k) & mask_)]); };
        if (t == 0.0) return s(0);
        return hermite(s(-1), s(0), s(1), s(2), t);
    }
    inline void advance(){ pos_ = (pos_ + 1) & mask_; }
private:
    std::vector<float> buf_;
    int mask_ = 15, pos_ = 0;
};

class PitchShifter {
public:
    static constexpr double windowSeconds = 0.040;
    static constexpr double minDelay = 3.0;            // samples; the heads never read the future

    void prepare(double sampleRate){
        sr_ = sampleRate;
        window_ = std::max(64.0, std::round(windowSeconds * sampleRate));
        for (auto& d : line_) d.prepare(static_cast<int>(window_ + minDelay) + 4);
        reset();
    }
    void reset(){
        for (auto& d : line_) d.reset();
        phase_ = 0.5;                                  // head A in the middle of its window, at full gain
    }

    // one channel's sample: the two heads, then the input (and the feedback)
    // written behind them
    inline double process(int ch, double x, double feedback){
        FracDelay& d = line_[ch & 1];
        const double pa = phase_, pb = pa + 0.5 >= 1.0 ? pa - 0.5 : pa + 0.5;
        const double sa = std::sin(M_PI * pa), sb = std::sin(M_PI * pb);
        const double y = sa * sa * d.read(minDelay + pa * window_)
                       + sb * sb * d.read(minDelay + pb * window_);
        d.write(x + feedback * y);
        return y;
    }
    // after every channel has had its sample: move the write position, and the
    // heads by the ratio. The heads' delay changes by (1 - ratio) a sample,
    // so they read at `ratio` times the speed the audio arrives
    inline void step(double ratio){
        for (auto& d : line_) d.advance();
        phase_ += (1.0 - ratio) / window_;
        phase_ -= std::floor(phase_);
    }
    double windowSamples() const { return window_; }

private:
    double sr_ = 48000.0, window_ = 1920.0, phase_ = 0.5;
    FracDelay line_[2];
};

} // namespace stoop
