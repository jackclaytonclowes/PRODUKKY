#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace crate;

juce::AudioProcessorValueTreeState::ParameterLayout CrateProcessor::buildLayout(){
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const Params& P = Params::get();
    for (int i = 0; i < P.count(); ++i){
        const ParamInfo& p = P[i];
        const juce::ParameterID pid { p.id, 1 };
        switch (p.kind){
        case Kind::Float: {
            juce::NormalisableRange<float> range { p.min, p.max };
            if (p.log) range.setSkewForCentre(std::sqrt(p.min * p.max));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                pid, p.name, range, p.def,
                juce::AudioParameterFloatAttributes().withLabel(p.unit)));
            break;
        }
        case Kind::Choice: {
            juce::StringArray choices;
            for (const auto& c : p.choices) choices.add(c);
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                pid, p.name, choices, static_cast<int>(p.def)));
            break;
        }
        case Kind::Bool:
            layout.add(std::make_unique<juce::AudioParameterBool>(pid, p.name, p.def > 0.5f));
            break;
        }
    }
    return layout;
}

CrateProcessor::CrateProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "CRATE", buildLayout())
{
    const Params& P = Params::get();
    raw.resize(static_cast<size_t>(P.count()));
    for (int i = 0; i < P.count(); ++i) raw[static_cast<size_t>(i)] = apvts.getRawParameterValue(P[i].id);
    std::vector<juce::RangedAudioParameter*> params;
    for (int i = 0; i < P.count(); ++i) params.push_back(apvts.getParameter(P[i].id));
    session = std::make_unique<session::Session>(std::move(params));
}

bool CrateProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void CrateProcessor::prepareToPlay(double sampleRate, int samplesPerBlock){
    engine.prepare(sampleRate, samplesPerBlock);
    reportedLatency = engine.latencySamples();
    setLatencySamples(reportedLatency);
}

void CrateProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&){
    juce::ScopedNoDenormals noDenormals;
    const Params& P = Params::get();
    for (int i = 0; i < P.count(); ++i) engine.setParam(i, raw[static_cast<size_t>(i)]->load());

    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
    if (auto* ph = getPlayHead()){
        if (const auto pos = ph->getPosition()){
            playing = pos->getIsPlaying();
            ppq = pos->getPpqPosition().orFallback(0.0);
            bpm = pos->getBpm().orFallback(120.0);
            if (const auto s = pos->getTimeInSamples()){
                // reseed the crackle whenever the transport jumps, so the same
                // bar renders the same way every time
                if (std::abs(*s - lastPlayhead) > buffer.getNumSamples() + 1) engine.seedFrom(*s);
                lastPlayhead = *s + buffer.getNumSamples();
            }
        }
    }
    engine.setTransport(playing, ppq, bpm);
    transportRunning.store(playing);
    hostBpm.store(static_cast<float>(bpm));

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    float* io[2] = { buffer.getWritePointer(0),
                     buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr };
    engine.process(io, buffer.getNumChannels() > 1 ? 2 : 1, buffer.getNumSamples());

    if (engine.latencySamples() != reportedLatency){
        reportedLatency = engine.latencySamples();
        setLatencySamples(reportedLatency);      // the grid setting changes it
    }
    inPeak.store(engine.inPeak);
    outPeak.store(engine.outPeak);
    rhythmNow.store(static_cast<float>(engine.rhythmValue(0)));
}

