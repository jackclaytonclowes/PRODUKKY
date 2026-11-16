// Relevance.h — which controls do nothing right now, and why.
//
// The panel dims a control whose value cannot change the sound under the
// current settings (the pitch shifter's knobs while it is switched off, Speed
// with no wow and no noise, anything on the wet path at 0% mix) and its tooltip
// says why. It is still usable, so a value can be set before it matters.
//
// A dim is a claim about the audio, so stoop/tests/test_core.cpp checks it the
// hard way: for every control this calls idle, in a range of patches, it moves
// the control end to end and requires the output to be bit-identical.
#pragma once
#include <string>
#include <vector>
#include "ParamTable.h"

namespace stoop {

struct Idle { int param; std::string why; };

inline std::vector<Idle> idleControls(const float* v){
    const Ids& id = Ids::get();
    std::vector<Idle> out;
    auto idle = [&](int p, const std::string& why){ out.push_back({ p, why }); };
    auto off = [&](int p){ return v[p] < 0.5f; };

    const std::vector<int> pitch = { id.pitch, id.pitchFb, id.pitchBal };
    const std::vector<int> filter = { id.fltType, id.fltFreq, id.fltReso, id.fltDrive };
    const std::vector<int> lofi = { id.lofiRate, id.lofiBits };
    const std::vector<int> vinyl = { id.comp, id.noise, id.wow, id.speed };

    if (v[id.mix] == 0.0f){
        // only the dry signal is heard. Input and Mono come before the split,
        // so they stay live
        for (const auto* group : { &pitch, &filter, &lofi, &vinyl })
            for (int p : *group) idle(p, "Mix is at 0%, so only the dry signal is heard");
        for (int p : { id.pitchOn, id.fltOn, id.lofiOn, id.vinylOn })
            idle(p, "Mix is at 0%, so only the dry signal is heard");
        return out;
    }
    if (off(id.pitchOn)) for (int p : pitch) idle(p, "Pitch is switched off");
    else if (v[id.pitchBal] == 0.0f)
        for (int p : { id.pitch, id.pitchFb }) idle(p, "Balance is at 0%, so only the unshifted signal is heard");
    if (off(id.fltOn)) for (int p : filter) idle(p, "Filter is switched off");
    if (off(id.lofiOn)) for (int p : lofi) idle(p, "Lo-fi is switched off");
    if (off(id.vinylOn)) for (int p : vinyl) idle(p, "Vinyl is switched off");
    else if (v[id.wow] == 0.0f && v[id.noise] == 0.0f)
        idle(id.speed, "Speed sets the wow and the once-a-revolution tick, and Wow flutter and Noise are both at 0%");
    return out;
}

} // namespace stoop
