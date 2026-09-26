#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "Relevance.h"
#include "FractureGuide.h"
#include <map>

using namespace bauhaus;
using namespace fracture;

// one line per control, for its tooltip. Band and slot ids share a line: the
// digit in them is replaced by # before the lookup (d0a, d1a, d2a -> d#a)
static juce::String helpFor(std::string id){
    for (auto& c : id) if (c >= '0' && c <= '9') c = '#';
    static const std::map<std::string, const char*> h = {
        { "inGain", "Level into the box" },
        { "preHP", "High pass before anything is driven: keeps the lows clean" },
        { "preLP", "Low pass before anything is driven: tames what the drive will fold" },
        { "bands", "Drive the whole signal, or split it into two or three bands first" },
        { "x#", "Crossover frequency between bands" },
        { "osFactor", "Oversampling around the drive. More is cleaner and costs latency and CPU" },
        { "m#a", "The first shaper" }, { "d#a", "How hard the first shaper is driven" },
        { "sb#", "A second shaper after the first" },
        { "m#b", "The second shaper" }, { "d#b", "How hard the second shaper is driven" },
        { "t#", "Tilt between the two shapers: plus is brighter" },
        { "mx#", "This band's drive against its own dry signal" },
        { "lv#", "This band's level" }, { "mu#", "Mute this band" }, { "so#", "Hear only this band" },
        { "bits", "Crush resolution" }, { "redux", "Crush sample-rate division" },
        { "crMix", "How much of the crushed signal" },
        { "fbAmt", "How much comes back round the loop" },
        { "fbTime", "The loop's length, in milliseconds" },
        { "fbNote", "The note the loop rings at, tuned to the cent" },
        { "fbTone", "Low pass inside the loop: each repeat darker" },
        { "fbMode", "Time: milliseconds. Pitch: a note it rings at. Sync: a note division" },
        { "fbDiv", "The loop's length as a division of the host tempo" },
        { "fbThru", "Send the repeats back through the drive, so every repeat is driven again" },
        { "fltType", "The post filter's response" },
        { "fltFreq", "Cutoff. On the ladder, the mark is where the resonance sings" },
        { "fltQ", "Resonance. The top of the ladder's range self-oscillates" },
        { "fltCirc", "Clean biquads, or a nonlinear ladder that squelches when driven" },
        { "fltPoles", "Steepness: 12 to 48 dB an octave" },
        { "fltDrive", "Drive into the ladder" }, { "fltDrift", "Slow independent wander per channel" },
        { "fltMix", "The filter against what went into it" },
        { "rhDepth", "How far the rhythm moves the cutoff, in octaves either way" },
        { "rhDiv", "One rhythm cycle per division, locked to the host. Free uses Rate" },
        { "rhRate", "The rhythm's speed when its division is Free" },
        { "rhShape", "Sine, triangle, saws, square, random per division, or the eight steps" },
        { "rhGroove", "Swings every second division late" },
        { "rhPhase", "Offsets the right channel's rhythm" }, { "rhGlide", "Rounds the rhythm's edges" },
        { "rhStep#", "Draw the eight steps; they play when the shape is Steps" },
        { "mix", "The whole effect against the dry signal" },
        { "width", "Stereo width of the result" }, { "outGain", "Level out" },
        { "autoGain", "Takes back the level the drive adds, so drive is heard as character" },
        { "safety", "A limiter, transparent below -3 dB, that never passes 0 dBFS" },
        { "l#Rate", "LFO speed" }, { "l#Div", "Lock the LFO to the host's tempo" },
        { "l#Shape", "LFO shape" }, { "l#Depth", "How much of the LFO reaches the matrix" },
        { "envAtk", "How fast the envelope follower rises" },
        { "envRel", "How fast it falls" }, { "envSens", "How much of the input level reaches the matrix" },
        { "trOn", "An insert tremolo at the very end of the chain" },
        { "trDiv", "Lock the tremolo to the host's tempo" }, { "trRate", "Tremolo speed" },
        { "trDepth", "How deep the tremolo cuts" }, { "trShape", "Sine through triangle to a hard chop" },
        { "trEdge", "Softens the chop's edges" }, { "trDuty", "How much of the cycle is the loud half" },
        { "trSpread", "Offsets the right channel; 180 degrees is auto-pan" },
        { "mS#", "What moves this slot's target" }, { "mD#", "What this slot moves" },
        { "mA#", "How far, either way" },
        { "mc#", "A macro: pick it as a source in the matrix and this one knob moves everything it is routed to" },
        { "xyX", "The pad's horizontal axis, as a matrix source" }, { "xyY", "The pad's vertical axis, as a matrix source" },
    };
    const auto it = h.find(id);
    return it != h.end() ? juce::String(it->second) : juce::String();
}

// value readouts formatted the way the browser version formats them
static juce::String fmtValue(const ParamInfo& p, double v){
    if (p.id == "l1Rate" || p.id == "l2Rate" || p.id == "rhRate") return juce::String(v, v < 1.0 ? 2 : 1);
    if (p.unit == "note"){                            // A3, or A3 +12c between notes
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const int k = juce::roundToInt(v);
        const int cents = juce::roundToInt((v - k) * 100.0);
        juce::String t = juce::String(names[((k % 12) + 12) % 12]) + juce::String(k / 12 - 1);
        if (cents != 0) t << (cents > 0 ? " +" : " ") << cents << "c";
        return t;
    }
    if (p.unit == "deg") return juce::String(juce::roundToInt(v)) + " deg";
    if (p.unit == "oct") return std::fabs(v) < 0.05 ? juce::String("off")
                                                     : (v > 0 ? "+" : "") + juce::String(v, 1) + " oct";
    if (p.id == "redux") return "/" + juce::String(juce::roundToInt(v));
    if (p.id == "bits")  return juce::String(v, 1);
    if (p.id == "fltQ")  return juce::String(v, 2);
    if (p.unit == "x")   return juce::String(20.0 * std::log10(std::max(1.0e-6, v)), 1);  // drive in dB
    if (p.unit == "Hz")  return v >= 1000.0 ? juce::String(v / 1000.0, v < 10000.0 ? 2 : 1) + "k"
                                            : juce::String(juce::roundToInt(v));
    if (p.unit == "dB")  return (v > 0 ? "+" : "") + juce::String(v, 1);
    if (p.unit == "%")   return juce::String(juce::roundToInt(v)) + "%";
    if (p.unit == "ms")  return v < 10.0 ? juce::String(v, 1) + "m" : juce::String(juce::roundToInt(v)) + "m";
    return juce::String(v, 2);
}

