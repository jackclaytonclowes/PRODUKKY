// Presets.h — starting points, written as parameter values rather than as a
// binary blob so they can be read and argued with.
//
// Every one is levelled on the audition loop (drums, bass and a chord) to
// within 3 dB of the dry loop's loudness, K-weighted, and kept off the
// ceiling; stoop/tests/test_core.cpp fails if one drifts, or if two become the
// same sound. New presets are appended, so a saved program number keeps
// pointing where it did.
//
// Choices are written as their index: fltType LP 0, BP 1, HP 2; speed 33 0,
// 45 1, 78 2. Switches are 0 or 1.
#pragma once
#include <vector>
#include <utility>
#include <string>

namespace stoop {

struct Preset {
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<Preset>& presets(){
    static const std::vector<Preset> all = {
        // the defaults: the vinyl sim on, a little of each of its three knobs
        { "Init", {} },
        // ---- the vinyl sim
        // the three knobs the original had, about half way, as a starting point
        { "Vinyl sim, the original", {
            { "comp", 60 }, { "noise", 35 }, { "wow", 35 } } },
        // the knob the SX took away, on its own: no noise, no wow
        { "Comp only", {
            { "comp", 75 }, { "noise", 0 }, { "wow", 0 }, { "outGain", -1 } } },
        { "Full pressure", {
            { "comp", 100 }, { "noise", 15 }, { "wow", 15 }, { "outGain", -1 } } },
        // a single played to death: faster wow, more crackle
        { "Worn 45", {
            { "speed", 1 }, { "comp", 50 }, { "noise", 60 }, { "wow", 55 } } },
        // shellac: mono, loud surface, a narrow band
        { "Shellac 78", {
            { "speed", 2 }, { "comp", 70 }, { "noise", 80 }, { "wow", 30 }, { "mono", 1 },
            { "fltOn", 1 }, { "fltFreq", 4500 }, { "fltReso", 10 }, { "outGain", -1 } } },
        { "Warped", {
            { "comp", 40 }, { "noise", 25 }, { "wow", 100 } } },
        { "Dust and nothing else", {
            { "comp", 0 }, { "noise", 70 }, { "wow", 0 } } },
        // ---- the pitch shifter
        { "Down an octave", {
            // an octave down puts the bass under what the ear weighs: brought back up
            { "pitchOn", 1 }, { "pitch", -12 }, { "comp", 40 }, { "outGain", 3.5 } } },
        { "Fifth up, doubled", {
            { "pitchOn", 1 }, { "pitch", 7 }, { "pitchBal", 50 }, { "outGain", 1.5 } } },
        // feedback shifts the shifted signal again: a fifth, a ninth, ...
        { "Climbing fifths", {
            { "pitchOn", 1 }, { "pitch", 7 }, { "pitchFb", 75 }, { "pitchBal", 60 } } },
        { "Chipmunk soul", {
            { "pitchOn", 1 }, { "pitch", 12 }, { "comp", 50 }, { "noise", 25 } } },
        // ---- filter and drive
        { "Through the wall", {
            { "fltOn", 1 }, { "fltFreq", 450 }, { "fltReso", 15 }, { "comp", 70 }, { "noise", 20 }, { "outGain", -1 } } },
        // a band pass throws away the kick and the bass, which is most of the
        // loudness: the drive and Output bring it back to the others
        { "Radio in the kitchen", {
            { "fltOn", 1 }, { "fltType", 1 }, { "fltFreq", 1500 }, { "fltReso", 30 }, { "fltDrive", 80 },
            { "lofiOn", 1 }, { "lofiRate", 11025 }, { "lofiBits", 9 }, { "outGain", 8 } } },
        { "Driven low pass", {
            { "fltOn", 1 }, { "fltFreq", 2500 }, { "fltReso", 55 }, { "fltDrive", 60 } } },
        { "Thin and driven", {
            { "fltOn", 1 }, { "fltType", 2 }, { "fltFreq", 700 }, { "fltDrive", 70 }, { "outGain", 3 } } },
        // ---- lo-fi
        { "Eight-bit sampler", {
            { "lofiOn", 1 }, { "lofiRate", 11025 }, { "lofiBits", 8 }, { "comp", 30 } } },
        { "Crushed to four bits", {
            { "lofiOn", 1 }, { "lofiRate", 6000 }, { "lofiBits", 4 }, { "mix", 60 } } },
        // ---- a whole loop, the way it went through the box: resampled
        { "Resampled twice", {
            { "lofiOn", 1 }, { "lofiRate", 22050 }, { "lofiBits", 12 },
            { "comp", 70 }, { "noise", 30 }, { "wow", 30 }, { "outGain", -1 } } },
        { "Slowed and dusty", {
            { "pitchOn", 1 }, { "pitch", -3 }, { "fltOn", 1 }, { "fltFreq", 7000 },
            { "comp", 60 }, { "noise", 40 }, { "wow", 45 } } },
        { "Late-night loop", {
            { "fltOn", 1 }, { "fltFreq", 3000 }, { "fltReso", 10 },
            { "comp", 80 }, { "noise", 30 }, { "wow", 30 }, { "outGain", -1 } } },
        { "Half the box", {
            { "mix", 50 }, { "comp", 60 }, { "noise", 30 }, { "wow", 30 },
            { "lofiOn", 1 }, { "lofiBits", 10 } } },
    };
    return all;
}

// The heading each preset sits under in the menu (Session.h's PresetMenu).
// Every factory preset is in this table, which the tests check, so a new one
// cannot arrive in the menu without a heading.
inline const char* presetCategory(const std::string& name){
    static const std::pair<const char*, const char*> table[] = {
        { "Init", "Start" },
        { "Vinyl sim, the original", "Vinyl sim" },
        { "Comp only", "Vinyl sim" },
        { "Full pressure", "Vinyl sim" },
        { "Worn 45", "Vinyl sim" },
        { "Shellac 78", "Vinyl sim" },
        { "Warped", "Vinyl sim" },
        { "Dust and nothing else", "Vinyl sim" },
        { "Down an octave", "Pitch" },
        { "Fifth up, doubled", "Pitch" },
        { "Climbing fifths", "Pitch" },
        { "Chipmunk soul", "Pitch" },
        { "Through the wall", "Filter and drive" },
        { "Radio in the kitchen", "Filter and drive" },
        { "Driven low pass", "Filter and drive" },
        { "Thin and driven", "Filter and drive" },
        { "Eight-bit sampler", "Lo-fi" },
        { "Crushed to four bits", "Lo-fi" },
        { "Resampled twice", "Whole loop" },
        { "Slowed and dusty", "Whole loop" },
        { "Late-night loop", "Whole loop" },
        { "Half the box", "Whole loop" },
    };
    for (const auto& [n, c] : table) if (name == n) return c;
    return "";
}

} // namespace stoop
