#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Flat, dark, high-contrast theme meant for a dim FOH position.
namespace lr::theme
{
inline const juce::Colour bg       { 0xff131518 };
inline const juce::Colour panel    { 0xff1b1e23 };
inline const juce::Colour raised   { 0xff252930 };
inline const juce::Colour line     { 0xff2f343c };
inline const juce::Colour text     { 0xffe8eaed };
inline const juce::Colour dim      { 0xff8a919b };
inline const juce::Colour accent   { 0xff3ecf8e };
inline const juce::Colour warn     { 0xfff5a623 };
inline const juce::Colour fault    { 0xffff5757 };
inline const juce::Colour critical { 0xffb18cff };

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColourScheme ({ bg, panel, raised, line, text, accent, juce::Colours::black, raised, text });

        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextButton::buttonColourId, raised);
        setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.85f));
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::ComboBox::backgroundColourId, raised);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::TextEditor::backgroundColourId, raised);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::PopupMenu::backgroundColourId, raised);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = colour;
        if (down)             c = c.brighter (0.15f);
        else if (highlighted) c = c.brighter (0.07f);

        g.setColour (c);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (line);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return font (juce::jmin (14.0f, (float) height * 0.55f));
    }
};

// Small helper: draws a horizontal peak meter.
inline void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float peak)
{
    g.setColour (raised);
    g.fillRoundedRectangle (r, 2.0f);

    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    const auto colour = db > -1.0f ? fault : db > -12.0f ? warn : accent;

    g.setColour (colour);
    g.fillRoundedRectangle (r.withWidth (r.getWidth() * proportion), 2.0f);
}
} // namespace lr::theme
