#include "PluginEditor.h"
#include "Presets.h"

using namespace bauhaus;
#include <map>
#include "Relevance.h"
#include "CrateGuide.h"
using namespace crate;

// one line per control, for its tooltip: what it does, in the terms the
// README uses. Controls that do nothing right now say why instead
static juce::String helpFor(const std::string& id){
    static const std::map<std::string, const char*> h = {
        { "inGain", "Level into the box. Hitting the converter is a technique, not a fault" },
        { "mono", "Sum to mono before anything else, as the hardware was" },
        { "machine", "SP: gentle anti-alias, images left in. S900: six-pole filters either side, darker and cleaner" },
        { "tune", "Moves the sample clock, the S900 way. Pitch and tempo stay where they are" },
        { "trick", "The 45-on-33 trick: sample it sped up, play it back down. Lowers the rate and adds the SP's drop-sample grit" },
        { "clock", "The sample clock. 26.04 kHz is the SP; the S900 ran up to 40 kHz" },
        { "bits", "Converter resolution. Six decibels of noise floor per bit" },
        { "compand", "Mu-law companding. Off is linear, which is what both machines stored" },
        { "aa", "How much the anti-alias filter removes. Lower lets more fold back" },
        { "subHz", "Everything below this goes around the converter and the four-pole, clean. Off at the bottom" },
        { "fltFreq", "Cutoff. For low and high pass this is the -3 dB point; for band pass and reject, the centre" },
        { "fltReso", "Resonance. Sings at the pole" },
        { "fltDrive", "Drive into the filter's saturating core" },
        { "fltEnv", "Each hit opens the filter this many octaves above Cutoff, like the SP's outputs 1 and 2" },
        { "fltDecay", "How fast the hit envelope falls back to Cutoff" },
        { "fltShape", "Low pass, band pass, high pass or band reject" },
        { "fltPoles", "Steepness: 2, 4, 6 or 8 poles, 6 dB an octave each" },
        { "fltMix", "The filter against what went into it" },
        { "rhDepth", "How far the rhythm moves the cutoff, in octaves either way" },
        { "rhDiv", "One rhythm cycle per division, locked to the host. Free uses Rate" },
        { "rhRate", "The rhythm's speed when the division is Free" },
        { "rhShape", "Sine, triangle, saws, square, a random value per division, or the eight steps below" },
        { "rhGroove", "Swings every second division late. 66% is a triplet feel" },
        { "rhPhase", "Offsets the right channel's rhythm; 180 is opposite" },
        { "rhGlide", "Rounds the rhythm's edges" },
        { "rhStep", "Draw the eight steps; they play when the shape is Steps" },
        { "swing", "Pulls the off-beats late against the host grid. Needs the transport running" },
        { "grid", "What the swing counts in" },
        { "push", "Moves everything early or late, in milliseconds" },
        { "dust", "Hiss and crackle from the record, added before the converter" },
        { "dustTone", "How bright the dust is" },
        { "mix", "The whole box against the dry signal" },
        { "outGain", "Level out" },
        { "safety", "A limiter that is transparent below -3 dB and never passes 0 dBFS" },
    };
    for (const auto& kv : h) if (id.rfind(kv.first, 0) == 0 && (id == kv.first || kv.first == "rhStep")) return kv.second;
    return {};
}

static juce::String fmtValue(const ParamInfo& p, double v){
    if (p.id == "bits")  return juce::String(v, 1);
    if (p.id == "tune")  return (v > 0 ? "+" : "") + juce::String(v, 1);
    if (p.id == "fltDrive") return juce::String(v, 1) + "x";
    if (p.id == "fltDecay") return juce::String(juce::roundToInt(v)) + " ms";   // a time, not an offset
    if (p.id == "rhRate")   return juce::String(v, v < 10.0 ? 2 : 1) + " Hz";
    if (p.unit == "oct") return std::fabs(v) < 0.05 ? juce::String("off")
                                                     : (v > 0 ? "+" : "") + juce::String(v, 1) + " oct";
    if (p.unit == "deg") return juce::String(juce::roundToInt(v)) + " deg";
    if (p.id == "subHz") return v <= 20.5 ? juce::String("off") : juce::String(juce::roundToInt(v));
    if (p.unit == "Hz")  return v >= 1000.0 ? juce::String(v / 1000.0, v < 10000.0 ? 2 : 1) + "k"
                                            : juce::String(juce::roundToInt(v));
    if (p.unit == "dB")  return (v > 0 ? "+" : "") + juce::String(v, 1);
    if (p.unit == "%")   return juce::String(v, v < 100.0 ? 1 : 0) + "%";
    if (p.unit == "ms")  return (v > 0 ? "+" : "") + juce::String(v, 1);
    if (p.unit == "st")  return (v > 0 ? "+" : "") + juce::String(v, 1);
    return juce::String(v, 2);
}

