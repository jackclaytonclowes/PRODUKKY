// Oversampler.h — 2x / 4x oversampling around the nonlinear stages.
//
// `oversample: '4x'` was one string in the browser; here it is real work, and
// it is the part that decides whether the distortion sounds expensive or cheap.
// Linear-phase windowed-sinc FIR, so the latency is constant and reportable:
// each 2x stage adds (taps-1)/2 samples at its own rate, which is why `taps` is
// odd and (taps-1) divisible by 4 — the total comes out as a whole number of
// host samples and getLatencySamples() can be exact.
//
// A shipping build could swap this for juce::dsp::Oversampling (polyphase, and
// cheaper); this one is here so the core has no dependencies and can be tested
// without a plugin host.
#pragma once
#include <vector>
#include <cmath>
#include <cstddef>

namespace fracture {

class HalfBandFir {
public:
    static constexpr int taps = 65;                  // (taps-1)/2 = 32, divisible by 4

    void prepare(){
        if (h_.empty()) design();
        z_.assign(taps, 0.0);
        pos_ = 0;
    }
    inline double process(double x){
        z_[pos_] = x;
        double acc = 0.0;
        size_t idx = pos_;
        for (size_t i = 0; i < static_cast<size_t>(taps); ++i){
            acc += h_[i] * z_[idx];
            idx = idx == 0 ? static_cast<size_t>(taps) - 1 : idx - 1;
        }
        pos_ = pos_ + 1 >= static_cast<size_t>(taps) ? 0 : pos_ + 1;
        return acc;
    }
    void reset(){ std::fill(z_.begin(), z_.end(), 0.0); pos_ = 0; }

private:
    void design(){
        // windowed sinc, cutoff at a quarter of the doubled rate, Blackman window
        h_.resize(static_cast<size_t>(taps));
        const int m = (taps - 1) / 2;
        double sum = 0.0;
        for (int i = 0; i < taps; ++i){
            const int n = i - m;
            const double sinc = n == 0 ? 0.5 : std::sin(M_PI * 0.5 * n) / (M_PI * n);
            const double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * i / (taps - 1))
                                  + 0.08 * std::cos(4.0 * M_PI * i / (taps - 1));
            h_[static_cast<size_t>(i)] = sinc * w;
            sum += h_[static_cast<size_t>(i)];
        }
        for (auto& v : h_) v /= sum;                 // unity DC gain
    }
    std::vector<double> h_;
    std::vector<double> z_;
    size_t pos_ = 0;
};

// One 2x stage: up(x) -> two samples, down(a,b) -> one sample.
class Stage2x {
public:
    void prepare(){ up_.prepare(); down_.prepare(); }
    void reset(){ up_.reset(); down_.reset(); }
    inline void up(double x, double& a, double& b){
        a = 2.0 * up_.process(x);                    // zero-stuffing loses 6 dB
        b = 2.0 * up_.process(0.0);
    }
    inline double down(double a, double b){
        const double y = down_.process(a);
        down_.process(b);
        return y;
    }
private:
    HalfBandFir up_, down_;
};

// factor is 1, 2 or 4. `process` hands the callback every oversampled sample.
class Oversampler {
public:
    void prepare(int factor){
        factor_ = factor == 4 ? 4 : (factor == 2 ? 2 : 1);
        s1_.prepare(); s2_.prepare();
    }
    void reset(){ s1_.reset(); s2_.reset(); }
    int factor() const { return factor_; }

    // whole host samples of delay the up/down pair introduces
    int latencySamples() const {
        const int d = (HalfBandFir::taps - 1) / 2;   // per filter, at its own rate
        if (factor_ == 1) return 0;
        if (factor_ == 2) return d;                  // d/2 up + d/2 down, at 2x
        return d + d / 2;                            // outer stage + inner stage
    }

