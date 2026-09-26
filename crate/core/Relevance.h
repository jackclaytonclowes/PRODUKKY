// Relevance.h — which controls do nothing right now, and why.
//
// The panel dims a control whose value cannot change the sound under the
// current settings (Decay with the envelope off, Rate while the rhythm follows
// a division, anything on the wet path at 0% mix) and its tooltip says why. It
// is still usable, so a value can be set before it matters.
//
// A dim is a claim about the audio, so crate/tests/test_core.cpp checks it the
// hard way: for every control this calls idle, in a range of patches, it moves
// the control end to end and requires the output to be bit-identical.
#pragma once
#include <string>
#include <vector>
#include "ParamTable.h"
#include "RhythmMod.h"

namespace crate {

struct Idle { int param; std::string why; };

inline std::vector<Idle> idleControls(const float* v){
    const Ids& id = Ids::get();
    std::vector<Idle> out;
    auto idle = [&](int p, const std::string& why){ out.push_back({ p, why }); };

    std::vector<int> rhythm = { id.rhDiv, id.rhRate, id.rhShape, id.rhGroove, id.rhPhase, id.rhGlide };
    for (int k = 0; k < 8; ++k) rhythm.push_back(id.rhStep[k]);
    std::vector<int> filter = { id.fltFreq, id.fltReso, id.fltDrive, id.fltEnv, id.fltDecay,
                                id.fltShape, id.fltPoles, id.rhDepth };
    filter.insert(filter.end(), rhythm.begin(), rhythm.end());

    if (v[id.mix] == 0.0f){
        // only the dry signal is heard; the feel section still moves it, and
        // the grid decides the latency, so those stay live
        for (int p : { id.machine, id.tune, id.trick, id.clock, id.bits, id.compand, id.aa,
                       id.fltMix, id.dust, id.dustTone })
            idle(p, "Mix is at 0%, so only the dry signal is heard");
        for (int p : filter) idle(p, "Mix is at 0%, so only the dry signal is heard");
        return out;
    }
    if (v[id.dust] == 0.0f) idle(id.dustTone, "Dust is at 0%");
    if (v[id.fltMix] == 0.0f){
        for (int p : filter) idle(p, "Filter mix is at 0%");
        return out;
    }
    if (v[id.fltEnv] == 0.0f) idle(id.fltDecay, "Env is off");
    if (v[id.rhDepth] == 0.0f){
        for (int p : rhythm) idle(p, "Mod is off");
    } else {
        if (static_cast<int>(v[id.rhDiv]) != 0) idle(id.rhRate, "The rhythm follows a division, not Rate");
        if (static_cast<int>(v[id.rhShape]) != RhythmMod::Steps)
            for (int k = 0; k < 8; ++k) idle(id.rhStep[k], "Rhythm shape is not Steps");
    }
    return out;
}

} // namespace crate
