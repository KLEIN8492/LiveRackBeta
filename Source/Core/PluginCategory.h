#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lr
{
// Live-mixing categories for the plugin browser.
enum class PluginCategory
{
    eq, compressor, limiter, gate, deEsser, multiband, channelStrip,
    reverb, delay, modulation, pitch, saturation, ampSim,
    feedback, restoration, multiFx, utility, instrument, other,
    count
};

const char* displayName (PluginCategory);

// Combines the format's own category (VST3 "Fx|Dynamics" etc.) with the
// plugin's name and maker, since many plugins are only labelled "Fx".
// Name rules win over the format category (a "Dynamics" plugin called
// "Pro-DS" is a de-esser).
PluginCategory classify (const juce::PluginDescription&);

// Stable key for storing a user's override ("Set category" in the browser).
juce::String categoryKey (PluginCategory);
PluginCategory categoryFromKey (const juce::String&); // `other` if unknown

} // namespace lr
