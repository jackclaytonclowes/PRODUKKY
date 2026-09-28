// PluginEditor.h — the same Béton clair the other box wears, with one panel it
// does not have: the feel strip, which draws where the sixteenths actually land.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Bauhaus.h"
#include "Guide.h"

class KnobBox : public juce::Component {
public:
    KnobBox(CrateProcessor&, const juce::String& paramId, juce::Colour hue);
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();
    void setState(bool idle, const juce::String& tip);   // dimmed or not, and what the tooltip says
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
    void setState(bool idle, const juce::String& tip);
    juce::ComboBox box;
private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attach;
};

class ToggleBox : public juce::Component {
public:
    ToggleBox(CrateProcessor&, const juce::String& paramId, juce::Colour onColour, int width = 96);
    void resized() override;
    void setState(bool idle, const juce::String& tip);
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

// the rhythm, drawn: eight steps you can draw with the mouse when the shape is
// Steps, and the shape itself across eight cycles when it is anything else,
// with where the rhythm is right now marked on the right
class StepEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit StepEditor(CrateProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); setTooltip(tip); }
private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<int> lane() const;
    void setFrom(juce::Point<int>);
    CrateProcessor& proc;
    int dragging = -1;
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
    bool keyPressed(const juce::KeyPress&) override;
    std::unique_ptr<session::SessionBar> sessionBar;   // public for the host-level test
private:
    void timerCallback() override;
    CrateProcessor& proc;
    bauhaus::Look look;
    static constexpr int designW = 1080, designH = 810;
    bauhaus::Canvas canvas { designW, designH };       // everything is drawn on this

    void paintDesign(juce::Graphics&);                 // both work in design coordinates
    void layoutDesign();

    std::vector<std::unique_ptr<juce::Component>> owned;
    // every control, by parameter, so the timer can dim the ones that do
    // nothing right now and give each its tooltip
    struct Control { int param; std::function<void(bool, const juce::String&)> set; bool idle = false; juce::String tip; };
    std::vector<Control> controls;
    void updateIdle();

    // the two header buttons: tooltips on or off (remembered on this machine,
    // not in the session, because it is a preference and not a sound), and
    // the built-in guide
    juce::TextButton tipsButton { "Tips" }, guideButton { "Guide" };
    std::unique_ptr<juce::TooltipWindow> tips;
    std::unique_ptr<bauhaus::GuideOverlay> guide;
    juce::Rectangle<int> guideArea;
    std::unique_ptr<juce::PropertiesFile> prefs;
public:
    // for the host-level test
    bool tipsOn() const { return tips != nullptr; }
    void setTips(bool on);
    void setGuideOpen(bool open);
    int guideHeight() const { return guide ? guide->contentHeight() : 0; }
private:
    std::vector<KnobBox*> knobs;
    Panel *pIn = nullptr, *pConv = nullptr, *pFilter = nullptr,
          *pFeel = nullptr, *pDust = nullptr, *pOut = nullptr, *pRhythm = nullptr;
    std::vector<juce::Component*> inRow, convRow, filterRow, feelRow, dustRow, outRow, rhythmRow;
    std::unique_ptr<session::PresetMenu> presetMenu;
    FeelStrip* strip = nullptr;
    Meters* meters = nullptr;
    StepEditor* steps = nullptr;

    template <typename T, typename... A> T* make(A&&... args){
        auto p = std::make_unique<T>(std::forward<A>(args)...);
        T* raw = p.get();
        owned.push_back(std::move(p));
        canvas.addAndMakeVisible(raw);
        return raw;
    }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrateEditor)
};
