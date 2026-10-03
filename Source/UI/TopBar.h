#pragma once

#include "AppContext.h"

namespace lr
{
class TopBar final : public juce::Component
{
public:
    enum View { racksView = 0, routingView, settingsView };

    explicit TopBar (AppContext&);

    void tick();               // 30 Hz: refresh stats
    void setView (View);
    std::function<void (View)> onViewChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx;
    juce::String deviceText, statsText;
    juce::Colour statsColour = theme::dim;

    juce::TextButton racksButton { "Racks" }, routingButton { "Routing" }, settingsButton { "Settings" };
    juce::TextButton bypassAll { "BYPASS ALL" };
};
} // namespace lr
