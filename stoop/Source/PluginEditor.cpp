#include "PluginEditor.h"
#include "Presets.h"

using namespace bauhaus;
#include <map>
#include "Relevance.h"
#include "StoopGuide.h"
using namespace stoop;

// one line per control, for its tooltip: what it does, in the terms the
// README uses. Controls that do nothing right now say why instead
static juce::String helpFor(const std::string& id){
    static const std::map<std::string, const char*> h = {
        { "inGain", "Level into the box. The compressor and the drive both answer to it" },
        { "mono", "Sum to mono before anything else" },
        { "pitchOn", "The pitch shifter, on or off. Every switch fades over 10 ms rather than clicking" },
        { "pitch", "Shifts the pitch up to two octaves either way, and leaves the tempo alone" },
        { "pitchFb", "Sends the shifted sound back through the shifter, so it climbs (or falls) again, quieter each time" },
        { "pitchBal", "The shifted sound against the original. 100% is only the shifted" },
        { "fltOn", "Filter and drive, on or off" },
        { "fltType", "Low pass, band pass or high pass, two poles" },
        { "fltFreq", "Cutoff. For low and high pass, the -3 dB point; for band pass, the centre" },
        { "fltReso", "Resonance: a peak at the cutoff, up to a Q of 20" },
        { "fltDrive", "Overdrive after the filter. Clean at 0; half of it is heard as level, half as grit" },
        { "lofiOn", "Lo-fi, on or off" },
        { "lofiRate", "The sample clock, with no filter in front of it, so what is above half of it folds back as grit" },
        { "lofiBits", "Resolution. Six decibels of noise floor per bit" },
        { "vinylOn", "The vinyl simulator, on or off" },
        { "comp", "The original's vinyl compression: one knob for threshold and ratio together, with automatic make-up, so the quiet parts come up to the loud ones" },
        { "noise", "The record's surface: hiss, crackle, rumble and a scratch that ticks once a revolution. It sits on top of the compression, so it never pumps" },
        { "wow", "The turntable: a wobble once a revolution, a slow drift and a fast flutter. At full the pitch swings 1% either way" },
        { "speed", "33, 45 or 78: how often the wow comes round, and the scratch with it" },
        { "mix", "The whole box against the dry signal" },
        { "outGain", "Level out" },
        { "safety", "A limiter that is transparent below -3 dB and never passes 0 dBFS" },
    };
    const auto it = h.find(id);
    return it == h.end() ? juce::String() : juce::String::fromUTF8(it->second);
}

static juce::String fmtValue(const ParamInfo& p, double v){
    if (p.id == "lofiBits") return juce::String(v, 1);
    if (p.unit == "st")  return std::fabs(v) < 0.05 ? juce::String("0 st") : (v > 0 ? "+" : "") + juce::String(v, 1) + " st";
    if (p.unit == "Hz")  return v >= 1000.0 ? juce::String(v / 1000.0, v < 10000.0 ? 2 : 1) + "k"
                                            : juce::String(juce::roundToInt(v));
    if (p.unit == "dB")  return (v > 0 ? "+" : "") + juce::String(v, 1);
    if (p.unit == "%")   return juce::String(v, v < 100.0 ? 1 : 0) + "%";
    return juce::String(v, 2);
}

KnobBox::KnobBox(StoopProcessor& p, const juce::String& paramId, juce::Colour hue)
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
void KnobBox::resized(){ slider.setBounds((getWidth() - dial) / 2, 0, dial, dial); }
// how wide drawTracked will draw a caption, so a long one ("Wow flutter")
// can close its spacing up to fit under the knob instead of being cut off
static float trackedWidth(const juce::String& text, float height, float tracking){
    const auto font = grot(height, true);
    const auto up = text.toUpperCase();
    float total = 0.0f;
    for (int i = 0; i < up.length(); ++i)
        total += juce::GlyphArrangement::getStringWidth(font, juce::String::charToString(up[i])) + tracking;
    return total;
}