// ---------------------------------------------------------------- primitives
KnobBox::KnobBox(FractureProcessor& p, const juce::String& paramId, juce::Colour hue,
                 bool small, bool withCaption, bool valueBeside)
    : proc(p), info(Params::get()[Params::get().index(paramId.toStdString())]),
      isSmall(small), showCaption(withCaption), beside(valueBeside)
{
    caption = info.id == "redux" ? "Downs." : juce::String(info.name);
    // the panel already says which section this is, so the caption need not
    for (const char* prefix : { "B1 ", "B2 ", "B3 ", "LFO 1 ", "LFO 2 ", "Env ",
                                "Filter ", "Trem ", "Rhythm " })
        if (caption.startsWith(prefix)) caption = caption.substring(static_cast<int>(std::strlen(prefix)));
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
    if (beside) setSize(34 + 52, 44);             // knob, then its value to the right
    else setSize(small ? wSmall : w, small ? hSmall : h);
}
void KnobBox::resized(){
    const int d = isSmall ? 34 : 46;
    if (beside) slider.setBounds(0, 14 + (30 - d) / 2 + 2, d, d);   // centred on a ChoiceBox's box
    else slider.setBounds((getWidth() - d) / 2, 0, d, d);
}
void KnobBox::paint(juce::Graphics& g){
    const int d = isSmall ? 34 : 46;
    if (beside){
        g.setColour(dim);
        g.setFont(mono(11.0f));
        g.drawText(valueText, d + 6, 14, getWidth() - d - 6, 30, juce::Justification::centredLeft);
        return;
    }
    if (showCaption)
        drawTracked(g, caption, { 0, d + 3, getWidth(), 11 }, isSmall ? 8.5f : 9.0f, 1.1f,
                    juce::Justification::horizontallyCentred, ink);
    g.setColour(dim);
    g.setFont(mono(isSmall ? 10.0f : 11.0f));
    g.drawText(valueText, 0, d + (showCaption ? 15 : 2), getWidth(), 12,
               juce::Justification::centred);
}
void KnobBox::setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); slider.setTooltip(tip); }
void ChoiceBox::setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); box.setTooltip(tip); }
void ToggleBox::setState(bool idle, const juce::String& tip){ setAlpha(idle ? 0.35f : 1.0f); button.setTooltip(tip); }

void KnobBox::refresh(){
    const float base = proc.apvts.getRawParameterValue(info.id)->load();
    const juce::String t = fmtValue(info, base);
    if (t != valueText){ valueText = t; repaint(); }
}

ChoiceBox::ChoiceBox(FractureProcessor& p, const juce::String& paramId,
                     const juce::String& label, int width)
    : caption(label)
{
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

ToggleBox::ToggleBox(FractureProcessor& p, const juce::String& paramId,
                     juce::Colour onColour, int width){
    const auto& info = Params::get()[Params::get().index(paramId.toStdString())];
    juce::String label = juce::String(info.name);
    if (label.startsWith("B")) label = label.substring(3);
    button.setButtonText(label);
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
    g.setColour(panelBg);
    g.fillRect(getLocalBounds());
    g.setColour(hue);
    g.fillRect(getLocalBounds().removeFromTop(barHeight));
    const bool light = hue.getPerceivedBrightness() < 0.6f;
    auto bar = getLocalBounds().removeFromTop(barHeight).reduced(10, 0);
    drawTracked(g, juce::String(no).paddedLeft('0', 2), bar.removeFromLeft(20), 9.0f, 1.0f,
                juce::Justification::left, (light ? face : ink).withAlpha(0.65f));
    drawTracked(g, title, bar, 10.0f, 2.2f, juce::Justification::left, light ? face : ink);
    g.setColour(ink);
    g.drawRect(getLocalBounds(), 2);
}

// ------------------------------------------------------------------- visuals
Scope::Scope(FractureProcessor& p) : proc(p){ startTimerHz(30); }
void Scope::timerCallback(){
    if (proc.copyScopeSpectrum(spectrum)){
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const int bins = static_cast<int>(spectrum.size());
        for (size_t i = 0; i < bars.size(); ++i){
            const double f0 = 20.0 * std::pow(1000.0, static_cast<double>(i) / bars.size());
            const double f1 = 20.0 * std::pow(1000.0, static_cast<double>(i + 1) / bars.size());
            const int b0 = juce::jlimit(0, bins - 1, static_cast<int>(f0 / (sr * 0.5) * bins));
            const int b1 = juce::jlimit(b0 + 1, bins, static_cast<int>(f1 / (sr * 0.5) * bins) + 1);
            float m = 0.0f;
            for (int b = b0; b < b1; ++b) m = std::max(m, spectrum[static_cast<size_t>(b)]);
            const float db = juce::Decibels::gainToDecibels(m / 64.0f, -70.0f);
            const float norm = juce::jlimit(0.0f, 1.0f, (db + 70.0f) / 70.0f);
            bars[i] = std::max(norm, bars[i] * 0.82f);           // slow fall
        }
    } else {
        for (auto& b : bars) b *= 0.9f;
    }
    repaint();
}
void Scope::paint(juce::Graphics& g){
    auto area = getLocalBounds();
    auto specArea = area.removeFromTop(area.getHeight() * 55 / 100);
    area.removeFromTop(12);
    auto curveArea = area;

    // ---- spectrum: flat bars, three regions following the live crossovers
    g.setColour(face); g.fillRect(specArea);
    g.setColour(ink);  g.drawRect(specArea, 2);
    const int nb = static_cast<int>(proc.apvts.getRawParameterValue("bands")->load()) + 1;
    const float x1 = proc.apvts.getRawParameterValue("x1")->load();
    const float x2 = proc.apvts.getRawParameterValue("x2")->load();
    const auto inner = specArea.reduced(3);
    for (const float f : { 100.0f, 1000.0f, 10000.0f }){
        const int x = inner.getX() + juce::roundToInt(inner.getWidth() * std::log(f / 20.0f) / std::log(1000.0f));
        g.setColour(track);
        g.drawVerticalLine(x, static_cast<float>(specArea.getY() + 2), static_cast<float>(specArea.getBottom() - 2));
    }
    const float slot = inner.getWidth() / static_cast<float>(bars.size());
    for (size_t i = 0; i < bars.size(); ++i){
        const double f0 = 20.0 * std::pow(1000.0, static_cast<double>(i) / bars.size());
        const double f1 = 20.0 * std::pow(1000.0, static_cast<double>(i + 1) / bars.size());
        const double fc = std::sqrt(f0 * f1);
        const juce::Colour c = nb == 1 ? ink
                             : nb == 2 ? (fc < x1 ? red : blue)
                             : (fc < x1 ? red : (fc < x2 ? yellow : blue));
        const float bh = bars[i] * inner.getHeight();
        if (bh < 1.0f) continue;
        g.setColour(c);
        g.fillRect(inner.getX() + slot * i + 1.0f, inner.getBottom() - bh, slot - 2.0f, bh);
    }

    // ---- the post filter's response over the spectrum, on the same log axis:
    // computed from the filter's own coefficients (FilterResponse.h), live, so a
    // notch sits where it is heard and moves when the rhythm or an LFO moves it
    {
        const auto st = proc.filterState();
        const auto fr = specArea.reduced(3).toFloat();
        const double srNow = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const float top = 18.0f, bottom = -36.0f;                     // dB shown
        auto yOf = [&](double dB){
            const double t = (top - juce::jlimit(static_cast<double>(bottom), static_cast<double>(top), dB)) / (top - bottom);
            return fr.getY() + static_cast<float>(t) * fr.getHeight();
        };
        auto xOf = [&](double f){ return fr.getX() + static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * fr.getWidth(); };
        // 0 dB, and the frequency labels the grid lines already stand for
        g.setColour(dim2);
        {
            juce::Path zero; zero.startNewSubPath(fr.getX(), yOf(0.0)); zero.lineTo(fr.getRight(), yOf(0.0));
            const float dashes[] = { 3.0f, 4.0f }; juce::Path dashed;
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, zero, dashes, 2);
            g.fillPath(dashed);
        }
        g.setFont(mono(9.0f));
        for (const auto& [f, label] : { std::pair<double, const char*>{ 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
            g.drawText(label, juce::Rectangle<float>(xOf(f) + 3.0f, fr.getBottom() - 12.0f, 30.0f, 11.0f), juce::Justification::left);
        if (st.type > 0){
            juce::Path curve, fill;
            const int n = juce::roundToInt(fr.getWidth());
            for (int px = 0; px <= n; ++px){
                const double f = 20.0 * std::pow(1000.0, px / static_cast<double>(n));
                const double dB = 20.0 * std::log10(std::max(1e-9, std::abs(filterResponse(st, f, srNow))));
                const float x = fr.getX() + static_cast<float>(px), y = yOf(dB);
                if (px == 0){ curve.startNewSubPath(x, y); fill.startNewSubPath(x, yOf(0.0)); }
                else curve.lineTo(x, y);
                fill.lineTo(x, y);
            }
            fill.lineTo(fr.getRight(), yOf(0.0)); fill.closeSubPath();
            g.setColour(blue.withAlpha(0.14f)); g.fillPath(fill);
            g.setColour(blue); g.strokePath(curve, juce::PathStrokeType(2.25f));
            // the frequency the filter is set to, and what it is
            const float cx = xOf(juce::jlimit(20.0, 20000.0, st.freq));
            g.setColour(blue.withAlpha(0.6f));
            g.fillRect(cx - 0.75f, fr.getY(), 1.5f, fr.getHeight());
            const juce::String hz = st.freq >= 1000.0 ? juce::String(st.freq / 1000.0, st.freq < 10000.0 ? 2 : 1) + " kHz"
                                                      : juce::String(juce::roundToInt(st.freq)) + " Hz";
            const juce::String text = juce::String(filterTypeLabel(st.type)) + " · " + hz;
            g.setFont(mono(11.0f));
            const float tw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text) + 12.0f;
            auto tag = juce::Rectangle<float>(juce::jlimit(fr.getX(), fr.getRight() - tw, cx - tw * 0.5f), fr.getY() + 4.0f, tw, 17.0f);
            g.setColour(face); g.fillRect(tag);
            g.setColour(blue); g.drawRect(tag, 1.5f);
            g.drawText(text, tag, juce::Justification::centred);
        }
    }

    // ---- transfer curve of the selected band, computed from the parameters
    g.setColour(face); g.fillRect(curveArea);
    g.setColour(ink);  g.drawRect(curveArea, 2);
    const auto ci = curveArea.reduced(3).toFloat();
    g.setColour(track);
    g.drawHorizontalLine(juce::roundToInt(ci.getCentreY()), ci.getX(), ci.getRight());
    g.drawVerticalLine(juce::roundToInt(ci.getCentreX()), ci.getY(), ci.getBottom());
    {
        juce::Path diag;
        diag.startNewSubPath(ci.getX(), ci.getBottom());
        diag.lineTo(ci.getRight(), ci.getY());
        const float dashes[] = { 4.0f, 5.0f };
        juce::Path dashed;
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, diag, dashes, 2);
        g.fillPath(dashed);
    }
    const int band = juce::jlimit(0, 2, static_cast<int>(getProperties().getWithDefault("band", 0)));
    const juce::String s = juce::String(band);
    const float dA = proc.apvts.getRawParameterValue("d" + s + "a")->load();
    const float dB = proc.apvts.getRawParameterValue("d" + s + "b")->load();
    const int   mA = static_cast<int>(proc.apvts.getRawParameterValue("m" + s + "a")->load());
    const int   mB = static_cast<int>(proc.apvts.getRawParameterValue("m" + s + "b")->load());
    const bool  sB = proc.apvts.getRawParameterValue("sb" + s)->load() > 0.5f;
    const bool  ag = proc.apvts.getRawParameterValue("autoGain")->load() > 0.5f;
    const float mix = proc.apvts.getRawParameterValue("mx" + s)->load() / 100.0f;
    const float lvl = juce::Decibels::decibelsToGain(proc.apvts.getRawParameterValue("lv" + s)->load());
    juce::Path curve;
    for (int px = 0; px <= juce::roundToInt(ci.getWidth()); ++px){
        const double x = (px / static_cast<double>(ci.getWidth())) * 2.0 - 1.0;
        double v = clampT(shape(mA, x * dA), -1.0, 1.0) * (ag ? autoGainFor(dA) : 1.0);
        if (sB) v = clampT(shape(mB, v * dB), -1.0, 1.0) * (ag ? autoGainFor(dB) : 1.0);
        v = (x + (v - x) * mix) * lvl;
        const float y = ci.getCentreY() - static_cast<float>(juce::jlimit(-1.4, 1.4, v)) * ci.getHeight() * 0.46f;
        if (px == 0) curve.startNewSubPath(ci.getX(), y);
        else curve.lineTo(ci.getX() + px, y);
    }
    g.setColour(yellow);
    g.strokePath(curve, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved));
    g.setColour(dim);
    g.setFont(mono(10.0f));
    static const char* names[] = { "low", "mid", "high" };
    g.drawText(juce::String(names[band]) + " band · transfer curve",
               curveArea.reduced(9, 6), juce::Justification::topLeft);
}

