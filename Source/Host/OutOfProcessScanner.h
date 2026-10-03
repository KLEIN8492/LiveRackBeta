#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lr
{
// Scans each plugin file in a separate LiveRackScanner process. A plugin that
// crashes or hangs while being scanned only kills that child; the file is then
// blacklisted by KnownPluginList and the app carries on.
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessScanner (juce::File scannerExecutable);

    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>&,
                             const juce::String& fileOrIdentifier) override;

    static constexpr int timeoutMs = 120000; // Waves shells hold hundreds of plugins

private:
    juce::File scanner;
};

} // namespace lr