KnobBox::KnobBox(CrateProcessor& p, const juce::String& paramId, juce::Colour hue)
    : proc(p), info(Params::get()[Params::get().index(paramId.toStdString())])
{
    caption = info.name;
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::degreesToRadians(-145.0f), juce::degreesToRadians(135.0f), true);
    slider.setColour(juce::Slider::rotarySliderFillColourId, hue);
    slider.setDoubleClickReturnValue(true, info.def);
    slider.onValueChange = [this]{ valueText = fmtValue(info, slider.getValue()); repaint(); };
    addAndMakeVisible(slider);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.apvts, info.id, slider);
    valueText = fmtValue(info, slider.getValue());
    addChildComponent(entry);
    setSize(w, h);
}
void KnobBox::resized(){ slider.setBounds((getWidth() - 46) / 2, 0, 46, 46); }
void KnobBox::paint(juce::Graphics& g){
    drawTracked(g, caption, { 0, 50, getWidth(), 11 }, 9.0f, 1.1f,
                juce::Justification::horizontallyCentred, ink);
    g.setColour(dim);
    g.setFont(mono(11.0f));
    g.drawText(valueText, 0, 63, getWidth(), 12, juce::Justification::centred);
}
void KnobBox::setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); slider.setTooltip(tip); }

void KnobBox::mouseDown(const juce::MouseEvent& e){
    if (!valueArea().contains(e.getPosition())) return;
    entry.begin(valueText, valueArea(), mono(11.0f), ink, face,
                [this](const juce::String& t){ return typeValue(t); });
}
// Read in the panel's own units (session::parseTyped), clamped to the range,
// and set inside a gesture so it is one undo step like a drag
bool KnobBox::typeValue(const juce::String& text){
    double v = 0.0;
    if (!session::parseTyped(text, false, v)) return false;
    v = juce::jlimit(static_cast<double>(info.min), static_cast<double>(info.max), v);
    auto* param = proc.apvts.getParameter(info.id);
    if (param == nullptr) return false;
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(v)));
    param->endChangeGesture();
    return true;
}
void ChoiceBox::setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); box.setTooltip(tip); }
void ToggleBox::setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); button.setTooltip(tip); }

void KnobBox::refresh(){
    const juce::String t = fmtValue(info, proc.apvts.getRawParameterValue(info.id)->load());
    if (t != valueText){ valueText = t; repaint(); }
}

ChoiceBox::ChoiceBox(CrateProcessor& p, const juce::String& paramId,
                     const juce::String& label, int width) : caption(label){
    box.setJustificationType(juce::Justification::centredLeft);
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(paramId)))
        box.addItemList(param->choices, 1);
    addAndMakeVisible(box);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, paramId, box);
    setSize(width, 44);
}
void ChoiceBox::resized(){ box.setBounds(0, 14, getWidth(), 30); }
void ChoiceBox::paint(juce::Graphics& g){
    drawTracked(g, caption, { 0, 0, getWidth(), 11 }, 9.0f, 1.6f, juce::Justification::left, dim);
}

ToggleBox::ToggleBox(CrateProcessor& p, const juce::String& paramId,
                     juce::Colour onColour, int width){
    const auto& info = Params::get()[Params::get().index(paramId.toStdString())];
    button.setButtonText(info.name);
    button.setClickingTogglesState(true);
    button.setColour(juce::TextButton::buttonOnColourId, onColour);
    addAndMakeVisible(button);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, paramId, button);
    setSize(width, 44);
}
void ToggleBox::resized(){ button.setBounds(0, 14, getWidth(), 30); }

