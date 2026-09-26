#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

using namespace fracture;

// ---------------------------------------------------------------- parameters
juce::AudioProcessorValueTreeState::ParameterLayout FractureProcessor::buildLayout(){
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const Params& P = Params::get();
    for (int i = 0; i < P.count(); ++i){
        const ParamInfo& p = P[i];
        const juce::ParameterID pid { p.id, 1 };
        switch (p.kind){
        case Kind::Float: {
            juce::NormalisableRange<float> range { p.min, p.max };
            if (p.log) range.setSkewForCentre(std::sqrt(p.min * p.max));  // the browser's log taper
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

FractureProcessor::FractureProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FRACTURE", buildLayout())
{
    const Params& P = Params::get();
    raw.resize(static_cast<size_t>(P.count()));
    for (int i = 0; i < P.count(); ++i) raw[static_cast<size_t>(i)] = apvts.getRawParameterValue(P[i].id);
}

bool FractureProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void FractureProcessor::prepareToPlay(double sampleRate, int samplesPerBlock){
    engine.prepare(sampleRate, samplesPerBlock);
    reportedLatency = engine.latencySamples();
    setLatencySamples(reportedLatency);
    scopeFill = 0;
    scopeFifo.fill(0.0f);
}

void FractureProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&){
    juce::ScopedNoDenormals noDenormals;
    const Params& P = Params::get();
    for (int i = 0; i < P.count(); ++i) engine.setParam(i, raw[static_cast<size_t>(i)]->load());

    // the host's clock: tempo and song position for the synced LFOs and the
    // tremolo, and a reseed of the random LFO shapes whenever the transport
    // jumps, so a bounce of the same bar is the same audio every time
    fracture::Transport transport;
    if (auto* ph = getPlayHead()){
        if (const auto pos = ph->getPosition()){
            transport.valid = true;
            transport.playing = pos->getIsPlaying();
            if (const auto bpm = pos->getBpm()) transport.bpm = *bpm;
            if (const auto ppq = pos->getPpqPosition()) transport.ppq = *ppq;
            if (const auto s = pos->getTimeInSamples()){
                if (*s != lastPlayhead){
                    if (std::abs(*s - lastPlayhead) > buffer.getNumSamples() + 1) engine.seedFrom(*s);
                    lastPlayhead = *s + buffer.getNumSamples();
                }
            }
        }
    }
    engine.setTransport(transport);
    hostBpm.store(static_cast<float>(transport.bpm));
    hostPlaying.store(transport.valid && transport.playing);

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    float* io[2] = { buffer.getWritePointer(0),
                     buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr };
    engine.process(io, buffer.getNumChannels() > 1 ? 2 : 1, buffer.getNumSamples());

    if (engine.latencySamples() != reportedLatency){
        reportedLatency = engine.latencySamples();
        setLatencySamples(reportedLatency);
    }

    inPeak.store(engine.inPeak);
    outPeak.store(engine.outPeak);
    rhythmOut.store(engine.rhythmOut());
    lfo1.store(engine.lfo1());
    lfo2.store(engine.lfo2());
    envOut.store(engine.envOut());
    tremOut.store(engine.tremOut());
    pushScopeSamples(buffer.getReadPointer(0), buffer.getNumSamples());
}

// -------------------------------------------------------------------- scope
void FractureProcessor::pushScopeSamples(const float* data, int n){
    for (int i = 0; i < n; ++i){
        scopeFifo[static_cast<size_t>(scopeFill++)] = data[i];
        if (scopeFill == scopeSize){
            if (!scopeReady.load()){
                std::fill(scopeScratch.begin(), scopeScratch.end(), 0.0f);
                std::copy(scopeFifo.begin(), scopeFifo.end(), scopeScratch.begin());
                window.multiplyWithWindowingTable(scopeScratch.data(), scopeSize);
                fft.performFrequencyOnlyForwardTransform(scopeScratch.data());
                for (size_t k = 0; k < scopeSpectrum.size(); ++k)
                    scopeSpectrum[k] = scopeScratch[k];
                scopeReady.store(true);
            }
            scopeFill = 0;
        }
    }
}
bool FractureProcessor::copyScopeSpectrum(std::array<float, scopeSize / 2>& dest){
    if (!scopeReady.load()) return false;
    dest = scopeSpectrum;
    scopeReady.store(false);
    return true;
}

// ------------------------------------------------------------------ presets
int FractureProcessor::getNumPrograms(){ return static_cast<int>(factoryPresets().size()); }
const juce::String FractureProcessor::getProgramName(int index){
    const auto& all = factoryPresets();
    if (index < 0 || index >= static_cast<int>(all.size())) return {};
    return all[static_cast<size_t>(index)].name;
}
void FractureProcessor::setCurrentProgram(int index){
    const auto& all = factoryPresets();
    if (index < 0 || index >= static_cast<int>(all.size())) return;
    currentProgram = index;
    loadBrowserPatch(all[static_cast<size_t>(index)].json);   // one import path for everything
}

// ------------------------------------------------------------------- state
void FractureProcessor::getStateInformation(juce::MemoryBlock& destData){
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, destData);
}
void FractureProcessor::setStateInformation(const void* data, int sizeInBytes){
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
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
        }
}

bool FractureProcessor::loadBrowserPatch(const juce::String& json){
    auto parsed = juce::JSON::parse(json);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) return false;
    const Params& P = Params::get();
    // anything the patch leaves out goes back to its default, as in the browser
    for (int i = 0; i < P.count(); ++i)
        if (auto* p = apvts.getParameter(P[i].id))
            p->setValueNotifyingHost(p->convertTo0to1(P[i].def));
    int applied = 0;
    for (const auto& prop : obj->getProperties()){
        const int idx = P.index(prop.name.toString().toStdString());
        if (idx < 0) continue;
        const juce::var& v = prop.value;
        float out = 0.0f;
        const bool ok = patchValueToParam(P[idx], v.toString().toStdString(),
                                          v.isDouble() || v.isInt(), static_cast<double>(v),
                                          v.isBool(), static_cast<bool>(v), out);
        if (!ok) continue;
        if (auto* p = apvts.getParameter(P[idx].id)){
            p->setValueNotifyingHost(p->convertTo0to1(out));
            ++applied;
        }
    }
    return applied > 0;
}

juce::String FractureProcessor::saveBrowserPatch() const {
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
            if (n >= 0 && n < static_cast<int>(p.choiceIds.size()))
                obj->setProperty(juce::Identifier(p.id), juce::String(p.choiceIds[static_cast<size_t>(n)]));
            break;
        }
        }
    }
    return juce::JSON::toString(juce::var(obj.get()), true);
}

juce::AudioProcessorEditor* FractureProcessor::createEditor(){ return new FractureEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){ return new FractureProcessor(); }