void KnobBox::paint(juce::Graphics& g){
    float tracking = 1.1f;
    while (tracking > -0.4f && trackedWidth(caption, 10.5f, tracking) > getWidth() - 2) tracking -= 0.25f;
    drawTracked(g, caption, { 0, dial + 4, getWidth(), 13 }, 10.5f, tracking,
                juce::Justification::horizontallyCentred, ink);
    g.setColour(dim);
    g.setFont(mono(12.5f));
    g.drawText(valueText, valueArea().withSizeKeepingCentre(getWidth(), 15), juce::Justification::centred);
}
void KnobBox::setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); slider.setTooltip(tip); }

void KnobBox::mouseDown(const juce::MouseEvent& e){
    if (!valueArea().contains(e.getPosition())) return;
    entry.begin(valueText, valueArea(), mono(12.5f), ink, face,
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

ChoiceBox::ChoiceBox(StoopProcessor& p, const juce::String& paramId,
                     const juce::String& label, int width) : caption(label){
    box.setJustificationType(juce::Justification::centredLeft);
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(paramId)))
        box.addItemList(param->choices, 1);
    addAndMakeVisible(box);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, paramId, box);
    setSize(width, 48);
}
void ChoiceBox::resized(){ box.setBounds(0, 16, getWidth(), 32); }
void ChoiceBox::paint(juce::Graphics& g){
    drawTracked(g, caption, { 0, 0, getWidth(), 13 }, 10.5f, 1.4f, juce::Justification::left, dim);
}

ToggleBox::ToggleBox(StoopProcessor& p, const juce::String& paramId,
                     juce::Colour onColour, int width, const juce::String& label){
    const auto& info = Params::get()[Params::get().index(paramId.toStdString())];
    button.setButtonText(label.isNotEmpty() ? label : juce::String(info.name));
    button.setClickingTogglesState(true);
    button.setColour(juce::TextButton::buttonOnColourId, onColour);
    addAndMakeVisible(button);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, paramId, button);
    setSize(width, 48);
}
void ToggleBox::resized(){ button.setBounds(0, 16, getWidth(), 32); }

Panel::Panel(int number, juce::String t, juce::Colour h) : no(number), title(std::move(t)), hue(h) {}
juce::Rectangle<int> Panel::content() const {
    return getLocalBounds().withTrimmedTop(barHeight + 8).reduced(14, 0).withTrimmedBottom(10);
}
void Panel::paint(juce::Graphics& g){
    g.setColour(panelBg); g.fillRect(getLocalBounds());
    g.setColour(hue);     g.fillRect(getLocalBounds().removeFromTop(barHeight));
    const bool light = hue.getPerceivedBrightness() < 0.6f;
    auto bar = getLocalBounds().removeFromTop(barHeight).reduced(10, 0);
    drawTracked(g, juce::String(no).paddedLeft('0', 2), bar.removeFromLeft(24), 10.5f, 1.0f,
                juce::Justification::left, (light ? face : ink).withAlpha(0.65f));
    drawTracked(g, title, bar, 12.5f, 2.0f, juce::Justification::left, light ? face : ink);
    g.setColour(ink); g.drawRect(getLocalBounds(), 2);
}

Meters::Meters(StoopProcessor& p) : proc(p){ startTimerHz(30); }
void Meters::timerCallback(){
    const auto fall = [](float cur, float target){ return target > cur ? target : cur - 1.5f; };
    inDb  = fall(inDb,  juce::Decibels::gainToDecibels(proc.inPeak.load(), -60.0f));
    outDb = fall(outDb, juce::Decibels::gainToDecibels(proc.outPeak.load(), -60.0f));
    repaint();
}
void Meters::paint(juce::Graphics& g){
    const char* caps[] = { "IN", "OUT" };
    const float vals[] = { inDb, outDb };
    auto area = getLocalBounds();
    const int each = area.getWidth() / 2;
    for (int i = 0; i < 2; ++i){
        auto col = area.removeFromLeft(each).reduced(2, 0);
        auto num = col.removeFromBottom(15);
        auto cap = col.removeFromBottom(15);
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
        drawTracked(g, caps[i], cap, 10.5f, 1.6f, juce::Justification::horizontallyCentred, ink);
        g.setColour(dim); g.setFont(mono(11.5f));
        g.drawText(vals[i] <= -59.5f ? juce::String("-inf") : juce::String(vals[i], 1),
                   num, juce::Justification::centred);
    }
}

