#pragma once

#include "AppContext.h"

namespace lr
{
// Status strip: boxed readouts across all open interfaces, SETTINGS (⌘,) and
// the BYPASS ALL panic button.
class TopBar final : public juce::Component
{
public:
    explicit TopBar (AppContext&);

    void tick();               // 30 Hz: refresh readouts

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Field
    {
        juce::String label, value;
        int width;
        juce::Colour colour;
        bool operator!= (const Field& o) const { return value != o.value || colour != o.colour; }
    };

    AppContext& ctx;
    std::vector<Field> fields;

    juce::TextButton settingsButton { juce::String (juce::CharPointer_UTF8 ("SETTINGS  \xe2\x8c\x98,")) };
    juce::TextButton bypassAll { "BYPASS ALL" };
};
} // namespace lr
