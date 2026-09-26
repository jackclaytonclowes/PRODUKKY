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
            { "compand", 0 }, { "aa", 25 }, { "mix", 100 } } },
        // the record sped up about five semitones going in and tuned back down on
        // the machine: 45 rpm against 33 is a ratio of 1.35, 5.2 semitones
        { "SP, 45 on 33", {
            { "machine", 0 }, { "trick", 5 }, { "aa", 10 }, { "dust", 20 } } },
        // the SP's outputs 3 and 4 had a fixed lowpass around 7.5 kHz; 7 and 8
        // had none at all, and no reconstruction filter either
        { "SP, outputs 3-4", {
            { "machine", 0 }, { "fltFreq", 7500 }, { "fltReso", 0 } } },
        { "SP, outputs 7-8 raw", {
            { "machine", 0 }, { "aa", 0 }, { "fltReso", 0 }, { "inGain", 3 } } },
        // the rack sampler at its top rate: twelve bits and a steep band limit
        { "S900, forty kilohertz", {
            { "machine", 1 }, { "clock", 40000 }, { "aa", 25 }, { "fltReso", 0 } } },
        // the S950 trick for lifting a bass line out of a loop: a low rate and
        // the four-pole shut right down on top of the six-pole
        { "S950, bass lifted out", {
            { "machine", 1 }, { "clock", 16000 }, { "fltFreq", 350 }, { "fltReso", 0 },
            { "fltDrive", 2 }, { "mono", 1 } } },
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
