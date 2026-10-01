// fracture-wasm.cpp — the plugin's engine (plugin/core), for the browser version.
//
// fx/fracture.html runs FRACTURE's own DSP rather than a second implementation
// of it: this file is compiled to WebAssembly by fx/engine/build.mjs and the
// result is embedded in the page, so the page stays one file that makes no
// requests. Nothing here does any audio work; it only exposes the core to
// JavaScript as plain C functions over numbers and pointers.
//
// One module instance holds one engine. The page makes two: one in the audio
// worklet that processes, and one on the main thread that answers questions
// for the drawings (the parameter table, what is idle, the filter's response,
// the rhythm's shape) without touching the audio.
#include "../../plugin/core/FractureCore.h"
#include "../../plugin/core/Relevance.h"
#include "../../plugin/core/FilterResponse.h"

using namespace fracture;

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

namespace {
constexpr int maxBlock = 128;                   // a Web Audio render quantum
Engine* engine = nullptr;
float buffer[2][maxBlock];
std::vector<float> values;                      // for the questions that take a whole state
std::vector<Idle> idle;
}

// operator new reports failure by throwing; this code is built without
// exceptions, so an allocation failure stops the module instead
extern "C" void* __cxa_allocate_exception(unsigned long){ __builtin_trap(); }
extern "C" void __cxa_throw(void*, void*, void (*)(void*)){ __builtin_trap(); }

// ---------------------------------------------------------------- the table
EXPORT(fx_param_count) int fx_param_count(){ return Params::get().count(); }
EXPORT(fx_param_id)    const char* fx_param_id(int i){ return Params::get()[i].id.c_str(); }
EXPORT(fx_param_name)  const char* fx_param_name(int i){ return Params::get()[i].name.c_str(); }
EXPORT(fx_param_unit)  const char* fx_param_unit(int i){ return Params::get()[i].unit.c_str(); }
EXPORT(fx_param_kind)  int fx_param_kind(int i){ return static_cast<int>(Params::get()[i].kind); }
EXPORT(fx_param_min)   float fx_param_min(int i){ return Params::get()[i].min; }
EXPORT(fx_param_max)   float fx_param_max(int i){ return Params::get()[i].max; }
EXPORT(fx_param_def)   float fx_param_def(int i){ return Params::get()[i].def; }
EXPORT(fx_param_log)   int fx_param_log(int i){ return Params::get()[i].log ? 1 : 0; }
EXPORT(fx_param_mod)   int fx_param_mod(int i){ return Params::get()[i].mod ? 1 : 0; }
EXPORT(fx_param_band)  int fx_param_band(int i){ return Params::get()[i].band; }
EXPORT(fx_param_choices) int fx_param_choices(int i){ return static_cast<int>(Params::get()[i].choices.size()); }
EXPORT(fx_param_choice_name) const char* fx_param_choice_name(int i, int k){ return Params::get()[i].choices[static_cast<size_t>(k)].c_str(); }
EXPORT(fx_param_choice_id) const char* fx_param_choice_id(int i, int k){
    const auto& p = Params::get()[i];
    return static_cast<size_t>(k) < p.choiceIds.size() ? p.choiceIds[static_cast<size_t>(k)].c_str() : "";
}

// ---------------------------------------------------------------- the engine
EXPORT(fx_init) void fx_init(double sampleRate){
    delete engine;
    engine = new Engine();
    engine->prepare(sampleRate, maxBlock);
    values.assign(static_cast<size_t>(Params::get().count()), 0.0f);
}
EXPORT(fx_reset) void fx_reset(){ engine->reset(); }
EXPORT(fx_set) void fx_set(int i, float v){ engine->setParam(i, v); }
EXPORT(fx_modulated) float fx_modulated(int i){ return engine->modulated(i); }
EXPORT(fx_transport) void fx_transport(double bpm, double ppq, int playing){
    Transport t; t.bpm = bpm; t.ppq = ppq; t.playing = playing != 0; t.valid = true;
    engine->setTransport(t);
}
EXPORT(fx_buffer) float* fx_buffer(int ch){ return buffer[ch ? 1 : 0]; }
EXPORT(fx_process) void fx_process(int n){
    float* io[2] = { buffer[0], buffer[1] };
    engine->process(io, 2, std::min(n, maxBlock));
}
// what the panel draws: 0 in peak, 1 out peak, 2 LFO 1, 3 LFO 2, 4 envelope,
// 5 tremolo, 6 rhythm, 7 table position (-1 unused), 8 latency in samples,
// then the filter as it is now: 9 type, 10 circuit, 11 poles, 12 cutoff,
// 13 resonance, 14 drive, 15 mix
EXPORT(fx_meter) double fx_meter(int k){
    const Engine& e = *engine;
    const FilterState f = e.filterState();
    switch (k){
    case 0: return e.inPeak;   case 1: return e.outPeak;
    case 2: return e.lfo1();   case 3: return e.lfo2();
    case 4: return e.envOut(); case 5: return e.tremOut();
    case 6: return e.rhythmOut(); case 7: return e.tablePosition();
    case 8: return e.latencySamples();
    case 9: return f.type;  case 10: return f.circuit; case 11: return f.poles;
    case 12: return f.freq; case 13: return f.q; case 14: return f.drive; case 15: return f.mix;
    default: return 0.0;
    }
}

// ---------------------------------------------------------------- questions
// a whole state, written by the page into this buffer, for the two below
EXPORT(fx_values) float* fx_values(){ return values.data(); }
// what Relevance.h says is idle in it: how many, then each one's parameter and why
EXPORT(fx_idle) int fx_idle(){ idle = idleControls(values.data()); return static_cast<int>(idle.size()); }
EXPORT(fx_idle_param) int fx_idle_param(int k){ return idle[static_cast<size_t>(k)].param; }
EXPORT(fx_idle_why) const char* fx_idle_why(int k){ return idle[static_cast<size_t>(k)].why.c_str(); }
// the post filter's response in dB at freqHz, from its own coefficients
EXPORT(fx_filter_db) double fx_filter_db(int type, int circuit, int poles, double freq, double q,
                                        double drive, double mix, double atHz, double sampleRate){
    FilterState s; s.type = type; s.circuit = circuit; s.poles = poles; s.freq = freq; s.q = q; s.drive = drive; s.mix = mix;
    return 20.0 * std::log10(std::max(1.0e-9, std::abs(filterResponse(s, atHz, sampleRate))));
}
// the rhythm's shape at a point in its cycle, for drawing; the steps are
// values[] at the rhStep parameters
EXPORT(fx_rhythm_shape) double fx_rhythm_shape(int shape, double groove, double cycles){
    const Ids& id = Ids::get();
    double steps[RhythmMod::numSteps];
    for (int k = 0; k < RhythmMod::numSteps; ++k) steps[k] = values[static_cast<size_t>(id.rhStep[k])] / 100.0;
    RhythmMod r; r.configure(1.0, 2.0, shape, groove, 0.0, 0.0, steps);
    return r.shapeAt(cycles);
}
EXPORT(fx_shape) double fx_shape(int mode, double x){ return shape(mode, x); }
