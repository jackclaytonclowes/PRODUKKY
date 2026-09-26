// PluginEditor.h — the Béton clair interface: the same panels, controls and
// readouts as fx/fracture.html, drawn natively.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Bauhaus.h"
#include "Guide.h"

// ---------------------------------------------------------------- primitives
class KnobBox : public juce::Component {
public:
    KnobBox(FractureProcessor& p, const juce::String& paramId, juce::Colour hue,
            bool small = false, bool withCaption = true, bool valueBeside = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();                                    // value text and mod tick
    void setState(bool idle, const juce::String& tip); // dimmed or not, and its tooltip
    static constexpr int w = 62, h = 76, wSmall = 56, hSmall = 62;
private:
    FractureProcessor& proc;
    const fracture::ParamInfo& info;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    juce::String caption, valueText;
    bool isSmall, showCaption, beside;
};

class ChoiceBox : public juce::Component {
public:
    ChoiceBox(FractureProcessor& p, const juce::String& paramId, const juce::String& label, int width = 116);
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
    ToggleBox(FractureProcessor& p, const juce::String& paramId, juce::Colour onColour, int width = 96);
    void resized() override;
    void setState(bool idle, const juce::String& tip);
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

// one cycle of the tremolo as it will actually sound: shape, duty, edge and
// depth, with a marker where the modulation currently is
class TremStrip : public juce::Component, private juce::Timer {
public:
    explicit TremStrip(FractureProcessor&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    FractureProcessor& proc;
};

// the rhythm, drawn: eight steps you draw with the mouse when the shape is
// Steps, the shape itself across eight cycles when it is anything else, and
// where the rhythm is right now on the right. The same component CRATE has.
class StepEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit StepEditor(FractureProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); setTooltip(tip); }
private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<int> lane() const;
    void setFrom(juce::Point<int>);
    FractureProcessor& proc;
    int dragging = -1;
};

// the XY pad: drag anywhere and both axes move together. Each axis is a matrix
// source, and the pad's edges say what it is routed to
class XYPad : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit XYPad(FractureProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); setTooltip(tip); }
private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> field() const;
    void setFrom(juce::Point<float>);
    FractureProcessor& proc;
    bool dragging = false;
};

// the Filter panel's own display: the live response, and a handle to play it.
// Drag across to move the cutoff, up and down for the resonance; double-click
// puts both back where they started
class FilterView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit FilterView(FractureProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); setTooltip(tip); }
private:
    void timerCallback() override { repaint(); }
    FractureProcessor& proc;
    float startFreqNorm = 0.0f, startQNorm = 0.0f;
    bool dragging = false;
};

// what each performance control moves, in words, read from the matrix
class PerformRoutes : public juce::Component, private juce::Timer {
public:
    explicit PerformRoutes(FractureProcessor&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    FractureProcessor& proc;
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

    static constexpr int designW = 1760, designH = 990;
    bauhaus::Canvas canvas { designW, designH };       // everything is drawn on this

    void paintDesign(juce::Graphics&);                 // both work in design coordinates
    void layoutDesign();
    bool built = false;                                // layout runs once everything exists

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<KnobBox*> knobs;
    // every control, by parameter, so the timer can dim the ones that do
    // nothing right now and give each its tooltip
    struct Control { int param; std::function<void(bool, const juce::String&)> set; juce::String tip; };
    std::vector<Control> controls;
    template <typename B> void reg(const juce::String& id, B* box){
        controls.push_back({ fracture::Params::get().index(id.toStdString()),
                             [box](bool i, const juce::String& t){ box->setState(i, t); }, {} });
    }
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
    std::vector<Panel*> panels;

    Panel* pIn = nullptr; Panel* pSplit = nullptr; Panel* pDrive = nullptr;
    Panel* pCrush = nullptr; Panel* pFilter = nullptr; Panel* pOut = nullptr;
    Panel* pTrem = nullptr; Panel* pMod = nullptr; Panel* pScope = nullptr;
    Panel* pRhythm = nullptr; Panel* pPerform = nullptr;

    std::vector<juce::Component*> inRow, splitRow, crushRow, outRow, fbRow;
    std::vector<juce::Component*> filterTypeRow, filterKnobRow;
    std::vector<juce::Component*> tremHeadRow, tremKnobRow, rhythmRow;
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
    TremStrip* tremStrip = nullptr;
    StepEditor* steps = nullptr;
    XYPad* pad = nullptr;
    FilterView* filterView = nullptr;
    PerformRoutes* routes = nullptr;
    std::vector<juce::Component*> performRow;
    juce::String padTip;
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
