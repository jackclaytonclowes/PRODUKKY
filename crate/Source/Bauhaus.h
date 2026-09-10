// Bauhaus.h — the house look: Béton clair, shared with FRACTURE so the two
// products read as coming from the same bench.
//
// Flat colour, zero radius, no shadows or gradients. Colour is functional:
// yellow adds harmonics, red destroys or limits, blue shapes, ink is structure —
// so a knob's arc says what kind of thing it does. Each Panel carries a hue and
// its controls inherit it.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace bauhaus {

const juce::Colour ground  { 0xffd6d1c6 };
const juce::Colour panelBg { 0xffe9e5dc };
const juce::Colour face    { 0xfff4f1ea };
const juce::Colour track   { 0xffbdb7aa };
const juce::Colour dim2    { 0xff8d877c };
const juce::Colour dim     { 0xff5f5a51 };
const juce::Colour ink     { 0xff17150f };
const juce::Colour yellow  { 0xffe9b21f };
const juce::Colour red     { 0xffc0392f };
const juce::Colour blue    { 0xff1e4b8f };

inline juce::Font grot(float height, bool bold = false){
    return juce::Font(juce::FontOptions()
                          .withName(juce::Font::getDefaultSansSerifFontName())
                          .withHeight(height)
                          .withStyle(bold ? "Bold" : "Regular"));
}
inline juce::Font mono(float height){
    return juce::Font(juce::FontOptions()
                          .withName(juce::Font::getDefaultMonospacedFontName())
                          .withHeight(height));
}
// small caps with the wide tracking the design uses everywhere
inline void drawTracked(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                        float height, float tracking, juce::Justification just, juce::Colour colour){
    g.setColour(colour);
    g.setFont(grot(height, true));
    const juce::String up = text.toUpperCase();
    juce::String spaced;
    for (int i = 0; i < up.length(); ++i){ spaced << up[i]; }
    // JUCE has no letter-spacing, so draw glyph by glyph
    const auto& font = g.getCurrentFont();
    float total = 0.0f;
    for (int i = 0; i < up.length(); ++i) total += juce::GlyphArrangement::getStringWidth(font, juce::String::charToString(up[i])) + tracking;
    float x = static_cast<float>(area.getX());
    if (just.testFlags(juce::Justification::horizontallyCentred)) x += (area.getWidth() - total) * 0.5f;
    else if (just.testFlags(juce::Justification::right)) x += area.getWidth() - total;
    const float baseline = area.getCentreY() + height * 0.35f;
    for (int i = 0; i < up.length(); ++i){
        const juce::String ch = juce::String::charToString(up[i]);
        g.drawSingleLineText(ch, juce::roundToInt(x), juce::roundToInt(baseline));
        x += juce::GlyphArrangement::getStringWidth(font, ch) + tracking;
    }
}

// ------------------------------------------------------------------- the knob
class Look : public juce::LookAndFeel_V4 {
public:
    Look(){
        setColour(juce::ComboBox::backgroundColourId, face);
        setColour(juce::ComboBox::textColourId, ink);
        setColour(juce::ComboBox::outlineColourId, ink);
        setColour(juce::ComboBox::arrowColourId, ink);
        setColour(juce::PopupMenu::backgroundColourId, face);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, ink);
        setColour(juce::PopupMenu::highlightedTextColourId, face);
        setColour(juce::TextButton::buttonColourId, face);
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::TextButton::textColourOnId, ink);
        setColour(juce::Label::textColourId, ink);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider& slider) override {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(1.0f);
        const float d = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float r = d * 0.5f;
        const float ringThickness = d * 0.152f;          // 7px on a 46px dial
        const auto fill = slider.findColour(juce::Slider::rotarySliderFillColourId);

        // the arc: from the centre for a bipolar parameter, from the start otherwise
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                             && std::abs(slider.getMinimum() + slider.getMaximum()) < 1.0e-3;
        const float anchor = bipolar ? 0.5f : 0.0f;
        const float a0 = startAngle + juce::jmin(anchor, pos) * (endAngle - startAngle);
        const float a1 = startAngle + juce::jmax(anchor, pos) * (endAngle - startAngle);

        juce::Path ring;
        ring.addCentredArc(centre.x, centre.y, r - ringThickness * 0.5f, r - ringThickness * 0.5f,
                           0.0f, startAngle, endAngle, true);
        g.setColour(track);
        g.strokePath(ring, juce::PathStrokeType(ringThickness));

        if (a1 > a0 + 1.0e-4f){
            juce::Path arc;
            arc.addCentredArc(centre.x, centre.y, r - ringThickness * 0.5f, r - ringThickness * 0.5f,
                              0.0f, a0, a1, true);
            g.setColour(fill);
            g.strokePath(arc, juce::PathStrokeType(ringThickness));
        }

        // plaster face inside an ink ring
        const float fr = r - ringThickness;
        g.setColour(face);
        g.fillEllipse(centre.x - fr, centre.y - fr, fr * 2.0f, fr * 2.0f);
        g.setColour(ink);
        g.drawEllipse(centre.x - fr, centre.y - fr, fr * 2.0f, fr * 2.0f, 1.5f);

        // pointer
        const float angle = startAngle + pos * (endAngle - startAngle);
        juce::Path pointer;
        pointer.addRectangle(-1.25f, -r * 0.66f, 2.5f, r * 0.66f);
        g.setColour(ink);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre));

        // modulation tick, set through the slider's component properties
        const juce::var modVar = slider.getProperties()["modNorm"];
        if (!modVar.isVoid()){
            const float m = static_cast<float>(modVar);
            const float ma = startAngle + juce::jlimit(0.0f, 1.0f, m) * (endAngle - startAngle);
            juce::Path tick;
            tick.addRectangle(-2.0f, -r - 6.0f, 4.0f, 8.0f);
            g.setColour(blue);
            g.fillPath(tick, juce::AffineTransform::rotation(ma).translated(centre));
        }
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                      juce::ComboBox&) override {
        g.setColour(face);
        g.fillRect(0, 0, width, height);
        g.setColour(ink);
        g.drawRect(juce::Rectangle<int>(0, 0, width, height), 2);
        juce::Path caret;                                     // flat triangle, no chevron
        const float cx = static_cast<float>(width) - 13.0f, cy = height * 0.5f;
        caret.addTriangle(cx - 5.0f, cy - 2.5f, cx + 5.0f, cy - 2.5f, cx, cy + 3.5f);
        g.setColour(ink);
        g.fillPath(caret);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return grot(13.0f); }
    juce::Font getPopupMenuFont() override { return grot(13.0f); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(7, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont(grot(13.0f));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool over, bool down) override {
        const bool on = b.getToggleState();
        const juce::Colour hue = b.findColour(juce::TextButton::buttonOnColourId, true);
        g.setColour(on ? hue : (down || over ? ground : face));
        g.fillRect(b.getLocalBounds());
        g.setColour(ink);
        g.drawRect(b.getLocalBounds(), 2);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        const bool on = b.getToggleState();
        const juce::Colour hue = b.findColour(juce::TextButton::buttonOnColourId, true);
        const bool light = on && hue.getPerceivedBrightness() < 0.6f;
        drawTracked(g, b.getButtonText(), b.getLocalBounds(), 10.0f, 1.4f,
                    juce::Justification::centred, light ? face : ink);
    }
};

} // namespace bauhaus