Panel::Panel(int number, juce::String t, juce::Colour h) : no(number), title(std::move(t)), hue(h) {}
juce::Rectangle<int> Panel::content() const {
    return getLocalBounds().withTrimmedTop(barHeight).reduced(14, 0).withTrimmedBottom(12);
}
void Panel::paint(juce::Graphics& g){
    g.setColour(panelBg); g.fillRect(getLocalBounds());
    g.setColour(hue);     g.fillRect(getLocalBounds().removeFromTop(barHeight));
    const bool light = hue.getPerceivedBrightness() < 0.6f;
    auto bar = getLocalBounds().removeFromTop(barHeight).reduced(10, 0);
    drawTracked(g, juce::String(no).paddedLeft('0', 2), bar.removeFromLeft(20), 9.0f, 1.0f,
                juce::Justification::left, (light ? face : ink).withAlpha(0.65f));
    drawTracked(g, title, bar, 10.0f, 2.2f, juce::Justification::left, light ? face : ink);
    g.setColour(ink); g.drawRect(getLocalBounds(), 2);
}

// --------------------------------------------------------------- feel strip
FeelStrip::FeelStrip(CrateProcessor& p) : proc(p){ startTimerHz(12); }
void FeelStrip::paint(juce::Graphics& g){
    auto area = getLocalBounds();
    g.setColour(face); g.fillRect(area);
    g.setColour(ink);  g.drawRect(area, 2);

    auto body = area.reduced(14, 10);
    auto label = body.removeFromBottom(14);
    auto numbers = body.removeFromTop(12);
    auto lane = body;

    const float swing = proc.apvts.getRawParameterValue("swing")->load();
    const float push = proc.apvts.getRawParameterValue("push")->load();
    const int steps = crate::gridSteps(static_cast<int>(proc.apvts.getRawParameterValue("grid")->load()));
    const int total = steps * 4;                          // one bar
    const double bpm = proc.hostBpm.load();
    const double stepMs = 60000.0 / bpm / steps;
    const double offsetMs = (swing / 100.0 - 0.5) * 2.0 * stepMs;
    const float stepW = lane.getWidth() / static_cast<float>(total);

    // the grid as written, so the displacement has something to be measured
    // against — the whole point of the panel is the gap between the two
    g.setColour(track);
    for (int i = 0; i <= total; ++i)
        g.fillRect(lane.getX() + stepW * i - 0.5f, static_cast<float>(lane.getY()),
                   1.0f, static_cast<float>(lane.getHeight()));

    for (int i = 0; i < total; ++i){
        const bool onBeat = (i % 2) == 0;
        const bool beat = (i % steps) == 0;
        const double ms = (onBeat ? 0.0 : offsetMs) + push;
        const float x = lane.getX() + stepW * i + static_cast<float>(ms / stepMs) * stepW;
        const float hgt = beat ? lane.getHeight() : lane.getHeight() * 0.66f;
        g.setColour(onBeat ? ink : yellow);
        g.fillRect(x - 1.5f, lane.getBottom() - hgt, beat ? 4.0f : 3.0f, hgt);
        if (beat){
            g.setColour(dim);
            g.setFont(mono(9.0f));
            g.drawText(juce::String(i / steps + 1),
                       juce::Rectangle<int>(juce::roundToInt(x) - 10, numbers.getY(), 20, 12),
                       juce::Justification::centred);
        }
    }

    g.setColour(dim);
    g.setFont(mono(10.0f));
    const juce::String text = proc.transportRunning.load()
        ? juce::String(offsetMs, 1) + " ms late off the beat  ·  "
          + juce::String(push, 1) + " ms push  ·  " + juce::String(bpm, 1) + " BPM"
        : juce::String::fromUTF8("transport stopped \xe2\x80\x94 swing follows the host grid, so nothing moves");
    g.drawText(text, label, juce::Justification::centredLeft);
}