int CrateProcessor::getNumPrograms(){ return static_cast<int>(presets().size()); }
const juce::String CrateProcessor::getProgramName(int index){
    const auto& all = presets();
    if (index < 0 || index >= static_cast<int>(all.size())) return {};
    return juce::String::fromUTF8(all[static_cast<size_t>(index)].name);   // UTF-8, not ASCII
}
void CrateProcessor::setCurrentProgram(int index){
    const auto& all = presets();
    if (index < 0 || index >= static_cast<int>(all.size())) return;
    currentProgram = index;
    const Params& P = Params::get();
    for (int i = 0; i < P.count(); ++i){
        float v = P[i].def;
        for (const auto& kv : all[static_cast<size_t>(index)].values)
            if (P[i].id == kv.first) v = kv.second;
        if (auto* p = apvts.getParameter(P[i].id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    }
    userPreset.clear();
    session->commit();
}

// ------------------------------------------------------------ patches as text
juce::String CrateProcessor::savePatch() const {
    const Params& P = Params::get();
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    for (int i = 0; i < P.count(); ++i){
        const ParamInfo& p = P[i];
        const float v = raw[static_cast<size_t>(i)]->load();
        if (std::abs(v - p.def) < 1.0e-6f) continue;               // defaults stay implicit
        switch (p.kind){
        case Kind::Float:  obj->setProperty(juce::Identifier(p.id), v); break;
        case Kind::Bool:   obj->setProperty(juce::Identifier(p.id), v > 0.5f); break;
        case Kind::Choice: {
            const int n = static_cast<int>(std::lround(v));
            if (n >= 0 && n < static_cast<int>(p.choices.size()))
                obj->setProperty(juce::Identifier(p.id), juce::String(p.choices[static_cast<size_t>(n)]));
            break;
        }
        }
    }
    return juce::JSON::toString(juce::var(obj.get()), true);
}

bool CrateProcessor::loadPatch(const juce::String& json){
    auto parsed = juce::JSON::parse(json);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) return false;
    const Params& P = Params::get();
    std::vector<float> values(static_cast<size_t>(P.count()));
    for (int i = 0; i < P.count(); ++i) values[static_cast<size_t>(i)] = P[i].def;
    int applied = 0;
    for (const auto& prop : obj->getProperties()){
        const int idx = P.index(prop.name.toString().toStdString());
        if (idx < 0) continue;
        const ParamInfo& p = P[idx];
        const juce::var& v = prop.value;
        float out = 0.0f;
        if (p.kind == Kind::Choice && v.isString()){
            const auto it = std::find(p.choices.begin(), p.choices.end(), v.toString().toStdString());
            if (it == p.choices.end()) continue;
            out = static_cast<float>(it - p.choices.begin());
        }
        else if (v.isBool()) out = static_cast<bool>(v) ? 1.0f : 0.0f;
        else if (v.isDouble() || v.isInt() || v.isInt64()) out = static_cast<float>(static_cast<double>(v));
        else continue;
        values[static_cast<size_t>(idx)] = std::clamp(out, p.min, p.max);
        ++applied;
    }
    if (applied == 0) return false;
    for (int i = 0; i < P.count(); ++i)
        if (auto* p = apvts.getParameter(P[i].id))
            p->setValueNotifyingHost(p->convertTo0to1(values[static_cast<size_t>(i)]));
    return true;
}

bool CrateProcessor::saveUserPreset(const juce::File& file){
    if (!userPresets.save(file, savePatch())) return false;
    userPreset = file.getFileNameWithoutExtension();
    return true;
}
bool CrateProcessor::loadUserPreset(const juce::File& file){
    if (!loadPatch(file.loadFileAsString())) return false;
    session->commit();
    userPreset = file.getFileNameWithoutExtension();
    return true;
}

void CrateProcessor::getStateInformation(juce::MemoryBlock& destData){
    if (auto xml = apvts.copyState().createXml()){
        session->saveInto(*xml);                       // the hidden A/B slot
        copyXmlToBinary(*xml, destData);
    }
}
void CrateProcessor::setStateInformation(const void* data, int sizeInBytes){
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            session->restoreFrom(*xml);                // and takes it out of the tree
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            // replaceState skips a parameter whose stored value looks unchanged,
            // and for a switch "unchanged" is judged after snapping: a toggle a
            // host left at 0.21 reads as off, the state says off, so nothing is
            // written and the parameter keeps reporting 0.21. pluginval caught
            // it (on four switches in FRACTURE, and on CRATE's with other seeds).
            // Writing every parameter back from the restored state makes the
            // value the host reads the value that was saved.
            for (auto* param : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
                    rp->setValueNotifyingHost(rp->convertTo0to1(
                        apvts.getRawParameterValue(rp->getParameterID())->load()));
            session->reset();
        }
}

juce::AudioProcessorEditor* CrateProcessor::createEditor(){ return new CrateEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){ return new CrateProcessor(); }