static void layoutRow(juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int gap = 12){
    int x = area.getX();
    for (auto* c : items){
        if (c == nullptr) continue;
        c->setBounds(x, area.getY(), c->getWidth(), c->getHeight());
        x += c->getWidth() + gap;
    }
}
// the same row with the panel's spare width shared out between the controls,
// so a panel is filled across rather than bunched up at its left (as CRATE)
static void spreadRow(juce::Rectangle<int> area, const std::vector<juce::Component*>& items){
    int used = 0, n = 0;
    for (auto* c : items) if (c != nullptr){ used += c->getWidth(); ++n; }
    const int gap = n > 1 ? juce::jlimit(8, 36, (area.getWidth() - used) / (n - 1)) : 0;
    layoutRow(area, items, gap);
}
static int rowWidth(const std::vector<juce::Component*>& items, int gap = 8){
    int w = 0, n = 0;
    for (auto* c : items) if (c != nullptr){ w += c->getWidth(); ++n; }
    return w + gap * juce::jmax(0, n - 1);
}

// ----------------------------------------------------------- the compressor
CompView::CompView(StoopProcessor& p) : proc(p){
    setTooltip("The compressor's curve at the current Comp, make-up included: level in across, level out up. "
               "The dot is where it is now; the bar is the gain it is taking off");
    startTimerHz(30);
}
void CompView::refresh(){
    const double red = proc.reduction.load();
    shownRed = red > shownRed ? red : std::max(red, shownRed - 0.6);    // falls 18 dB a second
    shownLevel = proc.levelDb.load();
    repaint();
}
void CompView::paint(juce::Graphics& g){
    auto area = getLocalBounds();
    g.setColour(face); g.fillRect(area);
    g.setColour(ink);  g.drawRect(area, 2);
    auto body = area.reduced(10, 8);
    auto text = body.removeFromBottom(16);
    body.removeFromBottom(4);
    auto bar = body.removeFromRight(24);
    body.removeFromRight(10);
    const int side = juce::jmin(body.getWidth(), body.getHeight());
    auto plot = body.removeFromLeft(side).withSizeKeepingCentre(side, side);
    auto readout = body.reduced(8, 0);

    const double c = proc.apvts.getRawParameterValue("comp")->load() / 100.0;
    const bool on = proc.apvts.getRawParameterValue("vinylOn")->load() > 0.5f;
    const double lo = -48.0, hi = 0.0;
    auto px = [&](double db){ return plot.getX() + static_cast<float>((db - lo) / (hi - lo)) * plot.getWidth(); };
    auto py = [&](double db){ return plot.getBottom() - static_cast<float>((juce::jlimit(lo, hi, db) - lo) / (hi - lo)) * plot.getHeight(); };

    g.setColour(track);
    for (double d = lo; d <= hi + 0.1; d += 12.0){
        g.fillRect(px(d) - 0.5f, static_cast<float>(plot.getY()), 1.0f, static_cast<float>(plot.getHeight()));
        g.fillRect(static_cast<float>(plot.getX()), py(d) - 0.5f, static_cast<float>(plot.getWidth()), 1.0f);
    }
    g.setColour(dim2);                                   // unity, for comparison
    g.drawLine(px(lo), py(lo), px(hi), py(hi), 1.0f);
    const double makeup = c > 0.0 ? VinylComp::makeupDb(c) : 0.0;
    juce::Path curve;
    for (int k = 0; k <= plot.getWidth(); ++k){
        const double in = lo + (hi - lo) * k / plot.getWidth();
        const double out = (c > 0.0 ? VinylComp::curve(in, c) : in) + makeup;
        if (k == 0) curve.startNewSubPath(px(in), py(out)); else curve.lineTo(px(in), py(out));
    }
    g.saveState();
    g.reduceClipRegion(plot);
    g.setColour(red);
    g.strokePath(curve, juce::PathStrokeType(2.0f));
    if (c > 0.0){                                        // the threshold
        const double t = VinylComp::thresholdDb(c);
        g.setColour(ink.withAlpha(0.6f));
        g.fillRect(px(t) - 0.75f, static_cast<float>(plot.getBottom() - 8), 1.5f, 8.0f);
    }
    if (on && shownLevel > lo){                          // where it is now
        const double out = (c > 0.0 ? VinylComp::curve(shownLevel, c) : shownLevel) + makeup;
        g.setColour(ink);
        g.fillEllipse(px(shownLevel) - 4.0f, py(out) - 4.0f, 8.0f, 8.0f);
    }
    g.restoreState();
    g.setColour(ink); g.drawRect(plot, 1);

    // gain reduction, from the top, 0 to 30 dB
    g.setColour(face.darker(0.04f)); g.fillRect(bar);
    const float frac = static_cast<float>(juce::jlimit(0.0, 1.0, shownRed / 30.0));
    g.setColour(red);
    g.fillRect(bar.withHeight(juce::roundToInt(frac * bar.getHeight())));
    g.setColour(ink); g.drawRect(bar, 1);

    // what the knob means, in numbers
    auto line = [&](const juce::String& cap, const juce::String& val){
        auto r = readout.removeFromTop(36);
        drawTracked(g, cap, r.removeFromTop(13), 10.0f, 1.2f, juce::Justification::left, dim);
        g.setColour(ink); g.setFont(mono(14.0f));
        g.drawText(val, r, juce::Justification::left);
    };
    if (c > 0.0){
        line("Threshold", juce::String(VinylComp::thresholdDb(c), 1) + " dB");
        line("Ratio", juce::String(VinylComp::ratio(c), 1) + ":1");
        line("Make-up", "+" + juce::String(makeup, 1) + " dB");
    } else line("Comp", "off");
    line("Taking off", juce::String(-shownRed, 1) + " dB");
    g.setColour(dim); g.setFont(mono(11.5f));
    g.drawText(juce::String::fromUTF8("in \xe2\x86\x92   attack 3 ms \xc2\xb7 release 120 ms"), text, juce::Justification::left);
}

