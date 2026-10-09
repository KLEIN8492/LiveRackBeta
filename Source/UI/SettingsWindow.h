#pragma once

#include "AppContext.h"
#include "RoutingView.h"

namespace lr
{
// Everything that isn't a rack: interfaces, routing, plugins, performance.
// Opened with ⌘, (LiveRack ▸ Settings…) or the SETTINGS button.
class SettingsComponent final : public juce::Component, private juce::Timer
{
public:
    enum Page { audioPage = 0, routingPage, pluginsPage, performancePage, numPages };

    explicit SettingsComponent (AppContext&);
    ~SettingsComponent() override;

    void showPage (Page);
    void saveSnapshots (const juce::File& dir); // one PNG per page

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class AudioPage;
    class PluginsPage;
    class PerformancePage;

    void timerCallback() override;

    AppContext& ctx;
    theme::LookAndFeel lookAndFeel;
    juce::OwnedArray<juce::TextButton> tabs;
    std::unique_ptr<AudioPage> audio;
    std::unique_ptr<RoutingView> routing;
    std::unique_ptr<PluginsPage> plugins;
    std::unique_ptr<PerformancePage> performance;
    Page page = audioPage;
};

class SettingsWindow final : public juce::DocumentWindow
{
public:
    explicit SettingsWindow (AppContext&);
    void closeButtonPressed() override { setVisible (false); }
    SettingsComponent& getContent() { return *content; }

private:
    SettingsComponent* content;
};
} // namespace lr
