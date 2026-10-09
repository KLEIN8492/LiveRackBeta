#include "PortRef.h"
#include <algorithm>

namespace lr
{
juce::String encodePorts (const std::vector<PortRef>& ports)
{
    juce::StringArray parts;
    for (auto& p : ports)
        parts.add (juce::URL::addEscapeChars (p.device, false) + "#" + juce::String (p.channel));
    return parts.joinIntoString (";");
}

std::vector<PortRef> decodePorts (const juce::String& text)
{
    std::vector<PortRef> ports;
    for (auto& part : juce::StringArray::fromTokens (text, ";", ""))
    {
        const int hash = part.lastIndexOfChar ('#');
        if (hash <= 0)
            continue;

        const auto number = part.substring (hash + 1).trim();
        if (number.isEmpty() || ! number.containsOnly ("0123456789"))
            continue;

        ports.push_back ({ juce::URL::removeEscapeChars (part.substring (0, hash)), number.getIntValue() });
    }
    return ports;
}

bool isLegacyChannelList (const juce::String& text)
{
    return text.isNotEmpty() && text.containsOnly ("0123456789, ");
}

void togglePort (std::vector<PortRef>& ports, const PortRef& port)
{
    auto it = std::find (ports.begin(), ports.end(), port);
    if (it != ports.end())
        ports.erase (it);
    else
        ports.push_back (port);
    std::sort (ports.begin(), ports.end());
}

} // namespace lr
