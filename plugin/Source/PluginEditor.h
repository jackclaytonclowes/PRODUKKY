// PluginEditor.h — the Béton clair interface: the same panels, controls and
// readouts as fx/fracture.html, drawn natively.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Bauhaus.h"

// ---------------------------------------------------------------- primitives
class KnobBox : public juce::Component {
public:
    KnobBox(FractureProcessor& p, const juce::String& paramId, juce::Colour hue,
            bool small = false, bool withCaption = true);
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();                                    // value text and mod tick
    static constexpr int w = 62, h = 76, wSmall = 56, hSmall = 62;
private:
    FractureProcessor& proc;
    const fracture::ParamInfo& info;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    juce::String caption, valueText;
    bool isSmall, showCaption;
};

class ChoiceBox : public juce::Component {
public:
    ChoiceBox(FractureProcessor& p, const juce::String& paramId, const juce::String& label, int width = 116);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::ComboBox box;
private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attach;
};

class ToggleBox : public juce::Component {
public:
    ToggleBox(FractureProcessor& p, const juce::String& paramId, juce::Colour onColour, int width = 96);
    void resized() override;
    juce::TextButton button;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach;
};

class Caption;

class Panel : public juce::Component {
public:
    Panel(int number, juce::String title, juce::Colour hue);
    void paint(juce::Graphics&) override;
    juce::Rectangle<int> content() const;
    static constexpr int barHeight = 26;
private:
    int no;
    juce::String title;
    juce::Colour hue;
};

// ------------------------------------------------------------------- visuals
class Scope : public juce::Component, private juce::Timer {
public:
    explicit Scope(FractureProcessor&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    FractureProcessor& proc;
    std::array<float, FractureProcessor::scopeSize / 2> spectrum {};
    std::array<float, 36> bars {};
};

class Meters : public juce::Component, private juce::Timer {
public:
    explicit Meters(FractureProcessor&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    FractureProcessor& proc;
    float inDb = -60.0f, outDb = -60.0f;
};

class ModSources : public juce::Component, private juce::Timer {
public:
    explicit ModSources(FractureProcessor&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    FractureProcessor& proc;
};

// -------------------------------------------------------------------- editor
class FractureEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit FractureEditor(FractureProcessor&);
    ~FractureEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateTabs();
    void selectBand(int band);
    void buildBand(int band);

    FractureProcessor& proc;
    bauhaus::Look look;

    static constexpr int designW = 1180, designH = 1190;
    bauhaus::Canvas canvas { designW, designH };       // everything is drawn on this

    void paintDesign(juce::Graphics&);                 // both work in design coordinates
    void layoutDesign();
    bool built = false;                                // layout runs once everything exists

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<KnobBox*> knobs;
    std::vector<Panel*> panels;

    Panel* pIn = nullptr; Panel* pSplit = nullptr; Panel* pDrive = nullptr;
    Panel* pCrush = nullptr; Panel* pFilter = nullptr; Panel* pOut = nullptr;
    Panel* pMod = nullptr; Panel* pScope = nullptr;

    std::vector<juce::Component*> inRow, splitRow, crushRow, filterRow, outRow;
    std::vector<juce::Component*> bandRow[3];
    std::vector<juce::Component*> lfoRow[2], envRow;
    std::vector<juce::Component*> matrixRow[6];

    juce::ComboBox presetBox;
    juce::TextButton copyButton { "Copy patch" }, pasteButton { "Paste patch" };
    juce::TextButton bandTab[3];
    juce::Component* bandPane[3] {};
    int currentBand = 0;

    Caption* lfoCaption[2] {}; Caption* envCaption = nullptr; Caption* matrixCaption = nullptr;
    Scope* scope = nullptr;
    Meters* meters = nullptr;
    ModSources* modSources = nullptr;

    template <typename T, typename... A> T* make(A&&... args){
        auto p = std::make_unique<T>(std::forward<A>(args)...);
        T* raw = p.get();
        owned.push_back(std::move(p));
        canvas.addAndMakeVisible(raw);
        return raw;
    }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FractureEditor)
};
