// ParamTable.h — one parameter table, shared by the DSP and the plugin wrapper,
// so a new control is added in one place and the host, the editor and the
// engine all see it.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

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
        // the converter
        f("tune",    "Tune",       -12, 12, 0, false, "st");
        f("clock",   "Clock",      8000, 48000, 26040, true, "Hz");
        f("bits",    "Bits",       4, 16, 12, false);
        f("compand", "Compand",    0, 100, 60, false, "%");
        f("aa",      "Anti-alias", 0, 100, 25, false, "%");
        // the four-pole
        f("fltFreq",  "Cutoff", 200, 18000, 18000, true, "Hz");
        f("fltReso",  "Reso",   0, 100, 10, false, "%");
        f("fltDrive", "Drive",  1, 8, 1, false);
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
    }
    std::vector<ParamInfo> info_;
};

struct Ids {
    static const Ids& get(){ static Ids i; return i; }
    int inGain, mono, tune, clock, bits, compand, aa;
    int fltFreq, fltReso, fltDrive, swing, grid, push, dust, dustTone, mix, outGain, safety;
private:
    Ids(){
        const Params& p = Params::get();
        auto I = [&](const char* s){ return p.index(s); };
        inGain = I("inGain"); mono = I("mono"); tune = I("tune"); clock = I("clock");
        bits = I("bits"); compand = I("compand"); aa = I("aa");
        fltFreq = I("fltFreq"); fltReso = I("fltReso"); fltDrive = I("fltDrive");
        swing = I("swing"); grid = I("grid"); push = I("push");
        dust = I("dust"); dustTone = I("dustTone");
        mix = I("mix"); outGain = I("outGain"); safety = I("safety");
    }
};

} // namespace crate
