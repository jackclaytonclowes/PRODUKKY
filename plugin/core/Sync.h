// Sync.h — the host's clock, and the note divisions that read from it.
//
// One table serves the two LFOs and the tremolo, and its first entry is "Free"
// rather than a separate sync switch. That is deliberate: a toggle plus a
// division is two controls that can disagree with each other, and the panel
// then has to explain which one is winning. A single list cannot.
//
// Divisions are in beats, so a quarter note is 1 and a bar is 4. A host in 7/8
// therefore gets bars that do not line up with its bars; the plugin is told
// the time signature but deliberately ignores it, because "1 bar" meaning
// "four beats" is the behaviour every other plugin on the track has.
#pragma once
#include <cmath>

namespace fracture {

struct Transport {
    double bpm = 120.0;
    double ppq = 0.0;             // quarter notes since the start of the timeline
    bool playing = false;
    bool valid = false;           // false when the host offers no position at all
};

inline const char* const divIds[] = {
    "free", "8b", "4b", "2b", "1b", "1/2", "1/4d", "1/4", "1/4t",
    "1/8d", "1/8", "1/8t", "1/16", "1/16t", "1/32"
};
inline const char* const divNames[] = {
    "Free", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4 dot", "1/4", "1/4 trip",
    "1/8 dot", "1/8", "1/8 trip", "1/16", "1/16 trip", "1/32"
};
inline const double divBeats[] = {
    0.0, 32.0, 16.0, 8.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0,
    0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125
};
inline constexpr int numDivs = 15;

// The cycle length a division asks for, in beats — 0 means free-running.
inline double beatsForDiv(int choice){
    return (choice > 0 && choice < numDivs) ? divBeats[choice] : 0.0;
}

// The same division as a frequency, for when the transport is not running: a
// synced LFO should keep moving at the right speed while the tape is stopped,
// otherwise the plugin looks broken until you press play.
inline double hzForDiv(int choice, double bpm){
    const double beats = beatsForDiv(choice);
    if (beats <= 0.0) return 0.0;
    return (bpm > 1.0 ? bpm : 120.0) / 60.0 / beats;
}

} // namespace fracture