// ---------------------------------------------------------------- the record
RecordView::RecordView(StoopProcessor& p) : proc(p){
    setTooltip("The record: it turns at the Speed you set, the red mark is the scratch that ticks once a "
               "revolution, and it sits off centre by as much as Wow flutter swings the pitch");
    startTimerHz(30);
}
void RecordView::paint(juce::Graphics& g){
    auto area = getLocalBounds();
    g.setColour(face); g.fillRect(area);
    g.setColour(ink);  g.drawRect(area, 2);
    auto body = area.reduced(10, 8);
    auto text = body.removeFromBottom(16);
    body.removeFromBottom(4);
    const float R = juce::jmin(body.getWidth(), body.getHeight()) * 0.5f - 4.0f;
    const auto spindle = body.getCentre().toFloat();

    const double rev = proc.revolution.load();
    const double w = proc.apvts.getRawParameterValue("wow")->load() / 100.0;
    const double nz = proc.apvts.getRawParameterValue("noise")->load() / 100.0;
    const bool on = proc.apvts.getRawParameterValue("vinylOn")->load() > 0.5f;
    const float turn = static_cast<float>(2.0 * juce::MathConstants<double>::pi * rev);
    // the disc's centre orbits the spindle: an off-centre hole is what wow is
    const float off = static_cast<float>(on ? w : 0.0) * R * 0.06f;
    const juce::Point<float> centre { spindle.x + off * std::sin(turn), spindle.y - off * std::cos(turn) };

    g.setColour(ink);
    g.fillEllipse(centre.x - R, centre.y - R, 2.0f * R, 2.0f * R);
    g.setColour(juce::Colour(0xff3a362e));               // the grooves
    for (float r = R * 0.42f; r < R * 0.96f; r += R * 0.09f)
        g.drawEllipse(centre.x - r, centre.y - r, 2.0f * r, 2.0f * r, 1.0f);
    const float lr = R * 0.33f;                          // the label, and the spindle through it
    g.setColour(yellow);
    g.fillEllipse(centre.x - lr, centre.y - lr, 2.0f * lr, 2.0f * lr);
    {
        juce::Path stripe;                               // so the label can be seen to turn
        stripe.addRectangle(-lr, -1.5f, 2.0f * lr, 3.0f);
        g.setColour(ink.withAlpha(0.7f));
        g.fillPath(stripe, juce::AffineTransform::rotation(turn).translated(centre));
    }
    g.setColour(ground);
    g.fillEllipse(spindle.x - 3.0f, spindle.y - 3.0f, 6.0f, 6.0f);
    // the scratch: under the needle (at the top) when the tick sounds
    if (nz > 0.0 || !on){
        const float a = static_cast<float>(2.0 * juce::MathConstants<double>::pi * (rev - stoop::Turntable::scratchAngle));
        juce::Path scratch;
        scratch.addRectangle(-1.5f, -R * 0.94f, 3.0f, R * 0.30f);
        g.setColour(red.withAlpha(static_cast<float>(juce::jlimit(0.35, 1.0, 0.35 + nz))));
        g.fillPath(scratch, juce::AffineTransform::rotation(a).translated(centre));
    }
    // the needle
    g.setColour(ink);
    g.fillRect(spindle.x - 1.5f, spindle.y - R - 6.0f, 3.0f, 12.0f);

    const auto speeds = juce::StringArray { "33", "45", "78" };
    const int s = juce::jlimit(0, 2, static_cast<int>(proc.apvts.getRawParameterValue("speed")->load()));
    const double ct = on ? proc.cents.load() : 0.0;
    g.setColour(dim); g.setFont(mono(11.5f));
    g.drawText(speeds[s] + " rpm   " + (ct >= 0.0 ? "+" : "") + juce::String(ct, 1) + " cents off speed",
               text, juce::Justification::centred);
}