Meters::Meters(CrateProcessor& p) : proc(p){ startTimerHz(30); }
void Meters::timerCallback(){
    const auto fall = [](float cur, float target){ return target > cur ? target : cur - 1.5f; };
    inDb  = fall(inDb,  juce::Decibels::gainToDecibels(proc.inPeak.load(), -60.0f));
    outDb = fall(outDb, juce::Decibels::gainToDecibels(proc.outPeak.load(), -60.0f));
    repaint();
}
BitsMeter::BitsMeter(CrateProcessor& p) : proc(p){
    setTooltip("How many of the converter's bits the drums are using. Each 6 dB under full scale "
               "costs one: push Input until it reads near the top for the grain the machines had");
    setSize(84, KnobBox::h);
    startTimerHz(30);
}
void BitsMeter::refresh(){
    const double bits = proc.apvts.getRawParameterValue("bits")->load();
    const float peak = proc.convPeak.load();
    const double now = crate::bitsInUse(bits, peak);
    used = now > used ? now : std::max(now, used - 0.25);     // falls a quarter bit a frame
    if (now >= hold){ hold = now; holdFrames = 45; }          // and the peak waits 1.5 s
    else if (--holdFrames <= 0) hold = std::max(now, hold - 0.25);
    clipping = peak >= 1.0f;
    repaint();
}
void BitsMeter::paint(juce::Graphics& g){
    const double bits = proc.apvts.getRawParameterValue("bits")->load();
    const int cells = juce::jlimit(1, 16, juce::roundToInt(std::ceil(bits)));
    auto a = getLocalBounds();
    drawTracked(g, "Bits in use", a.removeFromTop(11), 8.5f, 1.4f, juce::Justification::left, dim);
    a.removeFromTop(6);
    auto row = a.removeFromTop(30);
    const float cw = row.getWidth() / static_cast<float>(cells);
    for (int k = 0; k < cells; ++k){
        const auto c = juce::Rectangle<float>(row.getX() + k * cw, (float) row.getY(), cw, (float) row.getHeight()).reduced(1.0f, 0.0f);
        g.setColour(face); g.fillRect(c);
        const double fill = juce::jlimit(0.0, 1.0, used - k);
        if (fill > 0.0){
            g.setColour(clipping && k == cells - 1 ? red : yellow);
            g.fillRect(c.withTop(c.getBottom() - static_cast<float>(fill) * c.getHeight()));
        }
        if (hold > k && hold <= k + 1){ g.setColour(ink); g.fillRect(c.withHeight(2.0f)); }
        g.setColour(ink); g.drawRect(c, 1.0f);
    }
    a.removeFromTop(6);
    g.setColour(dim); g.setFont(mono(11.0f));
    g.drawText(juce::String(used, 1) + " / " + juce::String(bits, bits == std::floor(bits) ? 0 : 1)
                   + (clipping ? "  over" : ""), a.removeFromTop(14), juce::Justification::left);
}

void Meters::paint(juce::Graphics& g){
    const char* caps[] = { "IN", "OUT" };
    const float vals[] = { inDb, outDb };
    auto area = getLocalBounds();
    const int each = area.getWidth() / 2;
    for (int i = 0; i < 2; ++i){
        auto col = area.removeFromLeft(each).reduced(5, 0);
        auto num = col.removeFromBottom(13);
        auto cap = col.removeFromBottom(13);
        auto bar = col.withSizeKeepingCentre(18, col.getHeight());
        g.setColour(face); g.fillRect(bar);
        const float norm = juce::jlimit(0.0f, 1.0f, (vals[i] + 60.0f) / 60.0f);
        auto fill = bar.reduced(2).withTrimmedTop(juce::roundToInt((1.0f - norm) * (bar.getHeight() - 4)));
        g.setColour(yellow); g.fillRect(fill);
        if (norm > 0.9f){
            auto over = fill.withTrimmedBottom(juce::roundToInt(fill.getHeight() * (0.9f / norm)));
            g.setColour(red); g.fillRect(over);
        }
        g.setColour(ink); g.drawRect(bar, 2);
        drawTracked(g, caps[i], cap, 9.0f, 1.8f, juce::Justification::horizontallyCentred, ink);
        g.setColour(dim); g.setFont(mono(10.0f));
        g.drawText(vals[i] <= -59.5f ? juce::String("-inf") : juce::String(vals[i], 1),
                   num, juce::Justification::centred);
    }
}

// -------------------------------------------------------------- step editor
StepEditor::StepEditor(CrateProcessor& p) : proc(p){ startTimerHz(24); }

juce::Rectangle<int> StepEditor::lane() const { return getLocalBounds().reduced(10, 8).withTrimmedRight(26); }

