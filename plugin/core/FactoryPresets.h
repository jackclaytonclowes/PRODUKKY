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
,
        // ---- the second set: tuned feedback on a kick and a snare, synced
        // echoes through the drive, a trance gate, an acid line, a slow stereo notch
        { "Parallel crunch — half a driven ladder",
          R"JSON({"bands":"1","d0a":3,"m0a":"tube","fltType":"lp","fltCirc":"analog","fltFreq":900,"fltQ":6,"fltDrive":8,"fltMix":50})JSON" },
        { "Kick tuned to the key — C1 resonator",
          R"JSON({"bands":"2","x1":150,"d0a":2,"m0a":"soft","d1a":1,"mx1":0,"fbAmt":45,"fbMode":"pitch","fbNote":24,"fbTone":400})JSON" },
        { "Snare ring — tuned to G4",
          R"JSON({"bands":"1","d0a":2,"m0a":"soft","fbAmt":50,"fbMode":"pitch","fbNote":67,"fbTone":12000,"mix":50})JSON" },
        { "Quarter-note echo into the fold",
          R"JSON({"bands":"1","d0a":4,"m0a":"fold","fbAmt":45,"fbMode":"sync","fbDiv":"1/4","fbThru":true,"fbTone":3000,"outGain":-5})JSON" },
        { "Trance gate — sixteenth steps at 48 dB",
          R"JSON({"bands":"1","d0a":2,"m0a":"soft","fltType":"lp","fltPoles":"48","fltFreq":150,"rhShape":"steps","rhDiv":"1/16","rhDepth":6,"rhGlide":4,"rhStep1":100,"rhStep2":0,"rhStep3":100,"rhStep4":0,"rhStep5":100,"rhStep6":100,"rhStep7":0,"rhStep8":100})JSON" },
        { "Acid line — resonant ladder on steps",
          R"JSON({"bands":"1","d0a":4,"m0a":"tube","fltType":"lp","fltCirc":"analog","fltFreq":250,"fltQ":10,"fltDrive":3,"rhShape":"steps","rhDiv":"1/16","rhDepth":3.5,"rhGlide":30,"rhStep1":100,"rhStep2":20,"rhStep3":60,"rhStep4":0,"rhStep5":80,"rhStep6":40,"rhStep7":100,"rhStep8":10})JSON" },
        { "Notch drift — slow, opposite sides",
          R"JSON({"bands":"1","d0a":2,"m0a":"warm","fltType":"notch","fltPoles":"36","fltFreq":400,"fltQ":1.2,"rhShape":"sin","rhDiv":"2b","rhDepth":3,"rhPhase":180})JSON" },
        { "Stutter — 1/32 echoes through the drive",
          R"JSON({"bands":"1","d0a":5,"m0a":"bits","fbAmt":60,"fbMode":"sync","fbDiv":"1/32","fbThru":true,"fbTone":6000,"mix":70,"outGain":-5})JSON" },
        { "Comb on the fifth — rings on G3, half wet",
          R"JSON({"bands":"1","d0a":3,"m0a":"tape","fbAmt":65,"fbMode":"pitch","fbNote":55,"fbTone":7000,"fltType":"hp","fltFreq":120,"mix":55,"outGain":3})JSON" }
,
        // ---- performance: the pad and the macros are routed, and the preset is
        // saved at the middle of their travel. Levelled at every corner of the
        // pad and both ends of each macro, not only where it is saved
        { "XY — drive across, cutoff up",
          R"JSON({"bands":"1","d0a":2,"m0a":"tube","fltType":"lp","fltCirc":"analog","fltFreq":400,"fltQ":4,"mS0":"xyx","mD0":"d0a","mA0":60,"mS1":"xyy","mD1":"fltFreq","mA1":70,"xyX":50,"xyY":50})JSON" },
        { "XY — the loop's pitch across, feedback up",
          R"JSON({"bands":"1","d0a":3,"m0a":"soft","fbAmt":20,"fbMode":"pitch","fbNote":36,"fbTone":8000,"mS0":"xyx","mD0":"fbNote","mA0":60,"mS1":"xyy","mD1":"fbAmt","mA1":35,"outGain":-4,"xyX":50,"xyY":50})JSON" },
        { "Macros — 1 folds and crushes, 2 widens and repeats",
          R"JSON({"bands":"2","x1":300,"d0a":2,"d1a":2,"m1a":"fold","mS0":"mc1","mD0":"d1a","mA0":70,"mS1":"mc1","mD1":"crMix","mA1":80,"mS2":"mc2","mD2":"width","mA2":50,"mS3":"mc2","mD3":"fbAmt","mA3":50,"bits":6,"redux":4,"fbTime":80,"mc1":50,"mc2":50})JSON" }
    };
    return all;
}

// the whole menu, in order: the browser's presets, then the plugin's own. A
// preset added to the browser file moves the plugin-only ones down the list;
// that is harmless, because a host saves the plugin's whole state with a
// session, not the number of the preset it started from
inline const std::vector<Preset>& factoryPresets(){
    static const std::vector<Preset> all = []{
        std::vector<Preset> v = presets();
        for (const auto& p : pluginOnlyPresets()) v.push_back(p);
        return v;
    }();
    return all;
}

} // namespace fracture
