// Oversampler.h — 2x / 4x oversampling around the four-pole, and nothing else.
//
// The converter is left at the host rate on purpose: its aliasing is the thing
// being modelled. The four-pole's is not. Its saturation sits inside a feedback
// loop, and at high drive on bright material the harmonics it makes run past
// the host's Nyquist and fold back as tones that no analogue filter ever made.
// That is a digital artefact of this plugin, not a feature of the hardware, so
// the filter runs at four times the host rate. Twice was measured and is not
// enough: at full drive the ladder is nearly a hard clipper, its harmonics fall
// away slowly, and 2x still leaves a 13 kHz alias of a 5 kHz tone only 21 dB
// under the real harmonic. 4x puts it 65 dB under (crate/tests/test_core.cpp).
//
// The same linear-phase windowed-sinc FIR the other product in this repository
// uses (plugin/core/Oversampler.h), copied rather than shared because each
// product's core builds and is tested on its own. Latency is constant and a
// whole number of host samples, so it can be reported exactly and the dry path
// delayed to match.
#pragma once
#include <vector>
#include <cmath>
#include <cstddef>
#include <algorithm>

namespace crate {

class HalfBandFir {
public:
    static constexpr int taps = 65;                  // (taps-1)/2 = 32, divisible by 4

    void prepare(){
        if (h_.empty()) design();
        z_.assign(2 * taps, 0.0);
        pos_ = 0;
    }
    // A half-band filter's even taps are exactly zero, apart from the centre,
    // so only the other 33 are visited: half the work, the same arithmetic.
    // The history is stored twice over so a tap never has to wrap.
    inline double process(double x){
        z_[pos_] = z_[pos_ + taps] = x;
        const double* w = z_.data() + pos_ + taps;   // w[-i] is the input i samples ago
        double acc = 0.0;
        for (int i : nz_) acc += h_[static_cast<size_t>(i)] * w[-i];
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
            // sin(pi n / 2) is zero for every even n, but not in floating point
            // (about 1e-17), so the zeros are written as zeros
            const double sinc = n == 0 ? 0.5
                              : (n % 2 == 0 ? 0.0 : std::sin(M_PI * 0.5 * n) / (M_PI * n));
            const double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * i / (taps - 1))
                                  + 0.08 * std::cos(4.0 * M_PI * i / (taps - 1));
            h_[static_cast<size_t>(i)] = sinc * w;
            sum += h_[static_cast<size_t>(i)];
        }
        for (auto& v : h_) v /= sum;                 // unity DC gain
        nz_.clear();
        for (int i = 0; i < taps; ++i) if (h_[static_cast<size_t>(i)] != 0.0) nz_.push_back(i);
    }
    std::vector<double> h_;
    std::vector<int> nz_;
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

    template <typename Fn>
    inline double process(double x, Fn&& f){
        if (factor_ == 1) return f(x);
        double a, b;
        s1_.up(x, a, b);
        if (factor_ == 2) return s1_.down(f(a), f(b));
        double a1, a2, b1, b2;
        s2_.up(a, a1, a2);
        const double da = s2_.down(f(a1), f(a2));
        s2_.up(b, b1, b2);
        const double db = s2_.down(f(b1), f(b2));
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

} // namespace crate