void StepEditor::paint(juce::Graphics& g){
    auto area = getLocalBounds();
    g.setColour(face); g.fillRect(area);
    g.setColour(ink);  g.drawRect(area, 2);
    const auto L = lane();
    const int shape = static_cast<int>(proc.apvts.getRawParameterValue("rhShape")->load());
    const bool stepsShape = shape == crate::RhythmMod::Steps;
    const float w = L.getWidth() / 8.0f;

    double stepVals[8];
    for (int k = 0; k < 8; ++k)
        stepVals[k] = proc.apvts.getRawParameterValue("rhStep" + juce::String(k + 1))->load() / 100.0;

    // the bars: solid when they are what plays, faint when another shape is
    for (int k = 0; k < 8; ++k){
        const float hgt = static_cast<float>(stepVals[k]) * L.getHeight();
        g.setColour(stepsShape ? blue : track);
        g.fillRect(L.getX() + k * w + 2.0f, L.getBottom() - hgt, w - 4.0f, hgt);
    }
    // any other shape, drawn across eight cycles from the same arithmetic the DSP uses
    if (!stepsShape){
        crate::RhythmMod r;
        r.configure(1.0, 2.0, shape, proc.apvts.getRawParameterValue("rhGroove")->load(), 0.0, 0.0, stepVals);
        juce::Path path;
        const int n = L.getWidth();
        for (int x = 0; x <= n; ++x){
            const double v = r.shapeAt(8.0 * x / n);
            const float px = static_cast<float>(L.getX() + x), py = L.getBottom() - static_cast<float>(v) * L.getHeight();
            if (x == 0) path.startNewSubPath(px, py); else path.lineTo(px, py);
        }
        g.setColour(blue); g.strokePath(path, juce::PathStrokeType(2.0f));
    }
    // where the rhythm is now
    auto now = getLocalBounds().reduced(8).removeFromRight(14);
    g.setColour(track); g.fillRect(now);
    const float v = juce::jlimit(0.0f, 1.0f, proc.rhythmNow.load());
    g.setColour(yellow);
    g.fillRect(now.withTrimmedTop(juce::roundToInt((1.0f - v) * now.getHeight())));
    g.setColour(ink); g.drawRect(now, 1);
}

void StepEditor::setFrom(juce::Point<int> pos){
    const auto L = lane();
    const int k = juce::jlimit(0, 7, static_cast<int>((pos.x - L.getX()) * 8 / juce::jmax(1, L.getWidth())));
    const float v = juce::jlimit(0.0f, 1.0f, (L.getBottom() - pos.y) / static_cast<float>(juce::jmax(1, L.getHeight())));
    if (k != dragging){
        // the next step's gesture opens before the last one closes, so one
        // stroke across the steps is one undo, not one per step
        if (auto* prm = proc.apvts.getParameter("rhStep" + juce::String(k + 1))) prm->beginChangeGesture();
        if (dragging >= 0)
            if (auto* old = proc.apvts.getParameter("rhStep" + juce::String(dragging + 1))) old->endChangeGesture();
        dragging = k;
    }
    if (auto* prm = proc.apvts.getParameter("rhStep" + juce::String(k + 1)))
        prm->setValueNotifyingHost(prm->convertTo0to1(v * 100.0f));
    repaint();
}
void StepEditor::mouseDown(const juce::MouseEvent& e){ setFrom(e.getPosition()); }
void StepEditor::mouseDrag(const juce::MouseEvent& e){ setFrom(e.getPosition()); }
void StepEditor::mouseUp(const juce::MouseEvent&){
    if (dragging >= 0)
        if (auto* prm = proc.apvts.getParameter("rhStep" + juce::String(dragging + 1))) prm->endChangeGesture();
    dragging = -1;
}

// ------------------------------------------------------------------- editor
static void layoutRow(juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int gap = 12){
    int x = area.getX();
    for (auto* c : items){
        if (c == nullptr) continue;
        c->setBounds(x, area.getY(), c->getWidth(), c->getHeight());
        x += c->getWidth() + gap;
    }
}

