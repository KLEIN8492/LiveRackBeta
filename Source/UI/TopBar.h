#pragma once

#include "AppContext.h"

namespace lr
{
// Status strip: boxed readouts (device, rate, buffer, round trip, DSP,
// dropouts, boost), view switches and the BYPASS ALL panic button.
class TopBar final : public juce::Component
{
public:
    enum View { racksView = 0, routingView, settingsView };

    explicit TopBar (AppContext&);

    void tick();               // 30 Hz: refresh readouts
    void setView (View);
    std::function<void (View)> onViewChanged;

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

    juce::TextButton racksButton { "RACKS" }, routingButton { "ROUTING" }, settingsButton { "SETTINGS" };
    juce::TextButton bypassAll { "BYPASS ALL" };
};
} // namespace lr
