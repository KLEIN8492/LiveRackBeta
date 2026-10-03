#pragma once

#include "AppContext.h"

namespace lr
{
// Audio device selection and plugin scanning.
class SettingsView final : public juce::Component
{
public:
    explicit SettingsView (AppContext&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::PluginListComponent pluginList;
    juce::TextButton scanVst3 { "Scan VST3" }, scanAu { "Scan Audio Units" };
    void scan (const juce::String& formatName);
};
} // namespace lr
