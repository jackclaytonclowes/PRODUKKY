// Guide.h — the built-in guide: GUIDE.md, compiled into the plugin and drawn
// over the panel when the Guide button is on. The same file is the one people
// read on disk, so the two can never say different things.
//
// It draws a small subset of Markdown, which is all GUIDE.md uses: a # title,
// ## section headings, paragraphs, "- " bullets and **bold** words. Anything
// else comes out as plain text rather than as markup.
//
// Shared, identically, between CRATE and FRACTURE (Source/Guide.h in each).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Bauhaus.h"

namespace bauhaus {

class GuideText : public juce::Component {
public:
    void setText(const juce::String& markdown){ source = markdown; relayout(getWidth()); }

    // lay the text out at a width, and size to fit it
    void relayout(int width){
        if (width <= 0) return;
        blocks.clear();
        // a readable measure: past about 90 characters a line is hard to
        // track back from, so the column stops at 780 px however wide the panel
        const float inner = static_cast<float>(std::min(width - 2 * pad, 780));
        left = std::max(pad, static_cast<int>((width - inner) / 2.0f));   // centred when there is room
        float y = static_cast<float>(pad);
        juce::String para;
        auto flush = [&]{
            if (para.isNotEmpty()){ add(Kind::Para, para, inner, y); para.clear(); }
        };
        for (auto line : juce::StringArray::fromLines(source)){
            line = line.trimEnd();
            if (line.isEmpty()){ flush(); continue; }
            if (line.startsWith("# "))       { flush(); add(Kind::Title, line.substring(2), inner, y); }
            else if (line.startsWith("## ")) { flush(); add(Kind::Heading, line.substring(3), inner, y); }
            else if (line.startsWith("- "))  { flush(); add(Kind::Bullet, line.substring(2), inner, y); }
            else para << (para.isEmpty() ? "" : " ") << line;
        }
        flush();
        setSize(width, static_cast<int>(y) + pad);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(face);
        for (const auto& b : blocks){
            if (b.kind == Kind::Heading){
                g.setColour(ink);
                g.fillRect(juce::Rectangle<float>(static_cast<float>(left), b.area.getY() - 10.0f, 28.0f, 3.0f));
            }
            if (b.kind == Kind::Bullet){
                g.setColour(red);
                g.fillRect(juce::Rectangle<float>(static_cast<float>(left) + 2.0f, b.area.getY() + 7.0f, 6.0f, 6.0f));
            }
            b.layout.draw(g, b.area);
        }
    }

private:
    enum class Kind { Title, Heading, Para, Bullet };
    struct Block { Kind kind; juce::TextLayout layout; juce::Rectangle<float> area; };
    static constexpr int pad = 28;

    // **bold** becomes bold, and nothing else is treated as markup
    static juce::AttributedString styled(const juce::String& text, const juce::Font& regular,
                                         const juce::Font& strong, juce::Colour colour){
        juce::AttributedString s;
        s.setWordWrap(juce::AttributedString::byWord);
        bool bold = false;
        juce::String pending;
        for (int i = 0; i < text.length(); ++i){
            if (text[i] == '*' && i + 1 < text.length() && text[i + 1] == '*'){
                if (pending.isNotEmpty()) s.append(pending, bold ? strong : regular, colour);
                pending.clear(); bold = !bold; ++i;
            } else pending += juce::String::charToString(text[i]);
        }
        if (pending.isNotEmpty()) s.append(pending, bold ? strong : regular, colour);
        return s;
    }

    void add(Kind kind, const juce::String& text, float width, float& y){
        Block b; b.kind = kind;
        float indent = 0.0f, before = 0.0f, after = 8.0f;
        juce::AttributedString s;
        switch (kind){
        case Kind::Title:   s = styled(text, grot(30.0f, true), grot(30.0f, true), ink); after = 14.0f; break;
        case Kind::Heading: s = styled(text, grot(17.0f, true), grot(17.0f, true), ink); before = 18.0f; after = 6.0f; break;
        case Kind::Bullet:  s = styled(text, grot(14.0f), grot(14.0f, true), ink); indent = 18.0f; after = 6.0f; break;
        case Kind::Para:    s = styled(text, grot(14.0f), grot(14.0f, true), dim); after = 10.0f; break;
        }
        y += before;
        b.layout.createLayout(s, width - indent);
        b.area = { static_cast<float>(left) + indent, y, width - indent, b.layout.getHeight() };
        y += b.layout.getHeight() + after;
        blocks.push_back(std::move(b));
    }

    juce::String source;
    std::vector<Block> blocks;
    int left = pad;
};

// the overlay: a bordered panel with the guide in a scrolling view
class GuideOverlay : public juce::Component {
public:
    explicit GuideOverlay(const juce::String& markdown){
        text.setText(markdown);
        view.setViewedComponent(&text, false);
        view.setScrollBarsShown(true, false);
        view.setScrollBarThickness(10);
        view.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, ink);
        view.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId, track);
        addAndMakeVisible(view);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(3);
        view.setBounds(r);
        text.relayout(r.getWidth() - view.getScrollBarThickness());
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(face);
        g.setColour(ink); g.drawRect(getLocalBounds(), 3);
    }
    // the tests use it to check the guide really laid out
    int contentHeight() const { return text.getHeight(); }
private:
    GuideText text;
    juce::Viewport view;
};

} // namespace bauhaus