    // The callback keeps state (filters, and the anti-aliased shapers, which
    // remember the previous sample), so it must see the samples in order. Each
    // call is its own statement for that reason: in down(f(a), f(b)) C++ leaves
    // the order of the two calls unspecified, and GCC made the second first,
    // which fed every stateful stage its pairs swapped. Clang happened to go
    // left to right, which is why no Mac build showed it.
    template <typename Fn>
    inline double process(double x, Fn&& f){
        if (factor_ == 1) return f(x);
        double a, b;
        s1_.up(x, a, b);
        if (factor_ == 2){
            const double fa = f(a);
            const double fb = f(b);
            return s1_.down(fa, fb);
        }
        double a1, a2, b1, b2;
        s2_.up(a, a1, a2);
        const double fa1 = f(a1);
        const double fa2 = f(a2);
        const double da = s2_.down(fa1, fa2);
        s2_.up(b, b1, b2);
        const double fb1 = f(b1);
        const double fb2 = f(b2);
        const double db = s2_.down(fb1, fb2);
        return s1_.down(da, db);
    }
private:
    Stage2x s1_, s2_;
    int factor_ = 4;
};

// integer delay line, for keeping the dry path aligned with the oversampled one
class DelayLine {
public:
    void prepare(int maxDelay){
        buf_.assign(static_cast<size_t>(maxDelay) + 2, 0.0f);
        pos_ = 0; delay_ = 0;
    }
    void setDelay(int d){
        delay_ = d < 0 ? 0 : (d >= static_cast<int>(buf_.size()) ? static_cast<int>(buf_.size()) - 1 : d);
    }
    void reset(){ std::fill(buf_.begin(), buf_.end(), 0.0f); pos_ = 0; }
    inline double process(double x){
        buf_[static_cast<size_t>(pos_)] = static_cast<float>(x);
        int r = pos_ - delay_;
        if (r < 0) r += static_cast<int>(buf_.size());
        const double y = buf_[static_cast<size_t>(r)];
        if (++pos_ >= static_cast<int>(buf_.size())) pos_ = 0;
        return y;
    }
private:
    std::vector<float> buf_;
    int pos_ = 0, delay_ = 0;
};

// fractional delay line for the feedback loop (ms, smoothly changeable)
class FracDelay {
public:
    void prepare(double sampleRate, double maxMs){
        sr_ = sampleRate;
        buf_.assign(static_cast<size_t>(sampleRate * maxMs / 1000.0) + 4, 0.0f);
        pos_ = 0; delaySamples_ = 1.0;
    }
    void setMs(double ms){
        delaySamples_ = clampD(ms * sr_ / 1000.0);
        step_ = 0.0; left_ = 0;
    }
    // glide to a new length over n samples: a tuned loop swept by a knob or an
    // LFO would otherwise jump once a block, which is a zipper at audio pitch
    void rampToSamples(double d, int n){
        d = clampD(d);
        if (n <= 1){ delaySamples_ = d; step_ = 0.0; left_ = 0; return; }
        step_ = (d - delaySamples_) / n; left_ = n;
    }
    double delaySamples() const { return delaySamples_; }
    double maxSamples() const { return static_cast<double>(buf_.size()) - 3.0; }
    void reset(){ std::fill(buf_.begin(), buf_.end(), 0.0f); pos_ = 0; }
    inline double read() const {
        const int n = static_cast<int>(buf_.size());
        double rp = static_cast<double>(pos_) - delaySamples_;
        while (rp < 0) rp += n;
        const int i0 = static_cast<int>(rp);
        const double fr = rp - i0;
        const int i1 = (i0 + 1) % n;
        return buf_[static_cast<size_t>(i0)] * (1.0 - fr) + buf_[static_cast<size_t>(i1)] * fr;
    }
    inline void write(double x){
        buf_[static_cast<size_t>(pos_)] = static_cast<float>(x);
        if (++pos_ >= static_cast<int>(buf_.size())) pos_ = 0;
        if (left_ > 0){ delaySamples_ += step_; --left_; }
    }
private:
    double clampD(double d) const {
        const double maxD = static_cast<double>(buf_.size()) - 3.0;
        return d < 1.0 ? 1.0 : (d > maxD ? maxD : d);
    }
    std::vector<float> buf_;
    double sr_ = 44100.0, delaySamples_ = 1.0, step_ = 0.0;
    int left_ = 0;
    int pos_ = 0;
};

} // namespace fracture
