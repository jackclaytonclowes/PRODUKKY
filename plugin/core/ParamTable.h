// ParamTable.h — one parameter table, shared by the DSP core and the plugin
// wrapper, with the SAME ids as the JavaScript version in fx/fracture.html.
//
// Keeping the ids identical is deliberate: the browser build's patches are
// {id: value} JSON, so anything saved there can be loaded here (and the ranges
// and tapers match, so a patch means the same thing). The JUCE layer builds its
// AudioProcessorValueTreeState straight from this table, which is why a new
// parameter only has to be added in one place.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "Shapers.h"
#include "Sync.h"
#include "RhythmMod.h"

namespace fracture {

enum class Kind { Float, Choice, Bool };

struct ParamInfo {
    std::string id;            // matches the JavaScript parameter id
    std::string name;          // shown in the host's automation list
    Kind kind = Kind::Float;
    float min = 0, max = 1, def = 0;
    bool  log = false;         // logarithmic taper, as in the browser's norm()
    bool  mod = false;         // can be a modulation destination
    int   band = -1;           // 0..2 for per-band parameters
    std::string unit;
    std::vector<std::string> choices;    // shown in the host
    std::vector<std::string> choiceIds;  // the JavaScript values, for patch import
};

inline const char* const lfoShapeIds[] = { "sin","tri","saw","ramp","sqr","sh","smooth" };
inline const char* const lfoShapeNames[] = { "Sine","Triangle","Saw up","Saw down","Square",
                                             "Random S&H","Random smooth" };
inline const char* const filterTypeIds[] = { "off","lp","hp","bp","notch","peak" };
inline const char* const filterTypeNames[] = { "Off","Low pass","High pass","Band pass","Notch","Peak" };
// the performance sources were added after the first release, at the end, so a
// saved slot's source index still means what it did
inline const char* const modSourceNames[] = { "—","LFO 1","LFO 2","Envelope","Envelope inv","Tremolo",
                                              "Macro 1","Macro 2","XY X","XY Y" };
inline constexpr int numModSources = 10;
inline const char* const circuitIds[]   = { "clean","analog","vintage" };
inline const char* const circuitNames[] = { "Clean","Analogue","Vintage" };
// 36 and 48 were added later, at the end, so a saved 12 or 24 keeps its index
inline const char* const slopeIds[]     = { "12","24","36","48" };
inline const char* const slopeNames[]   = { "12 dB","24 dB","36 dB","48 dB" };
inline const char* const fbModeIds[]   = { "time","pitch","sync" };
inline const char* const fbModeNames[] = { "Time","Pitch","Sync" };
inline const char* const rhythmShapeIds[] = { "sin","tri","saw","ramp","sqr","steps","rnd" };

inline constexpr int numBands = 3;
inline constexpr int numSlots = 6;          // modulation matrix slots

// ---------------------------------------------------------------- the table
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
    // modulation destinations, in table order — the matrix's dst choice list
    const std::vector<int>& dests() const { return dests_; }

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
                     bool logTaper, bool modable, std::string unit = "", int band = -1){
            ParamInfo p; p.id = std::move(id); p.name = std::move(name); p.kind = Kind::Float;
            p.min = mn; p.max = mx; p.def = df; p.log = logTaper; p.mod = modable;
            p.unit = std::move(unit); p.band = band;
            info_.push_back(std::move(p));
        };
        auto c = [&](std::string id, std::string name, const char* const* names, int n,
                     int df, int band = -1, const char* const* jsIds = nullptr){
            ParamInfo p; p.id = std::move(id); p.name = std::move(name); p.kind = Kind::Choice;
            p.min = 0; p.max = static_cast<float>(n - 1); p.def = static_cast<float>(df);
            p.band = band;
            for (int i = 0; i < n; ++i){
                p.choices.emplace_back(names[i]);
                p.choiceIds.emplace_back(jsIds ? jsIds[i] : names[i]);
            }
            info_.push_back(std::move(p));
        };
        auto b = [&](std::string id, std::string name, bool df, int band = -1){
            ParamInfo p; p.id = std::move(id); p.name = std::move(name); p.kind = Kind::Bool;
            p.min = 0; p.max = 1; p.def = df ? 1.0f : 0.0f; p.band = band;
            info_.push_back(std::move(p));
        };

        // input and pre-filter
        f("inGain", "Input",  -24, 24, 0, false, true, "dB");
        f("preHP",  "Pre HP",  20, 2000, 20, true, true, "Hz");
        f("preLP",  "Pre LP",  500, 20000, 20000, true, true, "Hz");
        // split
        { const char* const n[] = { "1 band", "2 bands", "3 bands" };
          const char* const j[] = { "1", "2", "3" };
          c("bands", "Bands", n, 3, 2, -1, j); }
        f("x1", "Split 1", 60, 1200, 220, true, true, "Hz");
        f("x2", "Split 2", 600, 9000, 2200, true, true, "Hz");
        // crush and feedback
        f("bits",   "Bits",       2, 16, 8, false, true);
        f("redux",  "Downsample", 1, 32, 1, true,  true);
        f("crMix",  "Crush",      0, 100, 0, false, true, "%");
        f("fbAmt",  "Feedback",   0, 85, 0, false, true, "%");
        f("fbTime", "FB time",    1, 250, 55, true, true, "ms");
        f("fbTone", "FB tone",    300, 14000, 3500, true, true, "Hz");
        // post filter
        c("fltType", "Filter type", filterTypeNames, 6, 0, -1, filterTypeIds);
        f("fltFreq", "Cutoff", 30, 18000, 1200, true, true, "Hz");
        f("fltQ",    "Reso",   0.3f, 18, 0.7f, true, true);
        // output
        f("mix",     "Dry/wet", 0, 100, 100, false, true, "%");
        f("width",   "Width",   0, 200, 100, false, true, "%");
        f("outGain", "Output",  -24, 12, 0, false, true, "dB");
        b("autoGain", "Auto gain", true);
        b("safety",   "Safety clip", true);
        { const char* const n[] = { "Off", "2x", "4x" };
          const char* const j[] = { "off", "2x", "4x" };
          c("osFactor", "Oversampling", n, 3, 2, -1, j); }
        // modulation sources
        f("l1Rate", "LFO 1 rate", 0.02f, 20, 0.5f, true, false, "Hz");
        c("l1Div", "LFO 1 division", divNames, numDivs, 0, -1, divIds);
        c("l1Shape", "LFO 1 shape", lfoShapeNames, 7, 0, -1, lfoShapeIds);
        f("l1Depth", "LFO 1 depth", 0, 100, 100, false, false, "%");
        f("l2Rate", "LFO 2 rate", 0.02f, 20, 3, true, false, "Hz");
        c("l2Div", "LFO 2 division", divNames, numDivs, 0, -1, divIds);
        c("l2Shape", "LFO 2 shape", lfoShapeNames, 7, 0, -1, lfoShapeIds);
        f("l2Depth", "LFO 2 depth", 0, 100, 100, false, false, "%");
        f("envAtk",  "Env attack",  1, 300, 12, true, false, "ms");
        f("envRel",  "Env release", 20, 1200, 220, true, false, "ms");
        f("envSens", "Env sens",    0, 100, 60, false, false, "%");
        // per band
        std::vector<std::string> modeNames;
        for (int m = 0; m < numModes; ++m) modeNames.emplace_back(modeName(m));
        std::vector<const char*> modePtrs, modeIdPtrs;
        for (auto& s : modeNames) modePtrs.push_back(s.c_str());
        for (int m = 0; m < numModes; ++m) modeIdPtrs.push_back(modeId(m));
        for (int i = 0; i < numBands; ++i){
            const std::string s = std::to_string(i);
            const std::string pre = "B" + std::to_string(i + 1) + " ";
            f("d" + s + "a", pre + "Drive A", 1, 40, 4, true, true, "x", i);
            c("m" + s + "a", pre + "Mode A", modePtrs.data(), numModes, 0, i, modeIdPtrs.data());
            b("sb" + s, pre + "Stage B", false, i);
            f("d" + s + "b", pre + "Drive B", 1, 40, 2.5f, true, true, "x", i);
            c("m" + s + "b", pre + "Mode B", modePtrs.data(), numModes, 4, i, modeIdPtrs.data());
            f("t" + s,  pre + "Tone",  -12, 12, 0, false, true, "dB", i);
            f("mx" + s, pre + "Mix",   0, 100, 100, false, true, "%", i);
            f("lv" + s, pre + "Level", -24, 12, 0, false, true, "dB", i);
            b("mu" + s, pre + "Mute", false, i);
            b("so" + s, pre + "Solo", false, i);
        }
        // ---- added after the first release, and deliberately at the end.
        // The matrix stores its destination as an index into the list of
        // modulatable parameters, so a NEW MODULATABLE PARAMETER has to be
        // added here rather than next to its neighbours: inserting one higher
        // up would silently re-point the matrix of every session already
        // saved. Choices and toggles are not destinations and are free to sit
        // wherever they read best.
        //
        // the ladder: circuit, slope, and the two controls only it has
        c("fltCirc",  "Filter circuit", circuitNames, 3, 0, -1, circuitIds);
        c("fltPoles", "Filter slope",   slopeNames,   4, 1, -1, slopeIds);
        f("fltDrive", "Filter drive", 1, 16, 1, true, true, "x");
        f("fltDrift", "Filter drift", 0, 100, 35, false, false, "%");
        // tremolo, at the very end of the chain
        b("trOn",     "Tremolo", false);
        c("trDiv",    "Trem division", divNames, numDivs, 10, -1, divIds);
        f("trRate",   "Trem rate",   0.05f, 20, 5, true, true, "Hz");
        f("trDepth",  "Trem depth",  0, 100, 60, false, true, "%");
        f("trShape",  "Trem shape",  0, 100, 0, false, true, "%");
        f("trEdge",   "Trem edge",   0, 100, 50, false, true, "%");
        f("trDuty",   "Trem duty",   5, 95, 50, false, true, "%");
        f("trSpread", "Trem spread", 0, 180, 0, false, true, "deg");
        // the rhythm on the post filter, and the filter's own mix. The rule
        // above applies: the modulatable ones are new destinations, so they go
        // here, after everything that was already a destination. The eight
        // steps are not destinations; eight more entries in every matrix
        // target list would bury the ones people actually reach for.
        f("fltMix",   "Filter mix",   0, 100, 100, false, true, "%");
        f("rhDepth",  "Rhythm mod",   -6, 6, 0, false, true, "oct");
        c("rhDiv",    "Rhythm",       divNames, numDivs, 10, -1, divIds);
        f("rhRate",   "Rhythm rate",  0.05f, 20, 2, true, true, "Hz");
        c("rhShape",  "Rhythm shape", rhythmShapeNames, rhythmNumShapes, 0, -1, rhythmShapeIds);
        f("rhGroove", "Groove",       50, 75, 50, false, true, "%");
        f("rhPhase",  "Rhythm phase", 0, 180, 0, false, true, "deg");
        f("rhGlide",  "Glide",        0, 100, 10, false, true, "%");
        { const float pattern[8] = { 100, 0, 60, 0, 100, 25, 60, 0 };
          for (int k = 0; k < 8; ++k)
              f("rhStep" + std::to_string(k + 1), "Step " + std::to_string(k + 1),
                0, 100, pattern[k], false, false, "%"); }
        // tuned feedback, after Rift: the loop's length as a time (what it
        // always was), as a pitch it rings at, or as a note division; and
        // whether it goes back through the drive section. At Time, off, it is
        // the loop every existing patch was made with
        c("fbMode", "FB mode", fbModeNames, 3, 0, -1, fbModeIds);
        f("fbNote", "FB pitch", 24, 96, 57, false, true, "note");     // A3, 220 Hz
        c("fbDiv",  "FB division", divNames + 1, numDivs - 1, 9, -1, divIds + 1);
        b("fbThru", "FB through drive", false);
        // performance: two macro knobs and an XY pad. They are matrix SOURCES,
        // not targets: assign one in the matrix and a single gesture moves
        // everything it is routed to. At 0 they add nothing, so no patch that
        // does not route them changes
        f("mc1", "Macro 1", 0, 100, 0, false, false, "%");
        f("mc2", "Macro 2", 0, 100, 0, false, false, "%");
        f("xyX", "XY X", 0, 100, 0, false, false, "%");
        f("xyY", "XY Y", 0, 100, 0, false, false, "%");

        // the matrix destination list is every modulatable parameter above
        for (size_t i = 0; i < info_.size(); ++i)
            if (info_[i].mod) dests_.push_back(static_cast<int>(i));
        std::vector<std::string> destNames { "—" }, destIds { "" };
        for (int d : dests_){
            destNames.push_back(info_[static_cast<size_t>(d)].name);
            destIds.push_back(info_[static_cast<size_t>(d)].id);
        }
        std::vector<const char*> destPtrs, destIdPtrs;
        for (auto& s : destNames) destPtrs.push_back(s.c_str());
        for (auto& s : destIds) destIdPtrs.push_back(s.c_str());
        static const char* const srcIds[] = { "", "lfo1", "lfo2", "env", "env-", "trem",
                                              "mc1", "mc2", "xyx", "xyy" };
        for (int k = 0; k < numSlots; ++k){
            const std::string s = std::to_string(k);
                c("mS" + s, "Mod " + std::to_string(k + 1) + " source", modSourceNames, numModSources, 0, -1, srcIds);
            c("mD" + s, "Mod " + std::to_string(k + 1) + " target",
              destPtrs.data(), static_cast<int>(destPtrs.size()), 0, -1, destIdPtrs.data());
            f("mA" + s, "Mod " + std::to_string(k + 1) + " amount", -100, 100, 0, false, false, "%");
        }
    }
    std::vector<ParamInfo> info_;
    std::vector<int> dests_;
};