Meters::Meters(FractureProcessor& p) : proc(p){ startTimerHz(30); }
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
        auto col = area.removeFromLeft(each).reduced(6, 0);
        auto num = col.removeFromBottom(14);
        auto cap = col.removeFromBottom(14);
        auto bar = col.withSizeKeepingCentre(20, col.getHeight());
        g.setColour(face); g.fillRect(bar);
        const float norm = juce::jlimit(0.0f, 1.0f, (vals[i] + 60.0f) / 60.0f);
        auto fill = bar.reduced(2).withTrimmedTop(juce::roundToInt((1.0f - norm) * (bar.getHeight() - 4)));
        g.setColour(yellow); g.fillRect(fill);
        if (norm > 0.9f){                                  // last 6 dB in red
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

TremStrip::TremStrip(FractureProcessor& p) : proc(p){ startTimerHz(30); }
void TremStrip::paint(juce::Graphics& g){
    auto area = getLocalBounds().reduced(1);
    g.setColour(face); g.fillRect(area);
    g.setColour(ink);  g.drawRect(area, 2);
    auto plot = area.reduced(6, 5);

    const auto get = [&](const char* id){
        return proc.apvts.getRawParameterValue(id)->load();
    };
    const bool on = get("trOn") > 0.5f;
    const double depth = get("trDepth") / 100.0;
    const double shape = get("trShape") / 100.0;
    const double duty  = juce::jlimit(0.05, 0.95, static_cast<double>(get("trDuty")) / 100.0);
    const double edge  = get("trEdge") / 100.0;
    const int div = static_cast<int>(get("trDiv"));

    // the same arithmetic the DSP uses, so the picture is the sound
    const auto wave = [&](double p){
        const double w = p < duty ? 0.5 * p / duty : 0.5 + 0.5 * (p - duty) / (1.0 - duty);
        const double sine = 0.5 + 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * w);
        const double tri  = w < 0.5 ? 1.0 - 2.0 * w : 2.0 * w - 1.0;
        const double sqr  = w < 0.5 ? 1.0 : 0.0;
        return shape <= 0.5 ? sine + (tri - sine) * (shape * 2.0)
                            : tri + (sqr - tri) * ((shape - 0.5) * 2.0);
    };
    // edge as the eye sees it: a slew over a fraction of the cycle
    const double k = juce::jlimit(0.02, 1.0, 0.04 + 0.96 * edge * edge);

    juce::Path path;
    double sm = wave(0.0);
    const int steps = juce::jmax(8, plot.getWidth());
    for (int i = 0; i <= steps; ++i){
        const double p = static_cast<double>(i) / steps;
        sm += (wave(p) - sm) * k;
        const double gain = 1.0 - depth + depth * sm;
        const float x = plot.getX() + static_cast<float>(p) * plot.getWidth();
        const float y = plot.getBottom() - static_cast<float>(gain) * plot.getHeight();
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(on ? blue : dim.withAlpha(0.5f));
    g.strokePath(path, juce::PathStrokeType(2.0f));

    // where the modulation is right now, on the same scale as the curve
    if (on){
        const float now = (proc.tremOut.load() + 1.0f) * 0.5f;
        const float gain = static_cast<float>(1.0 - depth) + static_cast<float>(depth) * now;
        const float y = plot.getBottom() - gain * plot.getHeight();
        g.setColour(red);
        g.fillRect(static_cast<float>(plot.getRight() - 10), y - 1.0f, 10.0f, 2.0f);
    }

    const float bpm = proc.hostBpm.load();
    juce::String label = div > 0 ? juce::String(divNames[div]) + "  at  " + juce::String(bpm, 1) + " BPM"
                                 : juce::String(get("trRate"), 2) + " Hz  free";
    if (!on) label = "off  -  " + label;
    drawTracked(g, label, plot.removeFromBottom(11), 8.0f, 1.4f, juce::Justification::left, dim);
}

ModSources::ModSources(FractureProcessor& p) : proc(p){ startTimerHz(30); }
void ModSources::paint(juce::Graphics& g){
    const char* names[] = { "LFO 1", "LFO 2", "ENV", "TREM" };
    const float vals[] = { proc.lfo1.load(), proc.lfo2.load(), proc.envOut.load(),
                           proc.tremOut.load() };
    const juce::Colour hues[] = { blue, blue, red, blue };
    auto area = getLocalBounds();
    const int rowH = area.getHeight() / 4;
    for (int i = 0; i < 4; ++i){
        auto row = area.removeFromTop(rowH);
        drawTracked(g, names[i], row.removeFromLeft(46), 9.0f, 1.8f, juce::Justification::left, ink);
        auto bar = row.reduced(0, rowH / 2 - 5);
        g.setColour(face); g.fillRect(bar);
        g.setColour(ink); g.drawRect(bar, 2);
        const float v = juce::jlimit(-1.0f, 1.0f, vals[i]);
        const float centre = bar.getCentreX();
        const float half = (bar.getWidth() - 4) * 0.5f;
        g.setColour(hues[i]);
        if (v >= 0) g.fillRect(centre, static_cast<float>(bar.getY() + 2), std::max(1.5f, v * half), static_cast<float>(bar.getHeight() - 4));
        else        g.fillRect(centre + v * half, static_cast<float>(bar.getY() + 2), std::max(1.5f, -v * half), static_cast<float>(bar.getHeight() - 4));
    }
}

// ------------------------------------------------------------------- XY pad
// which matrix slots a source drives, as "Cutoff +45%" pieces
static juce::StringArray routesFor(FractureProcessor& proc, int source){
    const Params& P = Params::get();
    juce::StringArray out;
    for (int k = 0; k < numSlots; ++k){
        const juce::String s(k);
        const int src = juce::roundToInt(proc.apvts.getRawParameterValue("mS" + s)->load());
        const int dst = juce::roundToInt(proc.apvts.getRawParameterValue("mD" + s)->load());
        const float amt = proc.apvts.getRawParameterValue("mA" + s)->load();
        if (src != source || dst <= 0 || amt == 0.0f) continue;
        const auto& info = P[P.dests()[static_cast<size_t>(dst - 1)]];
        out.add(juce::String(info.name) + " " + (amt > 0 ? "+" : "") + juce::String(juce::roundToInt(amt)) + "%");
    }
    return out;
}

XYPad::XYPad(FractureProcessor& p) : proc(p){ startTimerHz(24); }
juce::Rectangle<float> XYPad::field() const { return getLocalBounds().toFloat().reduced(14.0f); }   // room for the whole dot at the edges
void XYPad::paint(juce::Graphics& g){
    auto area = getLocalBounds();
    g.setColour(face); g.fillRect(area);
    const auto f = field();
    g.setColour(track);
    for (int i = 1; i < 4; ++i){
        g.fillRect(f.getX() + f.getWidth() * i / 4.0f - 0.5f, f.getY(), 1.0f, f.getHeight());
        g.fillRect(f.getX(), f.getY() + f.getHeight() * i / 4.0f - 0.5f, f.getWidth(), 1.0f);
    }
    const float x = proc.apvts.getRawParameterValue("xyX")->load() / 100.0f;
    const float y = proc.apvts.getRawParameterValue("xyY")->load() / 100.0f;
    const float px = f.getX() + x * f.getWidth(), py = f.getBottom() - y * f.getHeight();
    g.setColour(blue.withAlpha(0.35f));
    g.fillRect(f.getX(), py - 0.75f, f.getWidth(), 1.5f);
    g.fillRect(px - 0.75f, f.getY(), 1.5f, f.getHeight());
    g.setColour(yellow); g.fillEllipse(px - 9.0f, py - 9.0f, 18.0f, 18.0f);
    g.setColour(ink);    g.drawEllipse(px - 9.0f, py - 9.0f, 18.0f, 18.0f, 2.5f);
    g.setColour(dim); g.setFont(mono(10.0f));
    g.drawText("X " + juce::String(juce::roundToInt(x * 100)) + "  Y " + juce::String(juce::roundToInt(y * 100)),
               area.reduced(8, 6), juce::Justification::topRight);
    g.setColour(ink); g.drawRect(area, 2);
}
void XYPad::setFrom(juce::Point<float> pos){
    const auto f = field();
    const float x = juce::jlimit(0.0f, 1.0f, (pos.x - f.getX()) / f.getWidth());
    const float y = juce::jlimit(0.0f, 1.0f, (f.getBottom() - pos.y) / f.getHeight());
    for (auto [id, v] : { std::pair<const char*, float>{ "xyX", x }, { "xyY", y } })
        if (auto* prm = proc.apvts.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(v * 100.0f));
    repaint();
}
void XYPad::mouseDown(const juce::MouseEvent& e){
    for (const char* id : { "xyX", "xyY" }) if (auto* prm = proc.apvts.getParameter(id)) prm->beginChangeGesture();
    dragging = true;
    setFrom(e.position);
}
void XYPad::mouseDrag(const juce::MouseEvent& e){ setFrom(e.position); }
void XYPad::mouseUp(const juce::MouseEvent&){
    if (!dragging) return;
    for (const char* id : { "xyX", "xyY" }) if (auto* prm = proc.apvts.getParameter(id)) prm->endChangeGesture();
    dragging = false;
}

PerformRoutes::PerformRoutes(FractureProcessor& p) : proc(p){ startTimerHz(6); }
void PerformRoutes::paint(juce::Graphics& g){
    const char* names[4] = { "Macro 1", "Macro 2", "Pad X", "Pad Y" };
    auto area = getLocalBounds();
    const int rowH = area.getHeight() / 4;
    for (int i = 0; i < 4; ++i){
        auto row = area.removeFromTop(rowH);
        drawTracked(g, names[i], row.removeFromTop(12), 8.5f, 1.4f, juce::Justification::left, ink);
        const auto r = routesFor(proc, 6 + i);
        g.setColour(r.isEmpty() ? dim2 : dim);
        g.setFont(mono(10.0f));
        g.drawFittedText(r.isEmpty() ? juce::String("not routed: pick it as a matrix source")
                                     : r.joinIntoString(", "),
                         row, juce::Justification::topLeft, 2, 0.9f);
    }
}

// -------------------------------------------------------------------- editor
// -------------------------------------------------------------- step editor
StepEditor::StepEditor(FractureProcessor& p) : proc(p){ startTimerHz(24); }

juce::Rectangle<int> StepEditor::lane() const { return getLocalBounds().reduced(10, 8).withTrimmedRight(26); }

void StepEditor::paint(juce::Graphics& g){
    auto area = getLocalBounds();
    g.setColour(face); g.fillRect(area);
    g.setColour(ink);  g.drawRect(area, 2);
    const auto L = lane();
    const int shape = static_cast<int>(proc.apvts.getRawParameterValue("rhShape")->load());
    const bool stepsShape = shape == fracture::RhythmMod::Steps;
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
        fracture::RhythmMod r;
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
    const float v = juce::jlimit(0.0f, 1.0f, proc.rhythmOut.load());
    g.setColour(yellow);
    g.fillRect(now.withTrimmedTop(juce::roundToInt((1.0f - v) * now.getHeight())));
    g.setColour(ink); g.drawRect(now, 1);
}

void StepEditor::setFrom(juce::Point<int> pos){
    const auto L = lane();
    const int k = juce::jlimit(0, 7, static_cast<int>((pos.x - L.getX()) * 8 / juce::jmax(1, L.getWidth())));
    const float v = juce::jlimit(0.0f, 1.0f, (L.getBottom() - pos.y) / static_cast<float>(juce::jmax(1, L.getHeight())));
    if (k != dragging){
        if (dragging >= 0)
            if (auto* old = proc.apvts.getParameter("rhStep" + juce::String(dragging + 1))) old->endChangeGesture();
        dragging = k;
        if (auto* prm = proc.apvts.getParameter("rhStep" + juce::String(k + 1))) prm->beginChangeGesture();
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

static void layoutRow(juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int gap = 12){
    int x = area.getX();
    for (auto* c : items){
        if (c == nullptr) continue;
        if (c->getWidth() == 2){                             // divider marker
            c->setBounds(x, area.getY(), 2, area.getHeight() - 12);
            x += 2 + gap;
            continue;
        }
        c->setBounds(x, area.getY(), c->getWidth(), c->getHeight());
        x += c->getWidth() + gap;
    }
}

class Caption : public juce::Component {
public:
    Caption(juce::String t, juce::Colour accent) : text(std::move(t)), hue(accent){ setSize(200, 14); }
    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        if (!hue.isTransparent()){
            g.setColour(hue);
            g.fillRect(area.removeFromLeft(4));
            area.removeFromLeft(6);
        }
        drawTracked(g, text, area, 9.0f, 1.8f, juce::Justification::left, dim);
    }
private:
    juce::String text;
    juce::Colour hue;
};

class Divider : public juce::Component {
public:
    Divider(){ setSize(2, 60); }
    void paint(juce::Graphics& g) override { g.setColour(ink); g.fillRect(getLocalBounds()); }
};

FractureEditor::FractureEditor(FractureProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&look);

    canvas.onPaint  = [this](juce::Graphics& g){ paintDesign(g); };
    canvas.onLayout = [this]{ layoutDesign(); };
    addAndMakeVisible(canvas);

    // ---- header
    for (const auto& preset : factoryPresets()) presetBox.addItem(preset.name, presetBox.getNumItems() + 1);
    presetBox.setSelectedItemIndex(proc.getCurrentProgram(), juce::dontSendNotification);
    presetBox.onChange = [this]{ proc.setCurrentProgram(presetBox.getSelectedItemIndex()); };
    canvas.addAndMakeVisible(presetBox);

    copyButton.onClick = [this]{
        juce::SystemClipboard::copyTextToClipboard(proc.saveBrowserPatch());
    };
    pasteButton.onClick = [this]{
        const auto text = juce::SystemClipboard::getTextFromClipboard();
        if (!proc.loadBrowserPatch(text))
            juce::NativeMessageBox::showAsync(
                juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("FRACTURE").withMessage("That clipboard text is not a FRACTURE patch."),
                nullptr);
    };
    canvas.addAndMakeVisible(copyButton);
    canvas.addAndMakeVisible(pasteButton);

    // ---- panels
    pIn     = make<Panel>(1, "Input & pre-filter", ink);
    pSplit  = make<Panel>(2, "Split", ink);
    pDrive  = make<Panel>(3, "Drive", yellow);
    pCrush  = make<Panel>(4, "Crush & feedback", red);
    pFilter = make<Panel>(5, "Filter", blue);
    pOut    = make<Panel>(11, "Output", ink);
    pMod    = make<Panel>(7, "Modulation", blue);
    pScope  = make<Panel>(8, "Scope", ink);
    pPerform = make<Panel>(9, "Perform", yellow);
    pTrem   = make<Panel>(10, "Tremolo", blue);
    pRhythm = make<Panel>(6, "Filter rhythm", blue);
    panels = { pIn, pSplit, pDrive, pCrush, pFilter, pOut, pMod, pScope, pTrem, pRhythm, pPerform };

    auto knob = [&](juce::Component* parent, const char* id, juce::Colour hue, bool small = false){
        auto* k = new KnobBox(proc, id, hue, small);
        owned.emplace_back(k);
        parent->addAndMakeVisible(k);
        knobs.push_back(k);
        reg(id, k);
        return static_cast<juce::Component*>(k);
    };
    auto choice = [&](juce::Component* parent, const char* id, const char* label, int width = 116){
        auto* c = new ChoiceBox(proc, id, label, width);
        owned.emplace_back(c);
        parent->addAndMakeVisible(c);
        reg(id, c);
        return static_cast<juce::Component*>(c);
    };
    auto toggle = [&](juce::Component* parent, const char* id, juce::Colour hue, int width = 96){
        auto* t = new ToggleBox(proc, id, hue, width);
        owned.emplace_back(t);
        parent->addAndMakeVisible(t);
        reg(id, t);
        return static_cast<juce::Component*>(t);
    };
    auto divider = [&](juce::Component* parent){
        auto* d = new Divider();
        owned.emplace_back(d);
        parent->addAndMakeVisible(d);
        return static_cast<juce::Component*>(d);
    };

    inRow    = { knob(pIn, "inGain", ink), knob(pIn, "preHP", ink), knob(pIn, "preLP", ink) };
    splitRow = { choice(pSplit, "bands", "Bands", 108), knob(pSplit, "x1", ink), knob(pSplit, "x2", ink),
                 divider(pSplit), choice(pSplit, "osFactor", "Oversampling", 108) };
    crushRow = { knob(pCrush, "bits", red), knob(pCrush, "redux", red), knob(pCrush, "crMix", red),
                 divider(pCrush),
                 knob(pCrush, "fbAmt", red), knob(pCrush, "fbTime", red), knob(pCrush, "fbNote", red),
                 knob(pCrush, "fbTone", red) };
    fbRow = { choice(pCrush, "fbMode", "FB mode", 100), choice(pCrush, "fbDiv", "FB division", 110),
              toggle(pCrush, "fbThru", red, 150) };
    filterTypeRow = { choice(pFilter, "fltType", "Type", 116),
                      choice(pFilter, "fltCirc", "Circuit", 116),
                      choice(pFilter, "fltPoles", "Slope", 96) };
    filterKnobRow = { knob(pFilter, "fltFreq", blue), knob(pFilter, "fltQ", blue),
                      knob(pFilter, "fltDrive", blue), knob(pFilter, "fltDrift", blue),
                      knob(pFilter, "fltMix", blue) };
    rhythmRow = { knob(pRhythm, "rhDepth", blue), choice(pRhythm, "rhDiv", "Rhythm", 104),
                  knob(pRhythm, "rhRate", blue), choice(pRhythm, "rhShape", "Shape", 110),
                  knob(pRhythm, "rhGroove", blue), knob(pRhythm, "rhPhase", blue),
                  knob(pRhythm, "rhGlide", blue) };
    steps = make<StepEditor>(proc);
    pRhythm->addAndMakeVisible(steps);
    reg("rhStep1", steps);                            // the eight steps dim together
    outRow   = { knob(pOut, "mix", ink), knob(pOut, "width", ink), knob(pOut, "outGain", ink) };
    outRow.push_back(toggle(pOut, "autoGain", yellow, 92));
    outRow.push_back(toggle(pOut, "safety", red, 92));

    tremHeadRow = { toggle(pTrem, "trOn", blue, 96), choice(pTrem, "trDiv", "Sync", 116) };
    tremKnobRow = { knob(pTrem, "trRate", blue), knob(pTrem, "trDepth", blue),
                    knob(pTrem, "trShape", blue), knob(pTrem, "trEdge", blue),
                    knob(pTrem, "trDuty", blue), knob(pTrem, "trSpread", blue) };
    tremStrip = make<TremStrip>(proc);
    pTrem->addAndMakeVisible(tremStrip);

    // ---- band tabs and panes
    for (int b = 0; b < 3; ++b){
        bandTab[b].setClickingTogglesState(false);
        bandTab[b].setColour(juce::TextButton::buttonOnColourId, ink);
        bandTab[b].onClick = [this, b]{ selectBand(b); };
        pDrive->addAndMakeVisible(bandTab[b]);
        auto* pane = new juce::Component();
        owned.emplace_back(pane);
        pDrive->addAndMakeVisible(pane);
        bandPane[b] = pane;
        buildBand(b);
    }
    updateTabs();
    selectBand(0);

    // ---- modulation
    for (int i = 0; i < 2; ++i){
        const juce::String n = juce::String(i + 1);
        lfoRow[i] = { knob(pMod, ("l" + n + "Rate").toRawUTF8(), blue, true),
                      choice(pMod, ("l" + n + "Div").toRawUTF8(), "Sync", 100),
                      choice(pMod, ("l" + n + "Shape").toRawUTF8(), "Shape", 100),
                      knob(pMod, ("l" + n + "Depth").toRawUTF8(), blue, true) };
    }
    envRow = { knob(pMod, "envAtk", red, true), knob(pMod, "envRel", red, true),
               knob(pMod, "envSens", red, true) };
    lfoCaption[0] = new Caption("LFO 1", blue);
    lfoCaption[1] = new Caption("LFO 2", blue);
    envCaption = new Caption("Envelope follower", red);
    matrixCaption = new Caption("Matrix", ink);
    for (auto* c : { lfoCaption[0], lfoCaption[1], envCaption, matrixCaption }){
        owned.emplace_back(c);
        pMod->addAndMakeVisible(c);
    }
    modSources = make<ModSources>(proc);
    pMod->addAndMakeVisible(modSources);
    for (int k = 0; k < 6; ++k){
        const juce::String s = juce::String(k);
        auto* amt = new KnobBox(proc, "mA" + s, blue, true, false, true);
        owned.emplace_back(amt);
        pMod->addAndMakeVisible(amt);
        knobs.push_back(amt);
        reg("mA" + s, amt);
        matrixRow[k] = { choice(pMod, ("mS" + s).toRawUTF8(), k == 0 ? "Source" : "", 186),
                         choice(pMod, ("mD" + s).toRawUTF8(), k == 0 ? "Target" : "", 206),
                         amt };
    }

    // ---- perform: the pad, two macros, and what they move
    pad = make<XYPad>(proc);
    pPerform->addAndMakeVisible(pad);

    performRow = { knob(pPerform, "mc1", yellow), knob(pPerform, "mc2", yellow) };
    routes = make<PerformRoutes>(proc);
    pPerform->addAndMakeVisible(routes);

    // ---- scope
    scope = make<Scope>(proc);
    pScope->addAndMakeVisible(scope);
    meters = make<Meters>(proc);
    pScope->addAndMakeVisible(meters);

    setResizable(true, true);
    setResizeLimits(designW * 2 / 5, designH * 2 / 5, designW * 2, designH * 2);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(designW) / designH);
    const auto open = bauhaus::Canvas::openingSize(designW, designH);
    setSize(open.getWidth(), open.getHeight());
    // ---- tooltips and the guide
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "FRACTURE"; o.folderName = "FRACTURE";
        o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        prefs = std::make_unique<juce::PropertiesFile>(o);
    }
    for (auto* b : { &tipsButton, &guideButton }){
        b->setClickingTogglesState(true);
        b->setColour(juce::TextButton::buttonOnColourId, ink);
        canvas.addAndMakeVisible(*b);
    }
    tipsButton.setTooltip("Show a note about each control when the mouse rests on it");
    guideButton.setTooltip("How FRACTURE works, panel by panel");
    tipsButton.onClick = [this]{ setTips(tipsButton.getToggleState()); };
    guideButton.onClick = [this]{ setGuideOpen(guideButton.getToggleState()); };
    setTips(prefs->getBoolValue("tooltips", true));

    built = true;
    layoutDesign();
    updateIdle();                                     // so the first paint is already right
    startTimerHz(24);
}

