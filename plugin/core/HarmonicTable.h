// HarmonicTable.h — the Table drive mode: a waveshaper you draw as harmonics.
//
// Serum's harmonic editor draws a wavetable as bars, one per harmonic. An
// effect cannot make a sound from nothing, but it can reshape what comes in,
// and there is an exact counterpart for a shaper: the Chebyshev polynomials.
// T_k(cos t) = cos(k t), so a curve f = sum of c_k T_k turns a full-scale sine
// into exactly harmonic k at level c_k, for every bar drawn. On a real signal
// the same curve gives the same recipe as a harmonic character; how much of it
// comes through depends on how hard the Drive pushes the signal into the curve
// (the recipe is exact where the input fills -1..1).
//
// Four frames of sixteen bars, and a Position that morphs across them, the way
// Serum's wavetable position does: frame 1 at 0%, frame 4 at 100%, blended
// linearly in between. Position is a matrix target, so an LFO on it makes the
// harmonics wobble.
//
// Three things keep it well-behaved whatever is drawn:
//   - normalised by the sum of the bars, so no drawing can exceed full scale;
//   - shifted so that f(0) = 0, so silence stays silent (the even Chebyshevs
//     are +-1 at zero, which would otherwise be a constant offset);
//   - the input is clamped to -1..1, where the polynomials are bounded.
// And it is anti-aliased like the other shapers (Shapers.h): the
// antiderivative of a Chebyshev series is another Chebyshev series, built
// alongside, and both are evaluated with Clenshaw's recurrence.
#pragma once
#include <cmath>
#include <algorithm>
#include "Shapers.h"

namespace fracture {

inline constexpr int tableFrames = 4;
inline constexpr int tableHarmonics = 16;

// one curve, as Chebyshev coefficients: f on -1..1, F its antiderivative
class TableCurve {
public:
    // bars[k] is harmonic k+1, in -1..1 (a negative bar flips that harmonic)
    void build(const double* bars){
        double sum = 0.0;
        for (int k = 0; k < tableHarmonics; ++k) sum += std::fabs(bars[k]);
        std::fill(a_, a_ + tableHarmonics + 1, 0.0);
        std::fill(A_, A_ + tableHarmonics + 2, 0.0);
        silent_ = sum < 1.0e-9;
        if (silent_){ fLo_ = fHi_ = FLo_ = FHi_ = 0.0; return; }
        for (int k = 1; k <= tableHarmonics; ++k) a_[k] = bars[k - 1] / sum;
        // T_k(0) = cos(k pi / 2): 0 for odd k, +-1 for even
        double f0 = 0.0;
        for (int k = 2; k <= tableHarmonics; k += 2) f0 += ((k / 2) % 2 ? -1.0 : 1.0) * a_[k];
        a_[0] = -f0;
        // integrate term by term: int T0 = T1, int T1 = (T2 + T0) / 4,
        // int Tk = T(k+1) / (2(k+1)) - T(k-1) / (2(k-1))
        A_[1] += a_[0];
        A_[2] += a_[1] / 4.0; A_[0] += a_[1] / 4.0;
        for (int k = 2; k <= tableHarmonics; ++k){
            A_[k + 1] += a_[k] / (2.0 * (k + 1));
            A_[k - 1] -= a_[k] / (2.0 * (k - 1));
        }
        fLo_ = clenshaw(a_, tableHarmonics, -1.0); fHi_ = clenshaw(a_, tableHarmonics, 1.0);
        FLo_ = clenshaw(A_, tableHarmonics + 1, -1.0); FHi_ = clenshaw(A_, tableHarmonics + 1, 1.0);
    }

    double f(double x) const {
        if (silent_) return 0.0;
        if (x >= 1.0) return fHi_;
        if (x <= -1.0) return fLo_;
        return clenshaw(a_, tableHarmonics, x);
    }
    // continues in a straight line past the clamp, so it has no corner or jump
    double F(double x) const {
        if (silent_) return 0.0;
        if (x >= 1.0) return FHi_ + fHi_ * (x - 1.0);
        if (x <= -1.0) return FLo_ + fLo_ * (x + 1.0);
        return clenshaw(A_, tableHarmonics + 1, x);
    }
    // the level harmonic k comes out at from a full-scale cosine: a[k]
    double harmonic(int k) const { return k >= 1 && k <= tableHarmonics ? a_[k] : 0.0; }
    bool silent() const { return silent_; }

    // sum of c[k] T_k(x), k = 0..n
    static double clenshaw(const double* c, int n, double x){
        double b1 = 0.0, b2 = 0.0;
        for (int k = n; k >= 1; --k){ const double b0 = 2.0 * x * b1 - b2 + c[k]; b2 = b1; b1 = b0; }
        return c[0] + x * b1 - b2;
    }

private:
    double a_[tableHarmonics + 1] = {};
    double A_[tableHarmonics + 2] = {};
    double fLo_ = 0.0, fHi_ = 0.0, FLo_ = 0.0, FHi_ = 0.0;
    bool silent_ = true;
};

// the four frames, and the curve at a position between them
struct HarmonicFrames {
    double bars[tableFrames][tableHarmonics] = {};

