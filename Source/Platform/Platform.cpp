// Platform functions that are the same everywhere, or differ only in data.
#include "Platform.h"

namespace lr::platform
{
namespace
{
const juce::String alsaHardwareSuffix = "; Direct hardware device without any conversions";
}

juce::String driverLabel (const juce::AudioIODeviceType& type)
{
    const auto name = type.getTypeName();
    if (name.startsWith ("Windows Audio")) return "WASAPI";
    if (name == "CoreAudio")               return "Core Audio";
    return name; // "ASIO", "ALSA", "JACK"
}

bool probingLoadsDriver (const juce::AudioIODeviceType& type)
{
    return type.getTypeName() == "ASIO";
}

bool offerDevice (const juce::AudioIODeviceType& type, const juce::String& deviceName)
{
    if (type.getTypeName() == "ALSA")
        return deviceName.endsWith (alsaHardwareSuffix);
    return deviceName.isNotEmpty();
}

juce::String shortDeviceName (const juce::AudioIODeviceType& type, const juce::String& deviceName)
{
    if (type.getTypeName() == "ALSA")
        return deviceName.upToLastOccurrenceOf (alsaHardwareSuffix, false, false);
    return deviceName;
}

juce::File helperExecutable (const juce::String& name)
{
   #if JUCE_WINDOWS
    const auto file = name + ".exe";
   #else
    const auto file = name;
   #endif
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile (file);
}

juce::File supportDirectory()
{
    // ~/Library/Application Support/LiveRack, %APPDATA%\LiveRack, ~/.config/LiveRack
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    return base.getChildFile ("Application Support/LiveRack");
   #else
    return base.getChildFile ("LiveRack");
   #endif
}

juce::File relaunchTarget()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile);
   #else
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #endif
}

juce::String monospaceFontName()
{
   #if JUCE_MAC
    return "Menlo";
   #elif JUCE_WINDOWS
    return "Consolas";
   #else
    return "DejaVu Sans Mono";
   #endif
}

juce::String secondaryPluginFormat()
{
   #if JUCE_MAC
    return "AudioUnit";
   #elif JUCE_LINUX
    return "LV2";
   #else
    return {};
   #endif
}
} // namespace lr::platform