// ------------------------------------------------------------------- editor
StoopEditor::StoopEditor(StoopProcessor& p) : juce::AudioProcessorEditor(&p), proc(p){
    setLookAndFeel(&look);

    canvas.onPaint  = [this](juce::Graphics& g){ paintDesign(g); };
    canvas.onLayout = [this]{ layoutDesign(); };
    addAndMakeVisible(canvas);

    presetMenu = std::make_unique<session::PresetMenu>(proc, proc.userPresets, session::PresetMenu::Hooks {
        [this]{ return proc.userPresetName(); },
        [this](const juce::File& f){ return proc.saveUserPreset(f); },
        [this](const juce::File& f){ return proc.loadUserPreset(f); },
        [](int i){                                         // the menu's headings
            const auto& all = stoop::presets();
            return i >= 0 && i < static_cast<int>(all.size())
                 ? juce::String::fromUTF8(stoop::presetCategory(all[static_cast<size_t>(i)].name)) : juce::String(); } }, "STOOP");
    canvas.addAndMakeVisible(presetMenu->box);
    canvas.addAndMakeVisible(presetMenu->saveButton);
    canvas.addAndMakeVisible(presetMenu->prev);
    canvas.addAndMakeVisible(presetMenu->next);
    sessionBar = std::make_unique<session::SessionBar>(*proc.session, yellow);
    canvas.addAndMakeVisible(*sessionBar);
    setWantsKeyboardFocus(true);

    pIn     = make<Panel>(1, "Input", ink);
    pPitch  = make<Panel>(2, "Pitch", blue);
    pFilter = make<Panel>(3, "Filter + drive", blue);
    pVinyl  = make<Panel>(4, "Vinyl sim", yellow);
    pLofi   = make<Panel>(5, "Lo-fi", red);
    pOut    = make<Panel>(6, "Out", ink);

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
    auto toggle = [&](juce::Component* parent, const char* id, juce::Colour hue, int width = 96, const char* label = ""){
        auto* t = new ToggleBox(proc, id, hue, width, label);
        owned.emplace_back(t); parent->addAndMakeVisible(t);
        reg(id, t);
        return static_cast<juce::Component*>(t);
    };

    inRow     = { knob(pIn, "inGain", ink), toggle(pIn, "mono", ink, 84) };
    pitchRow  = { toggle(pPitch, "pitchOn", blue, 64, "On"), knob(pPitch, "pitch", blue),
                  knob(pPitch, "pitchFb", blue), knob(pPitch, "pitchBal", blue) };
    filterRow = { toggle(pFilter, "fltOn", blue, 64, "On"), choice(pFilter, "fltType", "Type", 72),
                  knob(pFilter, "fltFreq", blue), knob(pFilter, "fltReso", blue), knob(pFilter, "fltDrive", yellow) };
    vinylRow  = { toggle(pVinyl, "vinylOn", yellow, 64, "On"), knob(pVinyl, "comp", red),
                  knob(pVinyl, "noise", yellow), knob(pVinyl, "wow", blue), choice(pVinyl, "speed", "Speed", 108) };
    lofiRow   = { toggle(pLofi, "lofiOn", red, 64, "On"), knob(pLofi, "lofiRate", red), knob(pLofi, "lofiBits", red) };
    outRow    = { knob(pOut, "mix", ink), knob(pOut, "outGain", ink), toggle(pOut, "safety", red, 116) };

    compView = new CompView(proc);
    owned.emplace_back(compView); pVinyl->addAndMakeVisible(compView);
    reg("comp", compView);                      // fades with Comp when the section is off
    recordView = new RecordView(proc);
    owned.emplace_back(recordView); pVinyl->addAndMakeVisible(recordView);
    reg("wow", recordView);
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
        o.applicationName = "STOOP"; o.folderName = "STOOP";
        o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        prefs = std::make_unique<juce::PropertiesFile>(o);
    }
    for (auto* b : { &tipsButton, &guideButton }){
        b->setClickingTogglesState(true);
        b->setColour(juce::TextButton::buttonOnColourId, ink);
        canvas.addAndMakeVisible(*b);
    }
    tipsButton.setTooltip("Show a note about each control when the mouse rests on it");
    guideButton.setTooltip("How STOOP works, panel by panel");
    tipsButton.onClick = [this]{ setTips(tipsButton.getToggleState()); };
    guideButton.onClick = [this]{ setGuideOpen(guideButton.getToggleState()); };
    setTips(prefs->getBoolValue("tooltips", true));

    updateIdle();                   // so the first paint is already right
    startTimerHz(20);
}
StoopEditor::~StoopEditor(){ setLookAndFeel(nullptr); }