CrateEditor::CrateEditor(CrateProcessor& p) : juce::AudioProcessorEditor(&p), proc(p){
    setLookAndFeel(&look);

    canvas.onPaint  = [this](juce::Graphics& g){ paintDesign(g); };
    canvas.onLayout = [this]{ layoutDesign(); };
    addAndMakeVisible(canvas);

    presetMenu = std::make_unique<session::PresetMenu>(proc, proc.userPresets, session::PresetMenu::Hooks {
        [this]{ return proc.userPresetName(); },
        [this](const juce::File& f){ return proc.saveUserPreset(f); },
        [this](const juce::File& f){ return proc.loadUserPreset(f); } }, "CRATE");
    canvas.addAndMakeVisible(presetMenu->box);
    canvas.addAndMakeVisible(presetMenu->saveButton);
    canvas.addAndMakeVisible(presetMenu->prev);
    canvas.addAndMakeVisible(presetMenu->next);
    sessionBar = std::make_unique<session::SessionBar>(*proc.session, yellow);
    canvas.addAndMakeVisible(*sessionBar);
    setWantsKeyboardFocus(true);

    pIn     = make<Panel>(1, "Input", ink);
    pConv   = make<Panel>(2, "Converter", yellow);
    pFilter = make<Panel>(3, "Four-pole", blue);
    pDust   = make<Panel>(4, "Dust", red);
    pOut    = make<Panel>(6, "Out", ink);
    pRhythm = make<Panel>(5, "Rhythm", blue);
    pFeel   = make<Panel>(7, "Feel", ink);

    auto reg = [&](const char* id, auto* box){
        controls.push_back({ Params::get().index(id), [box](bool i, const juce::String& t){ box->setState(i, t); } });
    };
    auto knob = [&](juce::Component* parent, const char* id, juce::Colour hue){
        auto* k = new KnobBox(proc, id, hue);
        owned.emplace_back(k); parent->addAndMakeVisible(k); knobs.push_back(k);
        reg(id, k);
        return static_cast<juce::Component*>(k);
    };
    auto choice = [&](juce::Component* parent, const char* id, const char* label, int width = 110){
        auto* c = new ChoiceBox(proc, id, label, width);
        owned.emplace_back(c); parent->addAndMakeVisible(c);
        reg(id, c);
        return static_cast<juce::Component*>(c);
    };
    auto toggle = [&](juce::Component* parent, const char* id, juce::Colour hue, int width = 96){
        auto* t = new ToggleBox(proc, id, hue, width);
        owned.emplace_back(t); parent->addAndMakeVisible(t);
        reg(id, t);
        return static_cast<juce::Component*>(t);
    };

    inRow     = { knob(pIn, "inGain", ink), toggle(pIn, "mono", ink, 88) };
    convRow   = { choice(pConv, "machine", "Machine", 96), knob(pConv, "tune", yellow),
                  knob(pConv, "trick", yellow), knob(pConv, "clock", yellow),
                  knob(pConv, "bits", yellow), knob(pConv, "compand", yellow),
                  knob(pConv, "aa", yellow), knob(pConv, "subHz", yellow) };
    bitsMeter = new BitsMeter(proc);
    owned.emplace_back(bitsMeter); pConv->addAndMakeVisible(bitsMeter);
    convRow.push_back(bitsMeter);
    filterRow = { choice(pFilter, "fltShape", "Shape", 76), choice(pFilter, "fltPoles", "Poles", 60),
                  knob(pFilter, "fltFreq", blue), knob(pFilter, "fltReso", blue),
                  knob(pFilter, "fltDrive", blue), knob(pFilter, "fltEnv", blue),
                  knob(pFilter, "fltDecay", blue), knob(pFilter, "fltMix", blue) };
    rhythmRow = { knob(pRhythm, "rhDepth", blue), choice(pRhythm, "rhDiv", "Rhythm", 96),
                  knob(pRhythm, "rhRate", blue), choice(pRhythm, "rhShape", "Shape", 100),
                  knob(pRhythm, "rhGroove", blue), knob(pRhythm, "rhPhase", blue),
                  knob(pRhythm, "rhGlide", blue) };
    dustRow   = { knob(pDust, "dust", red), knob(pDust, "dustTone", red) };
    outRow    = { knob(pOut, "mix", ink), knob(pOut, "outGain", ink),
                  toggle(pOut, "safety", red, 88) };
    feelRow   = { knob(pFeel, "swing", ink), choice(pFeel, "grid", "Grid", 96),
                  knob(pFeel, "push", ink) };

    strip = new FeelStrip(proc);
    owned.emplace_back(strip);
    pFeel->addAndMakeVisible(strip);
    steps = new StepEditor(proc);
    owned.emplace_back(steps);
    pRhythm->addAndMakeVisible(steps);
    reg("rhStep1", steps);          // the eight steps are dimmed together
    meters = new Meters(proc);
    owned.emplace_back(meters);
    pOut->addAndMakeVisible(meters);

    setResizable(true, true);
    setResizeLimits(designW * 2 / 5, designH * 2 / 5, designW * 2, designH * 2);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(designW) / designH);
    const auto open = bauhaus::Canvas::openingSize(designW, designH);
    setSize(open.getWidth(), open.getHeight());
    // ---- tooltips and the guide
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "CRATE"; o.folderName = "CRATE";
        o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        prefs = std::make_unique<juce::PropertiesFile>(o);
    }
    for (auto* b : { &tipsButton, &guideButton }){
        b->setClickingTogglesState(true);
        b->setColour(juce::TextButton::buttonOnColourId, ink);
        canvas.addAndMakeVisible(*b);
    }
    tipsButton.setTooltip("Show a note about each control when the mouse rests on it");
    guideButton.setTooltip("How CRATE works, panel by panel");
    tipsButton.onClick = [this]{ setTips(tipsButton.getToggleState()); };
    guideButton.onClick = [this]{ setGuideOpen(guideButton.getToggleState()); };
    setTips(prefs->getBoolValue("tooltips", true));

    updateIdle();                   // so the first paint is already right
    startTimerHz(20);
}
CrateEditor::~CrateEditor(){ setLookAndFeel(nullptr); }

