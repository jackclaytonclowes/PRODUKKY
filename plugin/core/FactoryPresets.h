// FactoryPresets.h — the plugin's preset menu: the browser version's presets
// (Presets.h, generated from fx/fracture.html and never edited by hand), then
// the ones below, which use what only the plugin has — tuned and synced
// feedback, feedback through the drive, the filter rhythm, 36 and 48 dB
// slopes and the filter's own mix — and so cannot live in the browser file.
//
// They are written in the same patch JSON and loaded through the same import
// as everything else, so a patch copied out of the plugin reads the same way.
// Each was levelled on the audition loop (tools/audition) to within a few dB
// of the dry signal; plugin/tests/test_core.cpp holds every preset in this
// list to that, so a preset cannot quietly arrive 25 dB down again.
#pragma once
#include <vector>
#include "Presets.h"

namespace fracture {

inline const std::vector<Preset>& pluginOnlyPresets(){
    static const std::vector<Preset> all = {
        { "Tuned comb — rings on A2",
          R"JSON({"bands":"1","d0a":3,"m0a":"soft","fbAmt":72,"fbMode":"pitch","fbNote":45,"fbTone":9000,"mix":70})JSON" },
        { "Resonator — C2 through the drive",
          R"JSON({"bands":"1","d0a":5,"m0a":"tube","fbAmt":60,"fbMode":"pitch","fbNote":36,"fbThru":true,"fbTone":5000,"outGain":-7})JSON" },
        { "Growl — the loop through a fold",
          R"JSON({"bands":"2","x1":300,"d1a":6,"m1a":"fold","fbAmt":55,"fbMode":"pitch","fbNote":40,"fbThru":true,"fltType":"lp","fltCirc":"analog","fltFreq":3500,"outGain":-6})JSON" },
        { "Dub echo, dotted eighth",
          R"JSON({"bands":"1","d0a":2,"m0a":"soft","fbAmt":55,"fbMode":"sync","fbDiv":"1/8d","fbTone":2200,"preHP":150,"mix":60,"outGain":6})JSON" },
        { "Tape slap, synced sixteenth",
          R"JSON({"bands":"1","d0a":4,"m0a":"tape","fbAmt":30,"fbMode":"sync","fbDiv":"1/16","fbTone":4000})JSON" },
        { "Gated sixteenths on the ladder",
          R"JSON({"bands":"1","d0a":3,"m0a":"tube","fltType":"lp","fltCirc":"analog","fltPoles":"24","fltFreq":300,"fltQ":5,"rhDepth":4,"rhDiv":"1/16","rhShape":"steps","rhGlide":8})JSON" },
        { "Wah on the quarter note",
          R"JSON({"bands":"1","d0a":5,"m0a":"tube","fltType":"bp","fltCirc":"vintage","fltFreq":500,"fltQ":4,"rhDepth":2.5,"rhDiv":"1/4","rhShape":"sin","fltMix":85,"outGain":8})JSON" },
        { "Cliff — 48 dB sweep across the bar",
          R"JSON({"bands":"1","d0a":3,"m0a":"soft","fltType":"lp","fltPoles":"48","fltFreq":180,"rhDepth":6,"rhDiv":"1b","rhShape":"saw"})JSON" },
        { "Swung notch, stereo",
          R"JSON({"bands":"1","d0a":3,"m0a":"warm","fltType":"notch","fltPoles":"36","fltFreq":600,"fltQ":1.5,"rhDepth":3,"rhDiv":"1/8","rhShape":"tri","rhGroove":62,"rhPhase":90})JSON" },
        { "Random steps, half wet",
          R"JSON({"bands":"1","d0a":4,"m0a":"tape","fltType":"bp","fltCirc":"analog","fltFreq":900,"fltQ":3,"rhShape":"rnd","rhDiv":"1/16","rhDepth":2.5,"rhGlide":25,"fltMix":50})JSON" }
    };
    return all;
}

// the whole menu, in order: the browser's first, so saved program numbers keep
// pointing at the presets they always did
inline const std::vector<Preset>& factoryPresets(){
    static const std::vector<Preset> all = []{
        std::vector<Preset> v = presets();
        for (const auto& p : pluginOnlyPresets()) v.push_back(p);
        return v;
    }();
    return all;
}

} // namespace fracture
