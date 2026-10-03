#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Host/PluginHost.h"
#include "App/SessionGuard.h"
#include "Theme.h"

namespace lr
{
struct AppContext
{
    PluginHost& host;
    juce::AudioDeviceManager& devices;
    AudioEngine& engine;
    SessionGuard& session;
    juce::PropertiesFile& settings;

    juce::StringArray inputNames() const
    {
        auto* d = devices.getCurrentAudioDevice();
        return d != nullptr ? d->getInputChannelNames() : juce::StringArray();
    }

    juce::StringArray outputNames() const
    {
        auto* d = devices.getCurrentAudioDevice();
        return d != nullptr ? d->getOutputChannelNames() : juce::StringArray();
    }
};

// Reads a peak meter and decays the displayed value smoothly.
struct MeterSmoother
{
    float shown = 0.0f;
    float update (std::atomic<float>& source)
    {
        const float v = source.exchange (0.0f, std::memory_order_relaxed);
        shown = juce::jmax (v, shown * 0.82f);
        return shown;
    }
};
} // namespace lr