FractureEditor::~FractureEditor(){ setLookAndFeel(nullptr); }

void FractureEditor::buildBand(int band){
    auto* pane = bandPane[band];
    const juce::String s = juce::String(band);
    auto knob = [&](const char* id, juce::Colour hue){
        auto* k = new KnobBox(proc, id, hue, false);
        owned.emplace_back(k);
        pane->addAndMakeVisible(k);
        knobs.push_back(k);
        reg(id, k);
        return static_cast<juce::Component*>(k);
    };
    auto choice = [&](const char* id, const char* label){
        auto* c = new ChoiceBox(proc, id, label, 116);
        owned.emplace_back(c);
        pane->addAndMakeVisible(c);
        reg(id, c);
        return static_cast<juce::Component*>(c);
    };
    auto toggle = [&](const char* id, juce::Colour hue, int width){
        auto* t = new ToggleBox(proc, id, hue, width);
        owned.emplace_back(t);
        pane->addAndMakeVisible(t);
        reg(id, t);
        return static_cast<juce::Component*>(t);
    };
    auto* div1 = new Divider(); owned.emplace_back(div1); pane->addAndMakeVisible(div1);
    auto* div2 = new Divider(); owned.emplace_back(div2); pane->addAndMakeVisible(div2);

    bandRow[band] = {
        choice(("m" + s + "a").toRawUTF8(), "Mode A"),
        knob(("d" + s + "a").toRawUTF8(), yellow),
        toggle(("sb" + s).toRawUTF8(), yellow, 92),
        choice(("m" + s + "b").toRawUTF8(), "Mode B"),
        knob(("d" + s + "b").toRawUTF8(), yellow),
        div1,
        knob(("t" + s).toRawUTF8(), blue),
        knob(("mx" + s).toRawUTF8(), ink),
        knob(("lv" + s).toRawUTF8(), ink),
        div2,
        toggle(("mu" + s).toRawUTF8(), red, 84),
        toggle(("so" + s).toRawUTF8(), blue, 84)
    };
}