void StoopEditor::setTips(bool on){
    tipsButton.setToggleState(on, juce::dontSendNotification);
    if (on && !tips) tips = std::make_unique<juce::TooltipWindow>(this, 500);
    if (!on) tips.reset();
    if (prefs){ prefs->setValue("tooltips", on); prefs->saveIfNeeded(); }
}

void StoopEditor::setGuideOpen(bool open){
    guideButton.setToggleState(open, juce::dontSendNotification);
    if (open && !guide){
        guide = std::make_unique<bauhaus::GuideOverlay>(
            juce::String::fromUTF8(StoopGuide::GUIDE_md, StoopGuide::GUIDE_mdSize));
        canvas.addAndMakeVisible(*guide);
        guide->setBounds(guideArea);
    }
    if (!open) guide.reset();
}

void StoopEditor::updateIdle(){
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

void StoopEditor::timerCallback(){
    for (auto* k : knobs) k->refresh();
    updateIdle();
    presetMenu->sync();
}

bool StoopEditor::keyPressed(const juce::KeyPress& key){
    if (!session::undoKeys(key, *proc.session)) return false;
    sessionBar->refresh();
    return true;
}

void StoopEditor::paint(juce::Graphics& g){
    g.fillAll(ground);                                  // behind the canvas, if it is letterboxed
}

void StoopEditor::paintDesign(juce::Graphics& g){
    g.fillAll(ground);
    auto area = juce::Rectangle<int>(0, 0, designW, designH).reduced(20, 18);
    auto header = area.removeFromTop(46);
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
    g.setFont(grot(34.0f, true));
    {
        const auto titleRow = header.removeFromTop(33);
        g.drawText("STOOP", titleRow, juce::Justification::topLeft);
        const int nameW = juce::roundToInt(juce::GlyphArrangement::getStringWidth(grot(34.0f, true), "STOOP"));
        g.setColour(dim);
        g.setFont(grot(12.5f, true));
        g.drawText("v" JucePlugin_VersionString, titleRow.withTrimmedLeft(nameW + 8).withTrimmedBottom(5),
                   juce::Justification::bottomLeft);
    }
    drawTracked(g, "Sampler effects and vinyl", header, 10.5f, 1.4f, juce::Justification::left, dim);

    area.removeFromTop(10);
    auto ribbon = area.removeFromTop(8);
    g.setColour(ink);    g.fillRect(ribbon.removeFromRight(50));
    g.setColour(red);    g.fillRect(ribbon.removeFromRight(100));
    g.setColour(blue);   g.fillRect(ribbon.removeFromRight(150));
    g.setColour(yellow); g.fillRect(ribbon);
}

void StoopEditor::resized(){
    canvas.fitInto(getLocalBounds());
}

void StoopEditor::layoutDesign(){
    auto area = juce::Rectangle<int>(0, 0, designW, designH).reduced(20, 18);
    auto header = area.removeFromTop(46);
    presetMenu->saveButton.setBounds(header.removeFromRight(60).withSizeKeepingCentre(60, 32));
    header.removeFromRight(4);
    presetMenu->next.setBounds(header.removeFromRight(30).withSizeKeepingCentre(30, 32));
    header.removeFromRight(2);
    presetMenu->box.setBounds(header.removeFromRight(200).withSizeKeepingCentre(200, 32));
    header.removeFromRight(2);
    presetMenu->prev.setBounds(header.removeFromRight(30).withSizeKeepingCentre(30, 32));
    header.removeFromRight(10);
    guideButton.setBounds(header.removeFromRight(68).withSizeKeepingCentre(68, 32));
    header.removeFromRight(6);
    tipsButton.setBounds(header.removeFromRight(58).withSizeKeepingCentre(58, 32));
    header.removeFromRight(14);
    sessionBar->setBounds(header.removeFromRight(session::SessionBar::preferredWidth)
                              .withSizeKeepingCentre(session::SessionBar::preferredWidth, 32));
    area.removeFromTop(10);
    area.removeFromTop(8);
    area.removeFromTop(12);
    guideArea = area;               // the guide covers everything under the header
    if (guide) guide->setBounds(guideArea);

    // Panels as wide as their controls need, the spare width shared out, and
    // the controls spread across each panel, as in CRATE
    const int gap = 14, pad = 28;
    const int knobRow = Panel::barHeight + 8 + KnobBox::h + 10;

    // ---- row 1: input, pitch, filter + drive
    auto rowA = area.removeFromTop(knobRow);
    const int wIn = rowWidth(inRow, 10) + pad, wPitch = rowWidth(pitchRow) + pad, wFilter = rowWidth(filterRow) + pad;
    const int spare = juce::jmax(0, rowA.getWidth() - 2 * gap - wIn - wPitch - wFilter);
    pIn->setBounds(rowA.removeFromLeft(wIn));
    rowA.removeFromLeft(gap);
    pPitch->setBounds(rowA.removeFromLeft(wPitch + spare / 2));
    rowA.removeFromLeft(gap);
    pFilter->setBounds(rowA);
    layoutRow(pIn->content().withHeight(KnobBox::h), inRow, 10);
    spreadRow(pPitch->content().withHeight(KnobBox::h), pitchRow);
    spreadRow(pFilter->content().withHeight(KnobBox::h), filterRow);

    // ---- row 2: the vinyl sim, and beside it lo-fi over out
    area.removeFromTop(gap);
    auto rightCol = area.removeFromRight(2 * KnobBox::w + 10 + 12 + 80 + pad);
    area.removeFromRight(gap);
    pLofi->setBounds(rightCol.removeFromTop(knobRow));
    rightCol.removeFromTop(gap);
    pOut->setBounds(rightCol);
    spreadRow(pLofi->content().withHeight(KnobBox::h), lofiRow);
    {
        auto inner = pOut->content();
        meters->setBounds(inner.removeFromRight(80));
        inner.removeFromRight(12);
        layoutRow(inner.removeFromTop(KnobBox::h), { outRow[0], outRow[1] }, 10);
        inner.removeFromTop(10);
        layoutRow(inner, { outRow[2] });
    }
    pVinyl->setBounds(area);
    {
        auto inner = pVinyl->content();
        spreadRow(inner.removeFromTop(KnobBox::h), vinylRow);
        inner.removeFromTop(10);
        const int half = (inner.getWidth() - 12) / 2;
        compView->setBounds(inner.removeFromLeft(half + 70));
        inner.removeFromLeft(12);
        recordView->setBounds(inner);
    }
}
