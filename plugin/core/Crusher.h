// Crusher.h — bit depth and sample-rate reduction, a straight port of the
// AudioWorkletProcessor in fx/fracture.html. It is meant to alias: that is the
// sound, so it runs at the host rate with no oversampling, exactly as before.
#pragma once
#include <cmath>

namespace fracture {

class Crusher {
public:
    void reset(){ hold_[0] = hold_[1] = 0.0; phase_[0] = phase_[1] = 0; }

    inline double process(int ch, double x, double bits, int reduce, double mix){
        const double steps = std::pow(2.0, bits) / 2.0;
        double& h = hold_[ch & 1];
        int& ph = phase_[ch & 1];
        if (ph <= 0){
            h = std::floor(x * steps + 0.5) / steps;   // JS Math.round
            ph = reduce < 1 ? 1 : reduce;
        }
        --ph;
        return x + (h - x) * mix;
    }
private:
    double hold_[2] = { 0.0, 0.0 };
    int phase_[2] = { 0, 0 };
};

} // namespace fracture