void FractureEditor::selectBand(int band){
    currentBand = band;
    for (int b = 0; b < 3; ++b){
        bandTab[b].setToggleState(b == band, juce::dontSendNotification);
        bandPane[b]->setVisible(b == band);
    }
    if (scope) scope->getProperties().set("band", band);
    layoutDesign();          // the canvas keeps its size, so lay out on it directly
}

void FractureEditor::setTips(bool on){
    tipsButton.setToggleState(on, juce::dontSendNotification);
    if (on && !tips) tips = std::make_unique<juce::TooltipWindow>(this, 500);
    if (!on) tips.reset();
    if (prefs){ prefs->setValue("tooltips", on); prefs->saveIfNeeded(); }
}

void FractureEditor::setGuideOpen(bool open){
    guideButton.setToggleState(open, juce::dontSendNotification);
    if (open && !guide){
        guide = std::make_unique<bauhaus::GuideOverlay>(
            juce::String::fromUTF8(FractureGuide::GUIDE_md, FractureGuide::GUIDE_mdSize));
        canvas.addAndMakeVisible(*guide);
        guide->setBounds(guideArea);
    }
    if (!open) guide.reset();
}

void FractureEditor::updateIdle(){
    const Params& P = Params::get();
    std::vector<float> v(static_cast<size_t>(P.count()));
    for (int i = 0; i < P.count(); ++i) v[static_cast<size_t>(i)] = proc.apvts.getRawParameterValue(P[i].id)->load();
    const auto idle = idleControls(v.data());
    for (auto& c : controls){
        juce::String why;
        for (const auto& d : idle) if (d.param == c.param){ why = d.why; break; }
        const juce::String help = helpFor(P[c.param].id);
        const juce::String tip = why.isNotEmpty() ? "Inactive: " + why + ".  " + help : help;
        if (tip != c.tip){ c.set(why.isNotEmpty(), tip); c.tip = tip; }   // only on a change
    }
    // the pad is two controls: it dims only when neither axis is routed
    if (pad){
        bool xIdle = false, yIdle = false;
        for (const auto& d : idle){ xIdle |= d.param == Ids::get().xyX; yIdle |= d.param == Ids::get().xyY; }
        const bool both = xIdle && yIdle;
        const juce::String tip = both ? juce::String("Inactive: nothing in the matrix uses the pad. Pick XY X or XY Y as a slot's source.")
                                      : juce::String("Drag to move both axes at once. Each axis is a matrix source");
        if (tip != padTip){ pad->setState(both, tip); padTip = tip; }
    }
}

