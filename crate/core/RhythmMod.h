// RhythmMod.h — the rhythm that moves the filter.
//
// A FilterFreak-style modulator: a shape repeated once per note division,
// locked to the host's song position, with a groove that swings every second
// cycle late, an eight-step pattern for when no textbook shape is the right
// one, a stereo phase offset, and glide to take the edges off.
//
// Four decisions, each deliberate:
//
//   * Unipolar. The value is 0..1 and the filter's own Cutoff is where it
//     rests at 0, so Cutoff is where the sweep starts and Mod (in octaves,
//     either sign) is how far it goes. A bipolar sweep around the knob sounds
//     the same but makes both of those numbers harder to set by ear.
//   * Position, not integration. While the transport runs, the phase is read
//     from the song position rather than accumulated, so the same bar moves
//     the same way wherever the playhead is dropped. When it stops (or on
//     Free) it keeps running at the division's speed at the host's tempo, or
//     at Rate, because a filter that freezes when you press stop looks broken.
//   * Random is a hash of the cycle's index, not a generator, so it is also a
//     function of position: two bounces of the same bar get the same values,
//     with no seeding to get wrong.
//   * Groove works on pairs of cycles, the way swing does on a drum machine:
//     at 50% they are even; at 66% the first of each pair takes two thirds of
//     the pair's time and the second starts late. On the steps pattern that
//     is exactly swung sixteenths (or eighths, or whatever the division is).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace crate {

// note divisions in beats; the first entry is Free, so there is one list and
// no separate sync switch that could disagree with it
inline const char* const rhythmDivNames[] = {
    "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4 dot", "1/4", "1/4 trip",
    "1/8 dot", "1/8", "1/8 trip", "1/16", "1/16 trip", "1/32"
};
inline const double rhythmDivBeats[] = {
    0.0, 16.0, 8.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125
};
inline constexpr int rhythmNumDivs = 14;

inline const char* const rhythmShapeNames[] = {
    "Sine", "Triangle", "Saw up", "Saw down", "Square", "Steps", "Random"
};
inline constexpr int rhythmNumShapes = 7;

class RhythmMod {
public:
    enum Shape { Sine = 0, Triangle, SawUp, SawDown, Square, Steps, Random };
    static constexpr int numSteps = 8;

    void prepare(double sampleRate){ sr_ = sampleRate; reset(); }
    void reset(){ free_ = 0.0; out_[0] = out_[1] = 0.0; first_ = true; }

    // division: index into rhythmDivBeats (0 = Free). groove 50..75 (%),
    // phaseDeg 0..180 for the right channel, glide 0..1, steps 0..1 each.
    void configure(int division, double rateHz, int shape, double groove,
                   double phaseDeg, double glide, const double* steps){
        div_ = std::clamp(division, 0, rhythmNumDivs - 1);
        rate_ = std::max(0.01, rateHz);
        shape_ = std::clamp(shape, 0, rhythmNumShapes - 1);
        swing_ = std::clamp(groove, 50.0, 75.0) / 100.0;
        offset_ = std::clamp(phaseDeg, 0.0, 180.0) / 360.0;
        glide_ = std::clamp(glide, 0.0, 1.0);
        for (int k = 0; k < numSteps; ++k) steps_[k] = std::clamp(steps[k], 0.0, 1.0);
    }

    void beginBlock(bool playing, double ppqAtStart, double bpm){
        bpm_ = bpm > 1.0 ? bpm : 120.0;
        const double beats = rhythmDivBeats[div_];
        synced_ = beats > 0.0 && playing && bpm > 1.0;
        cyclesPerSample_ = beats > 0.0 ? bpm_ / 60.0 / beats / sr_ : rate_ / sr_;
        if (synced_) cycles_ = ppqAtStart / beats;
        // the time a cycle lasts, for sizing the glide
        const double cycleSec = beats > 0.0 ? beats * 60.0 / bpm_ : 1.0 / rate_;
        const double tau = std::max(0.0003, glide_ * cycleSec * 0.5);
        smooth_ = 1.0 - std::exp(-1.0 / (tau * sr_));
    }

    // one sample: advances the clock and returns nothing; read value(ch)
    inline void step(){
        const double c = synced_ ? cycles_ : free_;
        for (int ch = 0; ch < 2; ++ch){
            const double target = shapeAt(c + (ch ? offset_ : 0.0));
            if (first_) out_[ch] = target;
            else out_[ch] += smooth_ * (target - out_[ch]);
        }
        first_ = false;
        if (synced_) cycles_ += cyclesPerSample_;
        else free_ += cyclesPerSample_;
    }
    double value(int ch) const { return out_[ch & 1]; }

    // the shape itself, at a position measured in cycles, before any glide.
    // Public so the tests, and an editor, can ask what it should be.
    double shapeAt(double cycles) const {
        // groove: pairs of cycles, the first stretched to 2*swing of the pair
        const double pair = std::floor(cycles / 2.0);
        const double q = cycles - 2.0 * pair;                  // 0..2
        const double split = 2.0 * swing_;
        int64_t index;
        double p;
        if (q < split){ index = static_cast<int64_t>(pair) * 2;     p = q / split; }
        else          { index = static_cast<int64_t>(pair) * 2 + 1; p = (q - split) / (2.0 - split); }
        switch (shape_){
        case Triangle: return 1.0 - std::fabs(2.0 * p - 1.0);
        case SawUp:    return p;
        case SawDown:  return 1.0 - p;
        case Square:   return p < 0.5 ? 1.0 : 0.0;
        case Steps:    return steps_[static_cast<size_t>(((index % numSteps) + numSteps) % numSteps)];
        case Random:   return hash(index);
        default:       return 0.5 - 0.5 * std::cos(2.0 * M_PI * p);   // starts at rest, peaks mid-cycle
        }
    }

private:
    static double hash(int64_t i){
        uint64_t z = static_cast<uint64_t>(i) + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) / 9007199254740992.0;
    }

    double sr_ = 48000.0, bpm_ = 120.0, rate_ = 2.0;
    double swing_ = 0.5, offset_ = 0.0, glide_ = 0.0, smooth_ = 1.0;
    double cycles_ = 0.0, free_ = 0.0, cyclesPerSample_ = 0.0;
    double out_[2] = { 0.0, 0.0 };
    double steps_[numSteps] = { 1, 0, 0.6, 0, 1, 0, 0.3, 0 };
    int div_ = 9, shape_ = 0;
    bool synced_ = false, first_ = true;
};

} // namespace crate
