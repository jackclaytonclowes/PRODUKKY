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
        // what the plugin defaulted to before it was corrected to linear: kept,
        // because it is a sound, but named for what it is. (As "Twelve bit,
        // straight" it had become identical to Init once linear was the default,
        // which the audition renders showed.)
        { "Twelve bit, companded", {
            { "compand", 60 }, { "aa", 25 } } },
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
        // outputs 1 and 2: the SSM2044 opened by each hit and shut again within
        // milliseconds, which is the dark, thumping kick with a bright attack
        { "SP, outputs 1-2", {
            { "machine", 0 }, { "fltFreq", 260 }, { "fltReso", 15 }, { "fltEnv", 5 },
            { "fltDecay", 25 }, { "fltDrive", 2 } } },
        // the same envelope, slower and higher, for a sampled bass line
        { "Filtered bass line", {
            { "machine", 0 }, { "trick", 5 }, { "fltFreq", 180 }, { "fltReso", 30 },
            { "fltEnv", 3 }, { "fltDecay", 120 }, { "mono", 1 } } },
        // the rack sampler at its top rate: twelve bits and a steep band limit
        { "S900, forty kilohertz", {
            { "machine", 1 }, { "clock", 40000 }, { "aa", 25 }, { "fltReso", 0 } } },
        // the S950 trick for lifting a bass line out of a loop: a low rate and
        // the four-pole shut right down on top of the six-pole
        { "S950, bass lifted out", {
            { "machine", 1 }, { "clock", 16000 }, { "fltFreq", 350 }, { "fltReso", 0 },
            { "fltDrive", 2 }, { "mono", 1 } } },
        // the rhythm section. Shapes, divisions and poles are written as the
        // index into their lists: shape LP 0, BP 1, HP 2, BR 3; poles 2/4/6/8
        // are 0..3; rhShape Sine 0 .. Steps 5, Random 6; rhDiv 1 bar 3, 1/4 6,
        // 1/8 9, 1/16 11
        { "Rhythm: gated sixteenths", {
            { "fltShape", 0 }, { "fltPoles", 1 }, { "fltFreq", 250 }, { "fltReso", 30 },
            { "rhShape", 5 }, { "rhDiv", 11 }, { "rhDepth", 5 }, { "rhGlide", 5 }, { "rhGroove", 58 } } },
        { "Rhythm: swung band pass", {
            { "fltShape", 1 }, { "fltPoles", 1 }, { "fltFreq", 700 }, { "fltReso", 45 },
            { "rhShape", 0 }, { "rhDiv", 6 }, { "rhDepth", 3 }, { "rhGroove", 62 },
            // a band pass on drums throws most of the kick away: the audition
            // renders measured this 14.5 dB under the dry loop without it
            { "outGain", 10 } } },
        { "Rhythm: notch through the bar", {
            { "fltShape", 3 }, { "fltPoles", 2 }, { "fltFreq", 400 },
            { "rhShape", 1 }, { "rhDiv", 3 }, { "rhDepth", 4 }, { "rhPhase", 90 } } },
        { "Rhythm: random steps", {
            { "fltShape", 0 }, { "fltPoles", 0 }, { "fltFreq", 600 }, { "fltReso", 60 },
            { "rhShape", 6 }, { "rhDiv", 11 }, { "rhDepth", 3 }, { "rhGlide", 20 } } },
        { "Rhythm: eight-pole bar sweep", {
            { "fltShape", 0 }, { "fltPoles", 3 }, { "fltFreq", 200 },
            { "rhShape", 2 }, { "rhDiv", 3 }, { "rhDepth", 6 }, { "fltMix", 80 } } },
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
            { "mix", 45 }, { "compand", 50 }, { "aa", 60 }, { "dust", 15 } } },
        // ---- starting points for a whole beat, added last so saved program
        // numbers keep pointing where they did. Levelled on the audition loop.
        // the rack sampler at a low rate across the drum bus, a little dust, a little swing
        { "Lo-fi bus, S900 at 22 kHz", {
            { "machine", 1 }, { "clock", 22050 }, { "dust", 25 }, { "dustTone", 3000 },
            { "fltFreq", 9000 }, { "swing", 56 }, { "mix", 80 } } },
        // a break sampled sped up off a dusty record, out of the 7.5 kHz outputs
        { "Chopped soul break", {
            { "machine", 0 }, { "trick", 5 }, { "fltFreq", 7500 }, { "fltReso", 0 },
            { "dust", 30 }, { "dustTone", 2600 }, { "swing", 58 } } },
        // heavy swing and everything a little late
        { "Drunk sixteenths", {
            { "swing", 64 }, { "push", 10 }, { "tune", -2 } } },
        // the filter opens on every beat and closes before the next: a saw
        // down on 1/4, four octaves up from 400 Hz
        { "Quarter-note filter pump", {
            { "fltFreq", 400 }, { "fltReso", 10 }, { "rhShape", 3 }, { "rhDiv", 6 }, { "rhDepth", 4 } } },
        // as rough as the box goes while still being drums
        { "Eight bits, sped up seven", {
            { "bits", 8 }, { "trick", 7 }, { "aa", 5 }, { "mono", 1 } } },
        // 0.2: Keep sub. The whole kit through eight companded bits, except
        // the kick's body and the bass below 110 Hz, which stay clean and full
        { "Crushed top, clean sub", {
            { "bits", 8 }, { "compand", 40 }, { "aa", 15 }, { "subHz", 110 } } }
    };
    return all;
}

} // namespace crate