void FractureEditor::timerCallback(){
    for (auto* k : knobs) k->refresh();
    updateIdle();
    updateTabs();
    if (presetBox.getSelectedItemIndex() != proc.getCurrentProgram())
        presetBox.setSelectedItemIndex(proc.getCurrentProgram(), juce::dontSendNotification);
}

void FractureEditor::updateTabs(){
    // the tabs carry the live crossover points, as in the browser version
    const int nb = static_cast<int>(proc.apvts.getRawParameterValue("bands")->load()) + 1;
    const float x1 = proc.apvts.getRawParameterValue("x1")->load();
    const float x2 = proc.apvts.getRawParameterValue("x2")->load();
    const auto hz = [](float f){
        return f >= 1000.0f ? juce::String(f / 1000.0f, f < 10000.0f ? 2 : 1) + "k"
                            : juce::String(juce::roundToInt(f));
    };
    static const char* names[] = { "Low", "Mid", "High" };
    for (int b = 0; b < 3; ++b){
        juce::String range;
        if (nb == 1) range = "full";
        else if (b == 0) range = "< " + hz(x1);
        else if (b == 1) range = nb == 2 ? "> " + hz(x1) : hz(x1) + "-" + hz(x2);
        else range = "> " + hz(x2);
        const bool muted = proc.apvts.getRawParameterValue("mu" + juce::String(b))->load() > 0.5f;
        bandTab[b].setButtonText(juce::String(b + 1) + "  " + names[b] + "  " + range + (muted ? "  mute" : ""));
        bandTab[b].setEnabled(b < nb);
    }
}

