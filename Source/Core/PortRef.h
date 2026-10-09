#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace lr
{
// One channel of one audio interface, e.g. { "Dante Virtual Soundcard", 3 }.
// Channels are 0-based; the UI shows them 1-based.
struct PortRef
{
    juce::String device;
    int channel = 0;

    bool operator== (const PortRef& o) const { return channel == o.channel && device == o.device; }
    bool operator!= (const PortRef& o) const { return ! (*this == o); }
    bool operator<  (const PortRef& o) const
    {
        const int c = device.compareNatural (o.device);
        return c != 0 ? c < 0 : channel < o.channel;
    }
};

// "name#ch;name#ch" with names URL-escaped, so any device name is safe.
juce::String encodePorts (const std::vector<PortRef>&);
std::vector<PortRef> decodePorts (const juce::String&);

// v1 shows stored plain comma-separated channel numbers ("0,1").
bool isLegacyChannelList (const juce::String&);

// Adds the port if absent, removes it if present; keeps the list sorted.
void togglePort (std::vector<PortRef>&, const PortRef&);

} // namespace lr
