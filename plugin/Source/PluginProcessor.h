// PluginProcessor.h — the JUCE wrapper. All of the DSP lives in ../core, which
// has no dependency on JUCE and is tested on its own (npm run test:core); this
// file is only plumbing: parameters, state, latency and the host contract.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "FractureCore.h"

class FractureProcessor : public juce::AudioProcessor {
public:
    FractureProcessor();
    ~FractureProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FRACTURE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }   // the feedback loop

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Loads a patch copied out of the browser version (fx/fracture.html writes
    // {id: value} JSON, choices as strings). The ids and ranges are the same
    // table, so a web patch means the same thing here.
    bool loadBrowserPatch(const juce::String& json);
    juce::String saveBrowserPatch() const;

    juce::AudioProcessorValueTreeState apvts;
    fracture::Engine engine;

    // metering and scope data for the editor, written on the audio thread
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<float> lfo1 { 0.0f }, lfo2 { 0.0f }, envOut { 0.0f }, tremOut { 0.0f };
    std::atomic<float> rhythmOut { 0.0f };
    // the post filter as it is right now, for the drawn response
    std::atomic<int>   fsType { 0 }, fsCircuit { 0 }, fsPoles { 4 };
    std::atomic<float> fsFreq { 1000.0f }, fsQ { 0.7f }, fsDrive { 1.0f }, fsMix { 1.0f };
    fracture::FilterState filterState() const {
        fracture::FilterState st;
        st.type = fsType.load(); st.circuit = fsCircuit.load(); st.poles = fsPoles.load();
        st.freq = fsFreq.load(); st.q = fsQ.load(); st.drive = fsDrive.load(); st.mix = fsMix.load();
        return st;
    }             // 0..1, for the step display
    std::atomic<float> hostBpm { 120.0f };             // what the panel shows next to a division
    std::atomic<bool>  hostPlaying { false };
    static constexpr int scopeOrder = 11;                 // 2048-point FFT
    static constexpr int scopeSize = 1 << scopeOrder;
    void pushScopeSamples(const float* data, int n);
    bool copyScopeSpectrum(std::array<float, scopeSize / 2>& dest);

private:
    juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();
    std::vector<std::atomic<float>*> raw;              // one per core parameter
    int currentProgram = 0;
    int reportedLatency = -1;
    int64_t lastPlayhead = -1;

    juce::dsp::FFT fft { scopeOrder };
    juce::dsp::WindowingFunction<float> window { scopeSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, scopeSize> scopeFifo {};
    std::array<float, scopeSize * 2> scopeScratch {};
    std::array<float, scopeSize / 2> scopeSpectrum {};
    int scopeFill = 0;
    std::atomic<bool> scopeReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FractureProcessor)
};