// A value from a browser patch (fx/fracture.html writes {id: value} JSON, where
// choices are strings like "tube" and toggles are booleans) turned into the
// number this table stores. Returns false if the id or the choice is unknown.
inline bool patchValueToParam(const ParamInfo& p, const std::string& text, bool isNumber,
                              double number, bool isBool, bool boolean, float& out){
    switch (p.kind){
    case Kind::Float:
        if (!isNumber) return false;
        out = static_cast<float>(std::clamp(number, static_cast<double>(p.min), static_cast<double>(p.max)));
        return true;
    case Kind::Bool:
        if (isBool) { out = boolean ? 1.0f : 0.0f; return true; }
        if (isNumber){ out = number != 0.0 ? 1.0f : 0.0f; return true; }
        return false;
    case Kind::Choice:
        for (size_t i = 0; i < p.choiceIds.size(); ++i)
            if (p.choiceIds[i] == text){ out = static_cast<float>(i); return true; }
        if (isNumber && number >= 0 && number < static_cast<double>(p.choices.size())){
            out = static_cast<float>(number); return true;   // already an index
        }
        return false;
    }
    return false;
}

// convenient ids used by the DSP, resolved once
struct Ids {
    static const Ids& get(){ static Ids i; return i; }
    int inGain, preHP, preLP, bands, x1, x2;
    int bits, redux, crMix, fbAmt, fbTime, fbTone;
    int fltType, fltFreq, fltQ, fltCirc, fltPoles, fltDrive, fltDrift;
    int trOn, trDiv, trRate, trDepth, trShape, trEdge, trDuty, trSpread;
    int fbMode, fbNote, fbDiv, fbThru, mc1, mc2, xyX, xyY;
    int fltMix, rhDepth, rhDiv, rhRate, rhShape, rhGroove, rhPhase, rhGlide, rhStep[8];
    int mix, width, outGain, autoGain, safety, osFactor;
    int l1Rate, l1Div, l1Shape, l1Depth, l2Rate, l2Div, l2Shape, l2Depth;
    int envAtk, envRel, envSens;
    int bandDriveA[numBands], bandModeA[numBands], bandStageB[numBands], bandDriveB[numBands],
        bandModeB[numBands], bandTone[numBands], bandMix[numBands], bandLevel[numBands],
        bandMute[numBands], bandSolo[numBands];
    int slotSrc[numSlots], slotDst[numSlots], slotAmt[numSlots];
private:
    Ids(){
        const Params& p = Params::get();
        auto I = [&](const char* s){ return p.index(s); };
        inGain = I("inGain"); preHP = I("preHP"); preLP = I("preLP");
        bands = I("bands"); x1 = I("x1"); x2 = I("x2");
        bits = I("bits"); redux = I("redux"); crMix = I("crMix");
        fbAmt = I("fbAmt"); fbTime = I("fbTime"); fbTone = I("fbTone");
        fltType = I("fltType"); fltFreq = I("fltFreq"); fltQ = I("fltQ");
        fltCirc = I("fltCirc"); fltPoles = I("fltPoles");
        fltDrive = I("fltDrive"); fltDrift = I("fltDrift");
        trOn = I("trOn"); trDiv = I("trDiv"); trRate = I("trRate"); trDepth = I("trDepth");
        trShape = I("trShape"); trEdge = I("trEdge"); trDuty = I("trDuty"); trSpread = I("trSpread");
        mc1 = I("mc1"); mc2 = I("mc2"); xyX = I("xyX"); xyY = I("xyY");
        fbMode = I("fbMode"); fbNote = I("fbNote"); fbDiv = I("fbDiv"); fbThru = I("fbThru");
        fltMix = I("fltMix"); rhDepth = I("rhDepth"); rhDiv = I("rhDiv"); rhRate = I("rhRate");
        rhShape = I("rhShape"); rhGroove = I("rhGroove"); rhPhase = I("rhPhase"); rhGlide = I("rhGlide");
        for (int k = 0; k < 8; ++k) rhStep[k] = I(("rhStep" + std::to_string(k + 1)).c_str());
        mix = I("mix"); width = I("width"); outGain = I("outGain");
        autoGain = I("autoGain"); safety = I("safety"); osFactor = I("osFactor");
        l1Rate = I("l1Rate"); l1Div = I("l1Div"); l1Shape = I("l1Shape"); l1Depth = I("l1Depth");
        l2Rate = I("l2Rate"); l2Div = I("l2Div"); l2Shape = I("l2Shape"); l2Depth = I("l2Depth");
        envAtk = I("envAtk"); envRel = I("envRel"); envSens = I("envSens");
        for (int i = 0; i < numBands; ++i){
            const std::string s = std::to_string(i);
            bandDriveA[i] = I(("d" + s + "a").c_str());
            bandModeA[i]  = I(("m" + s + "a").c_str());
            bandStageB[i] = I(("sb" + s).c_str());
            bandDriveB[i] = I(("d" + s + "b").c_str());
            bandModeB[i]  = I(("m" + s + "b").c_str());
            bandTone[i]   = I(("t" + s).c_str());
            bandMix[i]    = I(("mx" + s).c_str());
            bandLevel[i]  = I(("lv" + s).c_str());
            bandMute[i]   = I(("mu" + s).c_str());
            bandSolo[i]   = I(("so" + s).c_str());
        }
        for (int k = 0; k < numSlots; ++k){
            const std::string s = std::to_string(k);
            slotSrc[k] = I(("mS" + s).c_str());
            slotDst[k] = I(("mD" + s).c_str());
            slotAmt[k] = I(("mA" + s).c_str());
        }
    }
};

} // namespace fracture