    // position 0..1: frame 1 at 0, frame 4 at 1, linear in between
    void blend(double position, double* out) const {
        const double p = std::clamp(position, 0.0, 1.0) * (tableFrames - 1);
        const int i = std::min(static_cast<int>(p), tableFrames - 2);
        const double t = p - i;
        for (int k = 0; k < tableHarmonics; ++k)
            out[k] = bars[i][k] + (bars[i + 1][k] - bars[i][k]) * t;
    }
    void curveAt(double position, TableCurve& curve) const {
        double b[tableHarmonics];
        blend(position, b);
        curve.build(b);
    }
};

// One Table stage for one channel, anti-aliased (see Shapers.h): the average of
// the curve over the step between samples. The curve can change between
// samples when Position moves; the version number tells it to re-take the
// previous sample's antiderivative on the new curve, so the difference is
// always taken on one curve.
class TableShaper {
public:
    void reset(){ x1_ = 0.0; F1_ = 0.0; version_ = ~0u; }
    double process(const TableCurve& c, unsigned version, double x){
        if (version != version_){ version_ = version; F1_ = c.F(x1_); }
        const double F = c.F(x);
        const double dx = x - x1_;
        const double y = std::fabs(dx) > 1.0e-5 ? (F - F1_) / dx : c.f(0.5 * (x + x1_));
        x1_ = x; F1_ = F;
        return y;
    }
private:
    double x1_ = 0.0, F1_ = 0.0;
    unsigned version_ = ~0u;
};

// "Start from": the bars that give another mode's sound, so a frame can begin
// as Tube or Fold and be edited from there rather than drawn from nothing.
// They are that mode's Chebyshev coefficients at the given drive, which is to
// say exactly the harmonics it makes of a full-scale sine:
//     c_k = (2 / pi) * integral over 0..pi of shape(drive * cos t) cos(k t) dt,
// taken by the midpoint rule at 4096 points, then scaled so the largest bar is
// 100%. Past the 16th harmonic nothing can be drawn, so a hard-edged mode
// (Wrap, Quantize) comes back smoothed; the smooth ones come back close to
// their own curve (test_core measures how close). Bars under half a percent
// are left at zero, so what was silent in the source stays silent in the bars.
inline void barsFromShaper(int mode, double drive, double* bars){
    constexpr int n = 4096;
    double c[tableHarmonics] = {};
    for (int j = 0; j < n; ++j){
        const double t = M_PI * (j + 0.5) / n;
        const double y = shape(mode, drive * std::cos(t));
        for (int k = 0; k < tableHarmonics; ++k) c[k] += y * std::cos((k + 1) * t);
    }
    double peak = 0.0;
    for (int k = 0; k < tableHarmonics; ++k){ c[k] *= 2.0 / n; peak = std::max(peak, std::fabs(c[k])); }
    for (int k = 0; k < tableHarmonics; ++k){
        const double b = peak > 0.0 ? c[k] / peak : 0.0;
        bars[k] = std::fabs(b) < 0.005 ? 0.0 : b;
    }
}
// the drive one frame is filled at, and the four "all frames, gentle to hard"
// uses, so that Position sweeps the mode from barely touched to driven hard
inline constexpr double startFromDrive = 4.0;
inline constexpr double startFromDrives[tableFrames] = { 1.0, 2.0, 4.0, 8.0 };

// How far the drawn version is from the real curve: the RMS difference over
// -1..1 once the drawn one is scaled to fit best, as a fraction of the real
// one. Soft, Tube, Tape and Warm come back within 1% at drive 4; Wrap is 61%
// out, because its jumps are made of harmonics far past the 16th
inline double startFromError(int mode, double drive){
    double bars[tableHarmonics];
    barsFromShaper(mode, drive, bars);
    TableCurve c; c.build(bars);
    double gg = 0.0, gf = 0.0, ff = 0.0, e = 0.0;
    constexpr int n = 1001;
    double g[n], f[n];
    for (int i = 0; i < n; ++i){
        const double x = -1.0 + 2.0 * i / (n - 1);
        g[i] = shape(mode, drive * x); f[i] = c.f(x);
        gg += g[i] * g[i]; gf += g[i] * f[i]; ff += f[i] * f[i];
    }
    const double a = ff > 0.0 ? gf / ff : 0.0;
    for (int i = 0; i < n; ++i) e += (g[i] - a * f[i]) * (g[i] - a * f[i]);
    return gg > 0.0 ? std::sqrt(e / gg) : 1.0;
}
// offered in the menu only if every drive it could be filled at comes back
// within 15%: a frame that does not sound like its name would be a false label
inline bool startFromFits(int mode){
    if (mode < 0 || mode >= numBrowserModes) return false;
    for (double d : startFromDrives) if (startFromError(mode, d) > 0.15) return false;
    return true;
}

} // namespace fracture
