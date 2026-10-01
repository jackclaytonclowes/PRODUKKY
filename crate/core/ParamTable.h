// ParamTable.h — one parameter table, shared by the DSP and the plugin wrapper,
// so a new control is added in one place and the host, the editor and the
// engine all see it.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "RhythmMod.h"

namespace crate {

enum class Kind { Float, Choice, Bool };

struct ParamInfo {
    std::string id, name;
    Kind kind = Kind::Float;
    float min = 0, max = 1, def = 0;
    bool log = false;
    std::string unit;
    std::vector<std::string> choices;
};

inline constexpr int gridSteps(int choice){ return choice <= 0 ? 2 : (choice == 1 ? 4 : 8); }

class Params {
public:
    static const Params& get(){ static Params p; return p; }
    const std::vector<ParamInfo>& all() const { return info_; }
    int count() const { return static_cast<int>(info_.size()); }
    const ParamInfo& operator[](int i) const { return info_[static_cast<size_t>(i)]; }
    int index(const std::string& id) const {
        for (size_t i = 0; i < info_.size(); ++i) if (info_[i].id == id) return static_cast<int>(i);
        return -1;
    }
    static float toNorm(const ParamInfo& p, float v){
        v = std::clamp(v, p.min, p.max);
        if (p.log) return static_cast<float>(std::log(v / p.min) / std::log(p.max / p.min));
        return (v - p.min) / (p.max - p.min);
    }
    static float fromNorm(const ParamInfo& p, float n){
        n = std::clamp(n, 0.0f, 1.0f);
        if (p.log) return static_cast<float>(p.min * std::pow(p.max / p.min, n));
        return p.min + (p.max - p.min) * n;
    }

private:
    Params(){
        auto f = [&](std::string id, std::string name, float mn, float mx, float df,
                     bool logTaper, std::string unit = ""){
            ParamInfo p; p.id = std::move(id); p.name = std::move(name);
            p.min = mn; p.max = mx; p.def = df; p.log = logTaper; p.unit = std::move(unit);
            info_.push_back(std::move(p));
        };
        auto c = [&](std::string id, std::string name, std::vector<std::string> choices, int df){
            ParamInfo p; p.id = std::move(id); p.name = std::move(name); p.kind = Kind::Choice;
            p.min = 0; p.max = static_cast<float>(choices.size() - 1); p.def = static_cast<float>(df);
            p.choices = std::move(choices);
            info_.push_back(std::move(p));
        };
        auto b = [&](std::string id, std::string name, bool df){
            ParamInfo p; p.id = std::move(id); p.name = std::move(name); p.kind = Kind::Bool;
            p.min = 0; p.max = 1; p.def = df ? 1.0f : 0.0f;
            info_.push_back(std::move(p));
        };

        f("inGain", "Input", -24, 24, 0, false, "dB");
        b("mono", "Mono", false);
        // the converter. Machine is first because it decides what the rest mean
        c("machine", "Machine", { "SP", "S900" }, 0);
        f("tune",    "Tune",       -12, 12, 0, false, "st");
        f("trick",   "Pitch trick", 0, 12, 0, false, "st");
        f("clock",   "Clock",      8000, 48000, 26040, true, "Hz");
        f("bits",    "Bits",       4, 16, 12, false);
        f("compand", "Compand",    0, 100, 0, false, "%");
        f("aa",      "Anti-alias", 0, 100, 25, false, "%");
        // the four-pole
        f("fltFreq",  "Cutoff", 200, 18000, 18000, true, "Hz");
        f("fltReso",  "Reso",   0, 100, 10, false, "%");
        f("fltDrive", "Drive",  1, 8, 1, false);
        // the envelope on the SP's outputs 1 and 2: each hit opens the filter
        // this many octaves above Cutoff, and it falls back over Decay
        f("fltEnv",   "Env",    0, 6, 0, false, "oct");
        f("fltDecay", "Decay",  5, 500, 40, true, "ms");
        // the FilterFreak half: what comes out of the four-pole, how steep, and
        // how much of it. Four poles of low pass at full mix is what it always was
        c("fltShape", "Shape", { "LP", "BP", "HP", "BR" }, 0);
        c("fltPoles", "Poles", { "2", "4", "6", "8" }, 1);
        f("fltMix",   "Filter mix", 0, 100, 100, false, "%");
        // the rhythm that moves the cutoff. Mod is octaves from Cutoff, either
        // way; at 0 the rhythm runs but moves nothing
        f("rhDepth",  "Mod",    -6, 6, 0, false, "oct");
        { std::vector<std::string> d(rhythmDivNames, rhythmDivNames + rhythmNumDivs);
          c("rhDiv",  "Rhythm", d, 9); }
        f("rhRate",   "Rate",   0.05f, 20, 2, true, "Hz");
        { std::vector<std::string> sh(rhythmShapeNames, rhythmShapeNames + rhythmNumShapes);
          c("rhShape", "Rhythm shape", sh, 0); }
        f("rhGroove", "Groove", 50, 75, 50, false, "%");
        f("rhPhase",  "Phase",  0, 180, 0, false, "deg");
        f("rhGlide",  "Glide",  0, 100, 10, false, "%");
        { const float pattern[8] = { 100, 0, 60, 0, 100, 25, 60, 0 };
          for (int k = 0; k < 8; ++k)
              f("rhStep" + std::to_string(k + 1), "Step " + std::to_string(k + 1), 0, 100, pattern[k], false, "%"); }
        // feel
        f("swing", "Swing", 50, 80, 50, false, "%");
        c("grid",  "Grid", { "1/8", "1/16", "1/32" }, 1);
        f("push",  "Push", -25, 25, 0, false, "ms");
        // the record it came off
        f("dust",     "Dust",      0, 100, 0, false, "%");
        f("dustTone", "Dust tone", 500, 12000, 3500, true, "Hz");
        // out
        f("mix",     "Mix",    0, 100, 100, false, "%");
        f("outGain", "Output", -24, 12, 0, false, "dB");
        b("safety",  "Safety clip", true);
        // 0.2, appended so saved sessions keep their meaning: the low end
        // around the converter and the four-pole. 20 Hz is off
        f("subHz",   "Keep sub", 20, 300, 20, true, "Hz");
    }
    std::vector<ParamInfo> info_;
};

struct Ids {
    static const Ids& get(){ static Ids i; return i; }
    int inGain, mono, machine, tune, trick, clock, bits, compand, aa;
    int fltFreq, fltReso, fltDrive, fltEnv, fltDecay, fltShape, fltPoles, fltMix;
    int rhDepth, rhDiv, rhRate, rhShape, rhGroove, rhPhase, rhGlide, rhStep[8];
    int swing, grid, push, dust, dustTone, mix, outGain, safety, subHz;
private:
    Ids(){
        const Params& p = Params::get();
        auto I = [&](const char* s){ return p.index(s); };
        inGain = I("inGain"); mono = I("mono"); machine = I("machine"); tune = I("tune"); trick = I("trick"); clock = I("clock");
        bits = I("bits"); compand = I("compand"); aa = I("aa");
        fltFreq = I("fltFreq"); fltReso = I("fltReso"); fltDrive = I("fltDrive");
        fltEnv = I("fltEnv"); fltDecay = I("fltDecay");
        fltShape = I("fltShape"); fltPoles = I("fltPoles"); fltMix = I("fltMix");
        rhDepth = I("rhDepth"); rhDiv = I("rhDiv"); rhRate = I("rhRate"); rhShape = I("rhShape");
        rhGroove = I("rhGroove"); rhPhase = I("rhPhase"); rhGlide = I("rhGlide");
        for (int k = 0; k < 8; ++k) rhStep[k] = I(("rhStep" + std::to_string(k + 1)).c_str());
        swing = I("swing"); grid = I("grid"); push = I("push");
        dust = I("dust"); dustTone = I("dustTone");
        mix = I("mix"); outGain = I("outGain"); safety = I("safety");
        subHz = I("subHz");
    }
};

} // namespace crate