void FractureEditor::paint(juce::Graphics& g){
    g.fillAll(ground);                                 // behind the canvas, if it is letterboxed
}

void FractureEditor::paintDesign(juce::Graphics& g){
    g.fillAll(ground);
    auto area = juce::Rectangle<int>(0, 0, designW, designH).reduced(20, 18);

    // ---- header: the three marks, the name, the colour ribbon
    auto header = area.removeFromTop(46);
    auto marks = header.removeFromLeft(104);
    g.setColour(ink);    g.fillRect(marks.removeFromLeft(28).withSizeKeepingCentre(28, 28));
    marks.removeFromLeft(8);
    g.setColour(red);    g.fillEllipse(marks.removeFromLeft(28).withSizeKeepingCentre(28, 28).toFloat());
    marks.removeFromLeft(8);
    {
        auto q = marks.removeFromLeft(28).withSizeKeepingCentre(28, 28).toFloat();
        juce::Path corner;
        corner.startNewSubPath(q.getX(), q.getBottom());
        corner.lineTo(q.getX(), q.getY() + q.getHeight() * 0.5f);
        corner.addArc(q.getX(), q.getY(), q.getWidth(), q.getHeight(),
                      juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi, false);
        corner.lineTo(q.getRight(), q.getBottom());
        corner.closeSubPath();
        g.setColour(blue);
        g.fillPath(corner);
    }
    header.removeFromLeft(14);
    g.setColour(ink);
    g.setFont(grot(32.0f, true));
    g.drawText("FRACTURE", header.removeFromTop(32), juce::Justification::topLeft);
    drawTracked(g, "Multi-band multi-fx distortion", header, 9.0f, 2.6f,
                juce::Justification::left, dim);

    // ---- the colour ribbon under the header
    area.removeFromTop(10);
    auto ribbon = area.removeFromTop(8);
    juce::ignoreUnused(ribbon);
    g.setColour(ink);    g.fillRect(ribbon.removeFromRight(60));
    g.setColour(blue);   g.fillRect(ribbon.removeFromRight(120));
    g.setColour(red);    g.fillRect(ribbon.removeFromRight(180));
    g.setColour(yellow); g.fillRect(ribbon);
}

void FractureEditor::resized(){
    canvas.fitInto(getLocalBounds());                  // one fixed design, scaled to the window
}

