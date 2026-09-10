// Presets.h — starting points, written as parameter values rather than as a
// binary blob so they can be read and argued with.
#pragma once
#include <vector>
#include <utility>
#include <string>

namespace crate {

struct Preset {
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<Preset>& presets(){
    static const std::vector<Preset> all = {
        { "Init", {} },
        { "Twelve bit, straight", {
            { "compand", 60 }, { "aa", 25 }, { "mix", 100 } } },
        { "Pitched down for grit", {
            { "tune", -5 }, { "aa", 10 }, { "compand", 75 }, { "fltFreq", 9000 } } },
        { "Dusty break", {
            { "tune", -3 }, { "dust", 45 }, { "dustTone", 2800 },
            { "fltFreq", 7500 }, { "compand", 70 }, { "inGain", 2 } } },
        { "Off the grid", {
            { "swing", 62 }, { "push", 6 }, { "tune", -2 }, { "compand", 65 } } },
        { "Behind the beat", {
            { "swing", 58 }, { "push", 14 }, { "fltFreq", 6000 }, { "dust", 25 } } },
        { "Thick kick", {
            { "inGain", 5 }, { "tune", -7 }, { "fltFreq", 1200 }, { "fltReso", 25 },
            { "fltDrive", 3 }, { "mono", 1 } } },
        { "Broken telephone", {
            { "bits", 6 }, { "tune", -9 }, { "aa", 0 }, { "compand", 100 },
            { "fltFreq", 3000 }, { "fltReso", 40 }, { "mono", 1 } } },
        { "Whole bus, gently", {
            { "mix", 45 }, { "compand", 50 }, { "aa", 60 }, { "dust", 15 } } }
    };
    return all;
}

} // namespace crate
