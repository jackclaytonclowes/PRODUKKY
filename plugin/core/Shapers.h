// Shapers.h — the fourteen distortion modes, ported from fx/fracture.html.
//
// These are the same functions the browser version uses, evaluated per sample
// instead of baked into a WaveShaperNode curve. The browser needed a fixed
// curve over [-D_MAX, D_MAX] with a pre-gain of drive/D_MAX, because
// WaveShaperNode clamps its input to [-1,1] and swapping a curve at control
// rate clicks. None of that applies here: we just evaluate f(x * drive), which
// is both simpler and exact — there is no curve-interpolation error left.
//
// Every mode has unity slope at the origin and saturates towards +/-1, so
// changing mode at a given drive does not jump in level. Harmonics is the one
// deliberate exception: Chebyshev mixing gives it a shallow origin slope, and
// that is the character.
//
// plugin/tests/test_core.cpp checks each function against a reference table
// generated from the JavaScript, so a typo in a formula fails the build rather
// than shipping as a different-sounding plugin.
#pragma once
#include <cmath>
#include <cstddef>

namespace fracture {

enum class Mode {
    Soft = 0, Tube, Warm, Diode, Hard, Tape, Fold, Sine, Warp, Wrap, Gap, Rect, Bits, Harm,
    Count
};

inline constexpr int numModes = static_cast<int>(Mode::Count);

// ids and names in the same order as MODES in fx/fracture.html
inline const char* modeId(int i){
    static const char* ids[] = { "soft","tube","warm","diode","hard","tape","fold",
                                 "sine","warp","wrap","gap","rect","bits","harm" };
    return ids[i];
}
inline const char* modeName(int i){
    static const char* names[] = { "Soft","Tube","Warm","Diode","Hard","Tape","Fold",
                                   "Sine","Warp","Wrap","Gap","Rectify","Quantize","Harmonics" };
    return names[i];
}

template <typename T> inline T clampT(T v, T lo, T hi){ return v < lo ? lo : (v > hi ? hi : v); }

// JavaScript's Math.round rounds half towards +infinity; std::round rounds half
// away from zero. Quantize is the one mode that can land on a half step, so it
// uses the JS rule to stay bit-identical to the browser version.
inline double jsRound(double v){ return std::floor(v + 0.5); }

// JavaScript's % keeps the sign of the dividend, like std::fmod.
inline double posMod(double a, double n){ return std::fmod(std::fmod(a, n) + n, n); }

inline double shape(int mode, double x){
    switch (static_cast<Mode>(mode)){
    case Mode::Soft:  return std::tanh(x);
    case Mode::Tube: {                              // asymmetric tanh
        if (x >  40.0) return 1.0;
        if (x < -40.0) return -1.0;
        const double a = std::exp(x * 0.8), b = std::exp(-x * 1.2);
        return (a - b) / (a + b);
    }
    case Mode::Warm:  return (x < 0 ? -1.0 : 1.0) * (1.0 - std::exp(-std::fabs(x)));
    case Mode::Diode: return x > 0 ? 1.0 - std::exp(-x)
                                   : -0.55 * (1.0 - std::exp(x / 0.55));
    case Mode::Hard:  return clampT(x, -1.0, 1.0);
    case Mode::Tape: { const double t = std::tanh(x); return t - 0.12 * t * t * t; }
    case Mode::Fold: { const double y = posMod(x + 1.0, 4.0); return y < 2.0 ? y - 1.0 : 3.0 - y; }
    case Mode::Sine:  return std::sin(x);
    case Mode::Warp:  return std::sin(x + 0.35 * std::sin(2.0 * x));
    case Mode::Wrap:  return posMod(x + 1.0, 2.0) - 1.0;     // discontinuous, brutal
    case Mode::Gap: {                                        // crossover / dead-zone grit
        const double dz = 0.12, a = std::fabs(x);
        return a < dz ? 0.0 : std::tanh((x < 0 ? -1.0 : 1.0) * (a - dz) * 1.25);
    }
    case Mode::Rect: { const double t = std::tanh(x); return 0.5 * t + 0.5 * std::fabs(t); }
    case Mode::Bits: { const double t = clampT(x, -1.0, 1.0), s = 7.0; return jsRound(t * s) / s; }
    case Mode::Harm: {                                       // Chebyshev 1/3/5
        const double t = clampT(x, -1.0, 1.0);
        const double t3 = 4.0 * t * t * t - 3.0 * t;
        const double t5 = 16.0 * std::pow(t, 5) - 20.0 * t * t * t + 5.0 * t;
        return 0.5 * t + 0.35 * t3 + 0.15 * t5;
    }
    case Mode::Count:
    default: return std::tanh(x);
    }
}

// Loudness match so that turning drive up is a change of character rather than
// just a change of level. Same exponent as the browser version.
inline double autoGainFor(double drive){ return std::pow(drive, -0.55); }

} // namespace fracture