void CrateEditor::setTips(bool on){
    tipsButton.setToggleState(on, juce::dontSendNotification);
    if (on && !tips) tips = std::make_unique<juce::TooltipWindow>(this, 500);
    if (!on) tips.reset();
    if (prefs){ prefs->setValue("tooltips", on); prefs->saveIfNeeded(); }
}

void CrateEditor::setGuideOpen(bool open){
    guideButton.setToggleState(open, juce::dontSendNotification);
    if (open && !guide){
        guide = std::make_unique<bauhaus::GuideOverlay>(
            juce::String::fromUTF8(CrateGuide::GUIDE_md, CrateGuide::GUIDE_mdSize));
        canvas.addAndMakeVisible(*guide);
        guide->setBounds(guideArea);
    }
    if (!open) guide.reset();
}

void CrateEditor::updateIdle(){
    const Params& P = Params::get();
    std::vector<float> v(static_cast<size_t>(P.count()));
    for (int i = 0; i < P.count(); ++i) v[static_cast<size_t>(i)] = proc.apvts.getRawParameterValue(P[i].id)->load();
    const auto idle = idleControls(v.data());
    for (auto& c : controls){
        juce::String why;
        for (const auto& d : idle) if (d.param == c.param){ why = d.why; break; }
        const bool now = why.isNotEmpty();
        const juce::String help = helpFor(P[c.param].id);
        const juce::String tip = now ? "Inactive: " + why + ".  " + help : help;
        if (tip != c.tip){ c.set(now, tip); c.tip = tip; c.idle = now; }   // only on a change
    }
}

void CrateEditor::timerCallback(){
    for (auto* k : knobs) k->refresh();
    updateIdle();
    presetMenu->sync();
}

bool CrateEditor::keyPressed(const juce::KeyPress& key){
    if (!session::undoKeys(key, *proc.session)) return false;
    sessionBar->refresh();
    return true;
}

void CrateEditor::paint(juce::Graphics& g){
    g.fillAll(ground);                                  // behind the canvas, if it is letterboxed
}

void CrateEditor::paintDesign(juce::Graphics& g){
    g.fillAll(ground);
    auto area = juce::Rectangle<int>(0, 0, designW, designH).reduced(20, 18);
    auto header = area.removeFromTop(44);
    auto marks = header.removeFromLeft(96);
    g.setColour(ink); g.fillRect(marks.removeFromLeft(26).withSizeKeepingCentre(26, 26));
    marks.removeFromLeft(8);
    g.setColour(yellow); g.fillEllipse(marks.removeFromLeft(26).withSizeKeepingCentre(26, 26).toFloat());
    marks.removeFromLeft(8);
    {
        auto q = marks.removeFromLeft(26).withSizeKeepingCentre(26, 26).toFloat();
        juce::Path corner;
        corner.startNewSubPath(q.getX(), q.getBottom());
        corner.lineTo(q.getX(), q.getY() + q.getHeight() * 0.5f);
        corner.addArc(q.getX(), q.getY(), q.getWidth(), q.getHeight(),
                      juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi, false);
        corner.lineTo(q.getRight(), q.getBottom());
        corner.closeSubPath();
        g.setColour(red); g.fillPath(corner);
    }
    header.removeFromLeft(14);
    g.setColour(ink);
    g.setFont(grot(30.0f, true));
    {
        // the version beside the name, as in FRACTURE
        const auto titleRow = header.removeFromTop(30);
        g.drawText("CRATE", titleRow, juce::Justification::topLeft);
        const int nameW = juce::roundToInt(juce::GlyphArrangement::getStringWidth(grot(30.0f, true), "CRATE"));
        g.setColour(dim);
        g.setFont(grot(11.0f, true));
        g.drawText("v" JucePlugin_VersionString, titleRow.withTrimmedLeft(nameW + 8).withTrimmedBottom(5),
                   juce::Justification::bottomLeft);
    }
    drawTracked(g, "Twelve-bit drum processor", header, 9.0f, 2.6f, juce::Justification::left, dim);

    area.removeFromTop(10);
    auto ribbon = area.removeFromTop(8);
    g.setColour(ink);    g.fillRect(ribbon.removeFromRight(50));
    g.setColour(blue);   g.fillRect(ribbon.removeFromRight(100));
    g.setColour(red);    g.fillRect(ribbon.removeFromRight(150));
    g.setColour(yellow); g.fillRect(ribbon);
}

