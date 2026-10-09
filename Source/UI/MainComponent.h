#pragma once

#include "AppContext.h"
#include "TopBar.h"
#include "RackList.h"
#include "RackView.h"

namespace lr
{
// The main window: status strip, rack list and the selected rack.
// Everything else lives in the Settings window (⌘,).
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (AppContext&);
    ~MainComponent() override;

    void showBanner (const juce::String& text, juce::Colour colour);
    void showChanged();   // a new show was loaded
    void saveSnapshot (const juce::File& png);   // debug / docs

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AppContext& ctx;
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    TopBar topBar;
    RackList rackList;
    RackView rackView;

    juce::String bannerText;
    juce::Colour bannerColour;
    juce::TextButton bannerClose { "DISMISS" };
};
} // namespace lr
