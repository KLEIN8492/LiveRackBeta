#pragma once

#include "AppContext.h"
#include "TopBar.h"
#include "RackList.h"
#include "RackView.h"
#include "RoutingView.h"
#include "SettingsView.h"

namespace lr
{
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (AppContext&);
    ~MainComponent() override;

    void showBanner (const juce::String& text, juce::Colour colour);
    void showChanged();   // a new show was loaded
    void saveSnapshots (const juce::File& dir);  // one PNG per view (debug / docs)

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setView (TopBar::View);

    AppContext& ctx;
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    TopBar topBar;
    RackList rackList;
    RackView rackView;
    RoutingView routingView;
    std::unique_ptr<SettingsView> settingsView;

    juce::String bannerText;
    juce::Colour bannerColour;
    juce::TextButton bannerClose { "DISMISS" };
    TopBar::View view = TopBar::racksView;
};
} // namespace lr
