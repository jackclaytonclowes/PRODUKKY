// PluginProcessor.h — the JUCE wrapper. The DSP is in ../core and knows nothing
// about JUCE; this file is parameters, state, transport and latency.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CrateCore.h"

class CrateProcessor : public juce::AudioProcessor {
public:
    CrateProcessor();
    ~CrateProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CRATE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.4; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    crate::Engine engine;

    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<bool> transportRunning { false };
    std::atomic<float> hostBpm { 120.0f };
    std::atomic<float> rhythmNow { 0.0f };     // where the rhythm is, 0..1, for the step display

private:
    juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();
    std::vector<std::atomic<float>*> raw;
    int currentProgram = 0;
    int reportedLatency = -1;
    int64_t lastPlayhead = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrateProcessor)
};
