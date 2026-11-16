// ParamTable.h — one parameter table, shared by the DSP and the plugin wrapper,
// so a new control is added in one place and the host, the editor and the
// engine all see it.
//
// Parameters are appended, never reordered or renamed: hosts save sessions and
// automation by id, and A/B slots and your presets store ids too.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace stoop {

enum class Kind { Float, Choice, Bool };

struct ParamInfo {
    std::string id, name;
    Kind kind = Kind::Float;
    float min = 0, max = 1, def = 0;
    bool log = false;
    std::string unit;
    std::vector<std::string> choices;
};

// the turntable's speeds, in revolutions a minute, in the order of "speed"
inline constexpr double speedRpm[] = { 100.0 / 3.0, 45.0, 78.0 };

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
        // the pitch shifter: the original's three knobs, Pitch over two octaves
        // either way, Feedback and Balance. An octave down by default, because
        // a pitch shifter at 0 does nothing a pitch shifter is for
        b("pitchOn",  "Pitch on", false);
        f("pitch",    "Pitch",    -24, 24, -12, false, "st");
        f("pitchFb",  "Feedback", 0, 100, 0, false, "%");
        f("pitchBal", "Balance",  0, 100, 100, false, "%");
        // filter + drive: the first of the original's effect buttons
        b("fltOn",    "Filter on", false);
        c("fltType",  "Type", { "LP", "BP", "HP" }, 0);
        f("fltFreq",  "Cutoff", 20, 20000, 2000, true, "Hz");
        f("fltReso",  "Resonance", 0, 100, 20, false, "%");
        f("fltDrive", "Drive", 0, 100, 0, false, "%");
        // lo-fi: a slower sample clock with no filter before it, and fewer bits
        b("lofiOn",   "Lo-fi on", false);
        f("lofiRate", "Rate", 1000, 48000, 12000, true, "Hz");
        f("lofiBits", "Bits", 2, 16, 10, false);
        // the vinyl simulator, with the knob the original had and the SX did
        // not: Comp. On by default, because it is what this box is for
        b("vinylOn",  "Vinyl on", true);
        f("comp",     "Comp", 0, 100, 40, false, "%");
        f("noise",    "Noise", 0, 100, 20, false, "%");
        f("wow",      "Wow flutter", 0, 100, 25, false, "%");
        c("speed",    "Speed", { "33 rpm", "45 rpm", "78 rpm" }, 0);
        // out
        f("mix",     "Mix",    0, 100, 100, false, "%");
        f("outGain", "Output", -24, 12, 0, false, "dB");
        b("safety",  "Safety clip", true);
    }
    std::vector<ParamInfo> info_;
};

struct Ids {
    static const Ids& get(){ static Ids i; return i; }
    int inGain, mono;
    int pitchOn, pitch, pitchFb, pitchBal;
    int fltOn, fltType, fltFreq, fltReso, fltDrive;
    int lofiOn, lofiRate, lofiBits;
    int vinylOn, comp, noise, wow, speed;
    int mix, outGain, safety;
private:
    Ids(){
        const Params& p = Params::get();
        auto I = [&](const char* s){ return p.index(s); };
        inGain = I("inGain"); mono = I("mono");
        pitchOn = I("pitchOn"); pitch = I("pitch"); pitchFb = I("pitchFb"); pitchBal = I("pitchBal");
        fltOn = I("fltOn"); fltType = I("fltType"); fltFreq = I("fltFreq"); fltReso = I("fltReso"); fltDrive = I("fltDrive");
        lofiOn = I("lofiOn"); lofiRate = I("lofiRate"); lofiBits = I("lofiBits");
        vinylOn = I("vinylOn"); comp = I("comp"); noise = I("noise"); wow = I("wow"); speed = I("speed");
        mix = I("mix"); outGain = I("outGain"); safety = I("safety");
    }
};

} // namespace stoop