void CrateEditor::resized(){
    canvas.fitInto(getLocalBounds());
}

void CrateEditor::layoutDesign(){
    auto area = juce::Rectangle<int>(0, 0, designW, designH).reduced(20, 18);
    auto header = area.removeFromTop(44);
    presetMenu->saveButton.setBounds(header.removeFromRight(56).withSizeKeepingCentre(56, 30));
    header.removeFromRight(4);
    // previous, the menu, next: presets one click apart without opening the list
    presetMenu->next.setBounds(header.removeFromRight(28).withSizeKeepingCentre(28, 30));
    header.removeFromRight(2);
    presetMenu->box.setBounds(header.removeFromRight(200).withSizeKeepingCentre(200, 30));
    header.removeFromRight(2);
    presetMenu->prev.setBounds(header.removeFromRight(28).withSizeKeepingCentre(28, 30));
    header.removeFromRight(10);
    // narrower than FRACTURE's: the header is 1080 wide, and the undo strip
    // has to clear the subtitle
    guideButton.setBounds(header.removeFromRight(64).withSizeKeepingCentre(64, 30));
    header.removeFromRight(6);
    tipsButton.setBounds(header.removeFromRight(56).withSizeKeepingCentre(56, 30));
    header.removeFromRight(14);
    sessionBar->setBounds(header.removeFromRight(session::SessionBar::preferredWidth)
                              .withSizeKeepingCentre(session::SessionBar::preferredWidth, 30));
    area.removeFromTop(10);
    area.removeFromTop(8);
    area.removeFromTop(12);
    guideArea = area;               // the guide covers everything under the header
    if (guide) guide->setBounds(guideArea);

    const int gap = 14;
    const int col = (area.getWidth() - 11 * gap) / 12;
    const auto cols = [&](int n){ return n * col + (n - 1) * gap; };

    auto rowA = area.removeFromTop(132);
    pIn->setBounds(rowA.removeFromLeft(cols(3)));
    rowA.removeFromLeft(gap);
    pConv->setBounds(rowA.removeFromLeft(cols(9)));
    layoutRow(pIn->content().withHeight(KnobBox::h), inRow);
    layoutRow(pConv->content().withHeight(KnobBox::h), convRow);

    area.removeFromTop(gap);
    auto rowB = area.removeFromTop(132);
    pFilter->setBounds(rowB.removeFromLeft(cols(9)));
    rowB.removeFromLeft(gap);
    pDust->setBounds(rowB.removeFromLeft(cols(3)));
    layoutRow(pFilter->content().withHeight(KnobBox::h), filterRow);
    layoutRow(pDust->content().withHeight(KnobBox::h), dustRow);

    area.removeFromTop(gap);
    auto rowC = area.removeFromTop(190);
    pRhythm->setBounds(rowC.removeFromLeft(cols(8)));
    rowC.removeFromLeft(gap);
    pOut->setBounds(rowC.removeFromLeft(cols(4)));
    {
        auto inner = pRhythm->content();
        layoutRow(inner.removeFromTop(KnobBox::h), rhythmRow);
        inner.removeFromTop(8);
        steps->setBounds(inner);
    }
    {
        auto inner = pOut->content();
        meters->setBounds(inner.removeFromRight(64));
        inner.removeFromRight(6);
        layoutRow(inner.withHeight(KnobBox::h), outRow, 8);
    }

    area.removeFromTop(gap);
    pFeel->setBounds(area);
    {
        auto inner = pFeel->content();
        layoutRow(inner.removeFromTop(KnobBox::h), feelRow);
        inner.removeFromTop(8);
        strip->setBounds(inner);
    }
}
