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
    return all[static_cast<size_t>(index)].name;
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
}

void CrateProcessor::getStateInformation(juce::MemoryBlock& destData){
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, destData);
}
void CrateProcessor::setStateInformation(const void* data, int sizeInBytes){
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* CrateProcessor::createEditor(){ return new CrateEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){ return new CrateProcessor(); }
