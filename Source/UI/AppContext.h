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
    DeviceHub& devices;
    SessionGuard& session;
    juce::PropertiesFile& settings;
    std::function<void()> openSettings;

    // Interfaces whose channels a rack refers to but that aren't open.
    juce::StringArray missingInterfaces (const juce::ValueTree& rack) const
    {
        juce::StringArray missing;
        for (auto& prop : { ids::inputs, ids::outputs })
            for (auto& p : show::getPorts (rack, prop))
                if (devices.deviceFor (p.device) == nullptr)
                    missing.addIfNotAlreadyThere (p.device);
        return missing;
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
