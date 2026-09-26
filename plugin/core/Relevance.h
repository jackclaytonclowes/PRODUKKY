// Relevance.h — which controls do nothing right now, and why.
//
// The panel dims a control whose value cannot change the sound under the
// current settings (Feedback time while the loop is tuned to a pitch, the
// filter's Drive on the clean circuit, anything in the loop while Feedback is
// at zero) and its tooltip says why. It is still usable, so a value can be
// set up before it matters.
//
// A dim is a claim about the audio, so it has to be true, and it is tested
// that way: crate-style, test_core.cpp takes patches, and for every control
// this file calls idle, moves it end to end and requires the output to be
// bit-identical. A rule that dims something audible fails the build.
//
// Modulation is taken into account: a control whose base value makes a
// section inactive (Feedback at 0, Crush at 0) does not idle that section if
// the matrix is moving it, because the modulated value is what the DSP uses.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include "ParamTable.h"

namespace fracture {

struct Idle { int param; std::string why; };

inline std::vector<Idle> idleControls(const float* v){
    const Params& P = Params::get();
    const Ids& id = Ids::get();
    std::vector<Idle> out;
    auto idle = [&](int p, const std::string& why){ if (p >= 0) out.push_back({ p, why }); };
    auto idleId = [&](const std::string& s, const std::string& why){ idle(P.index(s), why); };

    // is anything in the matrix moving this parameter, or using this source?
    auto slotLive = [&](int k){
        return static_cast<int>(v[id.slotSrc[k]]) > 0 && static_cast<int>(v[id.slotDst[k]]) > 0
            && v[id.slotAmt[k]] != 0.0f;
    };
    auto targeted = [&](int param){
        for (int k = 0; k < numSlots; ++k)
            if (slotLive(k) && P.dests()[static_cast<size_t>(static_cast<int>(v[id.slotDst[k]]) - 1)] == param)
                return true;
        return false;
    };
    auto sourceUsed = [&](std::initializer_list<int> srcs){
        for (int k = 0; k < numSlots; ++k)
            if (slotLive(k))
                for (int s : srcs) if (static_cast<int>(v[id.slotSrc[k]]) == s) return true;
        return false;
    };
    auto atZero = [&](int param){ return v[param] == 0.0f && !targeted(param); };

    // ---- the split
    const int nb = static_cast<int>(v[id.bands]) + 1;
    if (nb < 2) idle(id.x1, "Only one band, so there is no split");
    if (nb < 3) idle(id.x2, "Only used with three bands");

    // ---- each band's second stage, and a band that is fully dry
    for (int b = 0; b < numBands; ++b){
        if (v[id.bandStageB[b]] < 0.5f){
            idle(id.bandModeB[b], "Stage B is off");
            idle(id.bandDriveB[b], "Stage B is off");
        }
    }

    // ---- crush
    if (atZero(id.crMix)){
        idle(id.bits, "Crush is at 0%");
        idle(id.redux, "Crush is at 0%");
    }

    // ---- the feedback loop
    const int fbMode = static_cast<int>(v[id.fbMode]);
    if (atZero(id.fbAmt)){
        for (int p : { id.fbTime, id.fbTone, id.fbMode, id.fbNote, id.fbDiv, id.fbThru })
            idle(p, "Feedback is at 0%");
    } else {
        if (fbMode != 0) idle(id.fbTime, "FB mode is not Time");
        if (fbMode != 1) idle(id.fbNote, "FB mode is not Pitch");
        if (fbMode != 2) idle(id.fbDiv, "FB mode is not Sync");
    }

    // ---- the post filter and its rhythm
    const int ft = static_cast<int>(v[id.fltType]);
    const bool filterOff = ft == 0;
    const bool filterOut = atZero(id.fltMix);
    const bool rhythmOff = atZero(id.rhDepth);
    std::vector<int> rhythm = { id.rhDiv, id.rhRate, id.rhShape, id.rhGroove, id.rhPhase, id.rhGlide };
    for (int k = 0; k < 8; ++k) rhythm.push_back(id.rhStep[k]);
    if (filterOff || filterOut){
        const std::string why = filterOff ? "Filter type is Off" : "Filter mix is at 0%";
        for (int p : { id.fltFreq, id.fltQ, id.fltCirc, id.fltPoles, id.fltDrive, id.fltDrift, id.rhDepth })
            idle(p, why);
        // Filter mix at 0 stays live itself: it is the control that brings the filter back
        if (filterOff) idle(id.fltMix, why);
        for (int p : rhythm) idle(p, why);
    } else {
        if (static_cast<int>(v[id.fltCirc]) == 0){
            idle(id.fltDrive, "Drive is part of the Analogue and Vintage circuits");
            idle(id.fltDrift, "Drift is part of the Analogue and Vintage circuits");
        }
        if (rhythmOff){
            for (int p : rhythm) idle(p, "Rhythm mod is off");
        } else {
            if (static_cast<int>(v[id.rhDiv]) != 0) idle(id.rhRate, "Rhythm follows a division, not Rate");
            if (static_cast<int>(v[id.rhShape]) != 5)
                for (int k = 0; k < 8; ++k) idle(id.rhStep[k], "Rhythm shape is not Steps");
        }
    }

    // ---- modulation sources nothing is listening to
    for (int i = 0; i < 2; ++i){
        const bool used = sourceUsed({ i + 1 });
        const int rate = i ? id.l2Rate : id.l1Rate, div = i ? id.l2Div : id.l1Div;
        const int shape = i ? id.l2Shape : id.l1Shape, depth = i ? id.l2Depth : id.l1Depth;
        const std::string name = "LFO " + std::to_string(i + 1);
        if (!used){
            for (int p : { rate, div, shape, depth }) idle(p, "Nothing in the matrix uses " + name);
        } else if (static_cast<int>(v[div]) != 0){
            idle(rate, name + " follows its sync division, not Rate");
        }
    }
    {   // the performance controls do nothing until a slot routes them
        const int perf[4] = { id.mc1, id.mc2, id.xyX, id.xyY };
        const char* names[4] = { "Macro 1", "Macro 2", "the pad's X", "the pad's Y" };
        for (int i = 0; i < 4; ++i)
            if (!sourceUsed({ 6 + i }))
                idle(perf[i], std::string("Nothing in the matrix uses ") + names[i]
                              + ": pick it as a source in a slot");
    }
    if (!sourceUsed({ 3, 4 }))
        for (int p : { id.envAtk, id.envRel, id.envSens }) idle(p, "Nothing in the matrix uses the envelope");

    // ---- the tremolo: off means silent as a source too
    if (v[id.trOn] < 0.5f){
        for (int p : { id.trDiv, id.trRate, id.trDepth, id.trShape, id.trEdge, id.trDuty, id.trSpread })
            idle(p, "Tremolo is off");
    } else if (static_cast<int>(v[id.trDiv]) != 0){
        idle(id.trRate, "Tremolo follows its sync division, not Rate");
    }

    // ---- matrix slots with nothing on one end
    for (int k = 0; k < numSlots; ++k){
        const bool noSrc = static_cast<int>(v[id.slotSrc[k]]) == 0;
        const bool noDst = static_cast<int>(v[id.slotDst[k]]) == 0;
        if (noSrc && noDst) idle(id.slotAmt[k], "Pick a source and a target");
        else if (noSrc){ idle(id.slotAmt[k], "Pick a source"); idle(id.slotDst[k], "Pick a source"); }
        else if (noDst){ idle(id.slotAmt[k], "Pick a target"); idle(id.slotSrc[k], "Pick a target"); }
        else if (v[id.slotAmt[k]] == 0.0f){
            idle(id.slotSrc[k], "Amount is 0%"); idle(id.slotDst[k], "Amount is 0%");
        }
    }

    // ---- nothing wet at all
    if (atZero(id.mix)){
        std::vector<Idle> all;
        for (int p = 0; p < P.count(); ++p){
            const std::string& s = P[p].id;
            if (s == "inGain" || s == "mix" || s == "width" || s == "outGain" || s == "safety"
                || s == "osFactor" || s.rfind("tr", 0) == 0) continue;
            all.push_back({ p, "Dry/wet is at 0%, so only the dry signal is heard" });
        }
        return all;
    }
    return out;
}

} // namespace fracture
