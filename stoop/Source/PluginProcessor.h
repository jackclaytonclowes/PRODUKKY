// PluginProcessor.h — the JUCE wrapper. The DSP is in ../core and knows nothing
// about JUCE; this file is parameters, state, the transport (for seeding the
// record) and latency.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "StoopCore.h"
#include "Session.h"

class StoopProcessor : public juce::AudioProcessor {
public:
    StoopProcessor();
    ~StoopProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "STOOP"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // the pitch shifter's feedback at full loses 0.9 dB a pass, about 20 ms a
    // pass, so it takes some 1.3 s to fall 60 dB
    double getTailLengthSeconds() const override { return 1.5; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    stoop::Engine engine;

    // A patch as {id: value} JSON, choices by name ("speed": "45 rpm"). What
    // your own presets are saved as. Anything left out goes back to default.
    juce::String savePatch() const;
    bool loadPatch(const juce::String& json);

    // undo, redo and A/B (Session.h), and presets you save yourself
    std::unique_ptr<session::Session> session;
    session::UserPresets userPresets { "STOOP" };
    bool saveUserPreset(const juce::File& file);
    bool loadUserPreset(const juce::File& file);
    juce::String userPresetName() const { return userPreset; }

    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<float> reduction { 0.0f };     // the compressor's gain reduction, dB
    std::atomic<float> levelDb { -100.0f };    // what its detector hears, dBFS
    std::atomic<float> revolution { 0.0f };    // where the record is in its turn, 0..1
    std::atomic<float> cents { 0.0f };         // how far off speed it is playing, now

private:
    juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();
    std::vector<std::atomic<float>*> raw;
    int currentProgram = 0;
    juce::String userPreset;                            // shown in the menu while it is loaded
    int reportedLatency = -1;
    int64_t lastPlayhead = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StoopProcessor)
};