void FractureEditor::layoutDesign(){
    if (! built) return;                               // selectBand() runs before the panels do
    auto area = juce::Rectangle<int>(0, 0, designW, designH).reduced(20, 18);
    auto header = area.removeFromTop(46);
    auto right = header.removeFromRight(690);
    tipsButton.setBounds(right.removeFromLeft(70).withSizeKeepingCentre(70, 30));
    right.removeFromLeft(8);
    guideButton.setBounds(right.removeFromLeft(84).withSizeKeepingCentre(84, 30));
    right.removeFromLeft(16);
    presetBox.setBounds(right.removeFromLeft(250).withSizeKeepingCentre(250, 30));
    right.removeFromLeft(8);
    copyButton.setBounds(right.removeFromLeft(120).withSizeKeepingCentre(120, 30));
    right.removeFromLeft(8);
    pasteButton.setBounds(right.removeFromLeft(120).withSizeKeepingCentre(120, 30));

    area.removeFromTop(10);
    area.removeFromTop(8);                                   // the ribbon, painted below
    area.removeFromTop(12);
    guideArea = area;                                // the guide covers everything under the header
    if (guide) guide->setBounds(guideArea);
    // Three rows across a wide canvas. It was one tall column at 1320 x 1376,
    // which on a 1440 x 900 screen opened at 57% and set 9-point captions at
    // about 5; at 1680 x 990 the same screen shows it at 79%.
    const int gap = 14;

    // ---- row 1: what goes in, how it is split, and the drive
    auto rowA = area.removeFromTop(160);
    pIn->setBounds(rowA.removeFromLeft(238));
    rowA.removeFromLeft(gap);
    pSplit->setBounds(rowA.removeFromLeft(418));
    rowA.removeFromLeft(gap);
    pDrive->setBounds(rowA);
    layoutRow(pIn->content().withSizeKeepingCentre(pIn->content().getWidth(), KnobBox::h), inRow);
    layoutRow(pSplit->content().withSizeKeepingCentre(pSplit->content().getWidth(), KnobBox::h), splitRow);
    {
        auto inner = pDrive->content();
        auto tabs = inner.removeFromTop(34);
        for (int b = 0; b < 3; ++b){
            bandTab[b].setBounds(tabs.removeFromLeft(210).withTrimmedBottom(4));
            tabs.removeFromLeft(10);
        }
        inner.removeFromTop(10);
        for (int b = 0; b < 3; ++b){
            bandPane[b]->setBounds(inner);
            layoutRow(bandPane[b]->getLocalBounds().withHeight(KnobBox::h), bandRow[b], 10);
        }
    }

    // ---- row 2: crush and the loop, the filter, and the filter's rhythm
    area.removeFromTop(gap);
    auto rowB = area.removeFromTop(190);
    pCrush->setBounds(rowB.removeFromLeft(520));
    rowB.removeFromLeft(gap);
    pFilter->setBounds(rowB.removeFromLeft(372));
    rowB.removeFromLeft(gap);
    pRhythm->setBounds(rowB);
    {   // the loop's length is a time, a pitch or a division, and the row
        // under the knobs says which, and whether it goes back through the drive
        auto inner = pCrush->content();
        layoutRow(inner.removeFromTop(KnobBox::h), crushRow, 8);
        inner.removeFromTop(12);
        layoutRow(inner.withHeight(44), fbRow, 10);
    }
    {   // the filter carries its circuit above its knobs
        auto inner = pFilter->content();
        layoutRow(inner.removeFromTop(44), filterTypeRow, 8);
        inner.removeFromTop(12);
        layoutRow(inner.withHeight(KnobBox::h), filterKnobRow, 8);
    }
    {   // the rhythm's controls, and under them the steps it plays
        auto inner = pRhythm->content();
        layoutRow(inner.removeFromTop(KnobBox::h), rhythmRow, 10);
        inner.removeFromTop(8);
        steps->setBounds(inner);
    }

    // ---- row 3: modulation, the scope, and the end of the chain
    area.removeFromTop(gap);
    auto rowC = area;
    pMod->setBounds(rowC.removeFromLeft(720));
    rowC.removeFromLeft(gap);
    auto rightCol = rowC.removeFromRight(440);
    rowC.removeFromRight(gap);
    pPerform->setBounds(rowC.removeFromBottom(236));
    rowC.removeFromBottom(gap);
    pScope->setBounds(rowC);
    {   // the pad is square; the macros stand beside it, and what they move under them
        auto inner = pPerform->content();
        pad->setBounds(inner.removeFromLeft(inner.getHeight()));
        inner.removeFromLeft(12);
        auto knobsCol = inner.removeFromLeft(KnobBox::w);
        layoutRow(knobsCol.removeFromTop(KnobBox::h), { performRow[0] });
        knobsCol.removeFromTop(8);
        layoutRow(knobsCol.removeFromTop(KnobBox::h), { performRow[1] });
        inner.removeFromLeft(12);
        routes->setBounds(inner);
    }
    pOut->setBounds(rightCol.removeFromBottom(172));
    rightCol.removeFromBottom(gap);
    pTrem->setBounds(rightCol);
    {
        auto inner = pOut->content();
        layoutRow(inner.removeFromTop(KnobBox::h), { outRow[0], outRow[1], outRow[2] });
        inner.removeFromTop(8);
        layoutRow(inner.withHeight(44), { outRow[3], outRow[4] }, 8);
    }
    {
        auto inner = pTrem->content();
        layoutRow(inner.removeFromTop(44), tremHeadRow, 10);
        inner.removeFromTop(10);
        layoutRow(inner.removeFromTop(KnobBox::h), tremKnobRow, 8);
        inner.removeFromTop(8);
        tremStrip->setBounds(inner);
    }
    {
        auto inner = pMod->content();
        auto lfos = inner.removeFromTop(14 + KnobBox::hSmall + 6);
        auto left = lfos.removeFromLeft(lfos.getWidth() / 2 - 8);
        auto right = lfos.withTrimmedLeft(16);
        lfoCaption[0]->setBounds(left.removeFromTop(14));
        lfoCaption[1]->setBounds(right.removeFromTop(14));
        layoutRow(left.withHeight(KnobBox::hSmall), lfoRow[0], 8);
        layoutRow(right.withHeight(KnobBox::hSmall), lfoRow[1], 8);
        envCaption->setBounds(inner.removeFromTop(14));
        layoutRow(inner.removeFromTop(KnobBox::hSmall), envRow, 10);
        inner.removeFromTop(8);
        modSources->setBounds(inner.removeFromTop(40));
        inner.removeFromTop(8);
        matrixCaption->setBounds(inner.removeFromTop(14));
        inner.removeFromTop(2);
        // each slot on one line: source, target, and its amount with the
        // value beside the knob rather than under it, which used to run into
        // the next row. Only the first slot has captions over its boxes; the
        // others start their (empty) caption strip 8 px up, so the boxes sit
        // on a 36 px pitch
        for (int k = 0; k < 6; ++k){
            auto r = inner.removeFromTop(k == 0 ? 46 : 36);
            if (k > 0) r = r.withTop(r.getY() - 8).withHeight(44);
            layoutRow(r, matrixRow[k], 10);
        }
    }
    {
        auto inner = pScope->content();
        auto metersArea = inner.removeFromRight(96);
        meters->setBounds(metersArea);
        inner.removeFromRight(10);
        scope->setBounds(inner);
    }
}
