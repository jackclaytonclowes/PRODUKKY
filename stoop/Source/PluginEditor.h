// PluginEditor.h — the same Béton clair the other two boxes wear, laid out as
// the signal runs: pitch, filter and drive, lo-fi, then the vinyl sim, which
// gets the most room and two panels of its own: the compressor's curve with
// what it is doing now, and the record, turning.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Bauhaus.h"
#include "Guide.h"

class KnobBox : public juce::Component {
public:
    KnobBox(StoopProcessor&, const juce::String& paramId, juce::Colour hue);
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();
    void setState(bool idle, const juce::String& tip);   // dimmed or not, and what the tooltip says
    void mouseDown(const juce::MouseEvent&) override;    // on the value: type one in
    bool typeValue(const juce::String& text);            // what Enter does, for the tests too
    juce::String id() const { return info.id; }
    juce::Rectangle<int> valueArea() const { return { 0, dial + 19, getWidth(), 19 }; }
    static constexpr int w = 80, h = 92, dial = 52;
private:
    StoopProcessor& proc;
    const stoop::ParamInfo& info;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    juce::String caption, valueText;
    session::ValueEntry entry;
};

class ChoiceBox : public juce::Component {
public:
    ChoiceBox(StoopProcessor&, const juce::String& paramId, const juce::String& label, int width = 110);
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
    // label: what the button says, if not the parameter's name ("On" for a
    // section's switch, whose panel already says which section)
    ToggleBox(StoopProcessor&, const juce::String& paramId, juce::Colour onColour, int width = 96,
              const juce::String& label = {});
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
    static constexpr int barHeight = 30;
private:
    int no; juce::String title; juce::Colour hue;
};

class Meters : public juce::Component, private juce::Timer {
public:
    explicit Meters(StoopProcessor&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    StoopProcessor& proc;
    float inDb = -60.0f, outDb = -60.0f;
};

// The compressor's static curve at the current Comp, drawn from the same
// arithmetic the DSP uses (VinylComp::curve), with the make-up added: level
// in across, level out up. The dot is where the detector is now, and the bar
// on the right is the gain it is taking off, so the curve, the knob and the
// sound can be read against each other.
class CompView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit CompView(StoopProcessor&);
    void paint(juce::Graphics&) override;
    void setState(bool idle, const juce::String&){ setAlpha(idle ? 0.35f : 1.0f); }
    double shownReduction() const { return shownRed; }   // for the tests
    void refresh();                                       // read the processor (30 times a second)
private:
    void timerCallback() override { refresh(); }
    StoopProcessor& proc;
    double shownRed = 0.0, shownLevel = -100.0;
};

// The record, seen from above: it turns at the speed Speed says (slowed down
// for the eye, an eighth of the real rate, so 33 still reads as a turn), the
// red mark is the scratch that ticks once a revolution, and it sits off
// centre by as much as Wow flutter swings the pitch. Under it, how far off
// speed it is playing right now, in cents.
class RecordView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit RecordView(StoopProcessor&);
    void paint(juce::Graphics&) override;
    void setState(bool idle, const juce::String&){ setAlpha(idle ? 0.35f : 1.0f); }
private:
    void timerCallback() override { repaint(); }
    StoopProcessor& proc;
};

class StoopEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit StoopEditor(StoopProcessor&);
    ~StoopEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    std::unique_ptr<session::SessionBar> sessionBar;   // public for the host-level test
    CompView* compView = nullptr;                      // and these two
    RecordView* recordView = nullptr;
private:
    void timerCallback() override;
    StoopProcessor& proc;
    bauhaus::Look look;
    static constexpr int designW = 1080, designH = 620;
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
    Panel *pIn = nullptr, *pPitch = nullptr, *pFilter = nullptr, *pLofi = nullptr, *pVinyl = nullptr, *pOut = nullptr;
    std::vector<juce::Component*> inRow, pitchRow, filterRow, lofiRow, vinylRow, outRow;
    std::unique_ptr<session::PresetMenu> presetMenu;
    Meters* meters = nullptr;

    template <typename T, typename... A> T* make(A&&... args){
        auto p = std::make_unique<T>(std::forward<A>(args)...);
        T* raw = p.get();
        owned.push_back(std::move(p));
        canvas.addAndMakeVisible(raw);
        return raw;
    }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StoopEditor)
};
