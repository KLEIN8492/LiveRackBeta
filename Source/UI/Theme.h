#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Utilitarian theme: neutral greys, square corners, 1px rules, dense rows,
// monospaced numbers. Colour is reserved for state (ok / warn / fault).
namespace lr::theme
{
inline const juce::Colour bg       { 0xff1b1b1b };
inline const juce::Colour panel    { 0xff232323 };
inline const juce::Colour raised   { 0xff2d2d2d };
inline const juce::Colour stripe   { 0xff272727 };   // alternate table rows
inline const juce::Colour line     { 0xff3d3d3d };
inline const juce::Colour text     { 0xffdcdcdc };
inline const juce::Colour dim      { 0xff8f8f8f };
inline const juce::Colour select   { 0xff3a3a3a };
inline const juce::Colour accent   { 0xff58c258 };   // OK / active
inline const juce::Colour warn     { 0xffe0a030 };
inline const juce::Colour fault    { 0xffe24a3b };
inline const juce::Colour critical { 0xffe0a030 };   // CRIT tag

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions ("Menlo", size, bold ? juce::Font::bold : juce::Font::plain));
}

// Small uppercase caption, e.g. column headers and field labels.
inline void caption (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
                     juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (dim);
    g.setFont (font (10.0f, true));
    g.drawText (s.toUpperCase(), r, j, true);
}

// Square status LED.
inline void led (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c)
{
    g.setColour (c);
    g.fillRect (r);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRect (r);
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColourScheme ({ bg, panel, raised, line, text, accent, juce::Colours::black, raised, text });

        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextButton::buttonColourId, raised);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::ComboBox::backgroundColourId, raised);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::ComboBox::arrowColourId, dim);
        setColour (juce::TextEditor::backgroundColourId, bg);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, dim);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, dim);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, select);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::ScrollBar::thumbColourId, line);
        setColour (juce::TooltipWindow::backgroundColourId, panel);
        setColour (juce::TooltipWindow::outlineColourId, line);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TableHeaderComponent::backgroundColourId, raised);
        setColour (juce::TableHeaderComponent::textColourId, dim);
        setColour (juce::TableHeaderComponent::outlineColourId, line);
        setColour (juce::TableHeaderComponent::highlightColourId, select);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds();
        auto c = colour;
        if (down)             c = c.darker (0.25f);
        else if (highlighted) c = c.brighter (0.08f);

        g.setColour (c);
        g.fillRect (r);
        g.setColour (line);
        g.drawRect (r);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return font (juce::jmin (12.0f, (float) height * 0.5f), true);
    }

    void drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool enabled, bool, bool) override
    {
        auto r = juce::Rectangle<float> (x, y, w, h).reduced (2.0f).toNearestInt();
        g.setColour (bg);
        g.fillRect (r);
        g.setColour (line);
        g.drawRect (r);
        if (ticked)
        {
            g.setColour (enabled ? accent : dim);
            g.fillRect (r.reduced (3));
        }
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&) override
    {
        g.setColour (raised);
        g.fillRect (0, 0, width, height);
        g.setColour (line);
        g.drawRect (0, 0, width, height);

        const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
        juce::Path p;
        p.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (dim);
        g.fillPath (p);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down) override
    {
        const int box = 14;
        auto r = b.getLocalBounds();
        drawTickBox (g, b, 0.0f, (float) (r.getCentreY() - box / 2), (float) box, (float) box,
                     b.getToggleState(), b.isEnabled(), highlighted, down);
        g.setColour (b.isEnabled() ? text : dim);
        g.setFont (font (11.0f, true));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (box + 6), juce::Justification::centredLeft, true);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }
    juce::Font getPopupMenuFont() override               { return font (12.5f); }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
        g.fillRect (0, 0, w, h);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        g.setColour (e.hasKeyboardFocus (true) ? dim : line);
        g.drawRect (0, 0, w, h);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (panel);
        g.setColour (line);
        g.drawRect (0, 0, w, h);
    }

    void drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g, const juce::Path&, juce::Image&) override
    {
        g.fillAll (panel);
        g.setColour (line);
        g.drawRect (box.getLocalBounds());
    }

    void drawTooltip (juce::Graphics& g, const juce::String& t, int w, int h) override
    {
        g.fillAll (panel);
        g.setColour (line);
        g.drawRect (0, 0, w, h);
        g.setColour (text);
        g.setFont (font (12.0f));
        g.drawFittedText (t, juce::Rectangle<int> (w, h).reduced (6, 3), juce::Justification::centredLeft, 4);
    }
};

inline int meterSegments (int width) { return juce::jmax (4, width / 5); }

// Lit segments for a peak: what a meter shows, so callers repaint only when it changes.
inline int meterSegmentsLit (float peak, int width)
{
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    return juce::roundToInt (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * (float) meterSegments (width));
}

// Segmented horizontal peak meter: green to -12 dB, amber to -3 dB, red above.
inline void drawMeter (juce::Graphics& g, juce::Rectangle<int> r, float peak)
{
    g.setColour (bg);
    g.fillRect (r);

    const int segments = meterSegments (r.getWidth());
    const int lit = meterSegmentsLit (peak, r.getWidth());
    const float segW = (float) r.getWidth() / (float) segments;

    for (int i = 0; i < segments; ++i)
    {
        const float segDb = -60.0f + 60.0f * ((float) i + 1.0f) / (float) segments;
        auto c = segDb > -3.0f ? fault : segDb > -12.0f ? warn : accent;
        g.setColour (i < lit ? c : c.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> ((float) r.getX() + (float) i * segW, (float) r.getY(),
                                            segW - 1.0f, (float) r.getHeight()));
    }
}
} // namespace lr::theme
