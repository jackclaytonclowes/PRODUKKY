// PluginEditor.h — the same Béton clair the other box wears, with one panel it
// does not have: the feel strip, which draws where the sixteenths actually land.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Bauhaus.h"

class KnobBox : public juce::Component {
public:
    KnobBox(CrateProcessor&, const juce::String& paramId, juce::Colour hue);
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();
    static constexpr int w = 66, h = 78;
private:
    CrateProcessor& proc;
    const crate::ParamInfo& info;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    juce::String caption, valueText;
};

class ChoiceBox : public juce::Component {
public:
    ChoiceBox(CrateProcessor&, const juce::String& paramId, const juce::String& label, int width = 110);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::ComboBox box;
private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attach;
};

class ToggleBox : public juce::Component {
public:
    ToggleBox(CrateProcessor&, const juce::String& paramId, juce::Colour onColour, int width = 96);
    void resized() override;
    juce::TextButton button;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach;
};

class Panel : public juce::Component {
public:
    Panel(int number, juce::String title, juce::Colour hue);
    void paint(juce::Graphics&) override;
    juce::Rectangle<int> content() const;
    static constexpr int barHeight = 26;
private:
    int no; juce::String title; juce::Colour hue;
};

// one bar of sixteenths, drawn where they will actually sound
class FeelStrip : public juce::Component, private juce::Timer {
public:
    explicit FeelStrip(CrateProcessor&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    CrateProcessor& proc;
};

class Meters : public juce::Component, private juce::Timer {
public:
    explicit Meters(CrateProcessor&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    CrateProcessor& proc;
    float inDb = -60.0f, outDb = -60.0f;
};

class CrateEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit CrateEditor(CrateProcessor&);
    ~CrateEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    CrateProcessor& proc;
    bauhaus::Look look;
    static constexpr int designW = 1080, designH = 620;

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<KnobBox*> knobs;
    Panel *pIn = nullptr, *pConv = nullptr, *pFilter = nullptr,
          *pFeel = nullptr, *pDust = nullptr, *pOut = nullptr;
    std::vector<juce::Component*> inRow, convRow, filterRow, feelRow, dustRow, outRow;
    juce::ComboBox presetBox;
    FeelStrip* strip = nullptr;
    Meters* meters = nullptr;

    template <typename T, typename... A> T* make(A&&... args){
        auto p = std::make_unique<T>(std::forward<A>(args)...);
        T* raw = p.get();
        owned.push_back(std::move(p));
        addAndMakeVisible(raw);
        return raw;
    }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrateEditor)
};
