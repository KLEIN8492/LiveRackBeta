#include "ShowModel.h"

namespace lr::show
{
static constexpr int currentVersion = 1;

juce::ValueTree createEmpty()
{
    juce::ValueTree show (ids::SHOW);
    show.setProperty (ids::version, currentVersion, nullptr);
    show.appendChild (juce::ValueTree (ids::RACKS), nullptr);
    return show;
}

juce::ValueTree getRacks (const juce::ValueTree& show)
{
    return show.getChildWithName (ids::RACKS);
}

juce::ValueTree addRack (juce::ValueTree show, const juce::String& name, int channels)
{
    juce::ValueTree rack (ids::RACK);
    rack.setProperty (ids::id, juce::Uuid().toString(), nullptr);
    rack.setProperty (ids::name, name, nullptr);
    rack.setProperty (ids::channels, juce::jlimit (1, 2, channels), nullptr);
    rack.setProperty (ids::critical, false, nullptr);
    rack.setProperty (ids::bypassed, false, nullptr);
    rack.setProperty (ids::inputs, "", nullptr);
    rack.setProperty (ids::sources, "", nullptr);
    rack.setProperty (ids::outputs, "", nullptr);

    auto racks = getRacks (show);
    if (! racks.isValid())
    {
        racks = juce::ValueTree (ids::RACKS);
        show.appendChild (racks, nullptr);
    }
    racks.appendChild (rack, nullptr);
    return rack;
}

juce::ValueTree addSlot (juce::ValueTree rack, const juce::PluginDescription& desc, int index)
{
    juce::ValueTree slot (ids::SLOT);
    slot.setProperty (ids::id, juce::Uuid().toString(), nullptr);
    slot.setProperty (ids::bypassed, false, nullptr);
    slot.setProperty (ids::faultBypassed, false, nullptr);

    if (auto xml = desc.createXml())
        slot.setProperty (ids::plugin, xml->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);

    slot.setProperty (ids::state, "", nullptr);
    rack.addChild (slot, index, nullptr);
    return slot;
}

juce::ValueTree findRack (const juce::ValueTree& show, const juce::String& rackId)
{
    return getRacks (show).getChildWithProperty (ids::id, rackId);
}

juce::ValueTree findSlot (const juce::ValueTree& show, const juce::String& slotId)
{
    for (auto rack : getRacks (show))
    {
        auto slot = rack.getChildWithProperty (ids::id, slotId);
        if (slot.isValid())
            return slot;
    }
    return {};
}

std::vector<int> parseChannels (const juce::String& text)
{
    std::vector<int> result;
    for (auto& token : juce::StringArray::fromTokens (text, ",", ""))
        if (token.trim().isNotEmpty())
            result.push_back (token.trim().getIntValue());
    return result;
}

juce::String formatChannels (const std::vector<int>& channels)
{
    juce::StringArray parts;
    for (int c : channels)
        parts.add (juce::String (c));
    return parts.joinIntoString (",");
}

juce::StringArray parseIds (const juce::String& text)
{
    auto ids = juce::StringArray::fromTokens (text, ",", "");
    ids.trim();
    ids.removeEmptyStrings();
    return ids;
}

std::vector<int> getChannels (const juce::ValueTree& rack, const juce::Identifier& prop)
{
    return parseChannels (rack[prop].toString());
}

void setChannels (juce::ValueTree rack, const juce::Identifier& prop, const std::vector<int>& channels)
{
    rack.setProperty (prop, formatChannels (channels), nullptr);
}

std::vector<PortRef> getPorts (const juce::ValueTree& rack, const juce::Identifier& prop)
{
    return decodePorts (rack[prop].toString());
}

void setPorts (juce::ValueTree rack, const juce::Identifier& prop, const std::vector<PortRef>& ports)
{
    rack.setProperty (prop, encodePorts (ports), nullptr);
}

int migrateLegacyPorts (juce::ValueTree show, const juce::String& inputDevice, const juce::String& outputDevice)
{
    int changed = 0;
    for (auto rack : getRacks (show))
    {
        bool touched = false;
        for (auto [prop, device] : { std::pair { ids::inputs, inputDevice }, std::pair { ids::outputs, outputDevice } })
        {
            const auto text = rack[prop].toString();
            if (! isLegacyChannelList (text))
                continue;

            std::vector<PortRef> ports;
            if (device.isNotEmpty())
                for (int ch : parseChannels (text))
                    ports.push_back ({ device, ch });

            setPorts (rack, prop, ports);
            touched = true;
        }
        changed += touched ? 1 : 0;
    }
    return changed;
}

static juce::String channelSummary (std::vector<int> chans);

juce::String describePorts (const std::vector<PortRef>& ports)
{
    if (ports.empty())
        return "-";

    juce::StringArray groups;
    size_t i = 0;
    while (i < ports.size())
    {
        std::vector<int> chans;
        const auto device = ports[i].device;
        for (; i < ports.size() && ports[i].device == device; ++i)
            chans.push_back (ports[i].channel);
        groups.add (device + " " + channelSummary (chans));
    }
    return groups.joinIntoString (", ");
}

bool getPluginDescription (const juce::ValueTree& slot, juce::PluginDescription& out)
{
    if (auto xml = juce::parseXML (slot[ids::plugin].toString()))
        return out.loadFromXml (*xml);
    return false;
}

static juce::String channelSummary (std::vector<int> chans)
{
    if (chans.empty())
        return "-";

    std::sort (chans.begin(), chans.end());
    const bool contiguous = chans.back() - chans.front() + 1 == (int) chans.size();

    if (chans.size() == 1)
        return juce::String (chans[0] + 1);
    if (contiguous)
        return juce::String (chans.front() + 1) + "-" + juce::String (chans.back() + 1);

    juce::StringArray parts;
    for (int c : chans)
        parts.add (juce::String (c + 1));
    return parts.joinIntoString (",");
}

juce::String describeRouting (const juce::ValueTree& rack, const juce::ValueTree& show)
{
    juce::String in;
    auto sources = parseIds (rack[ids::sources].toString());

    if (! sources.isEmpty())
    {
        juce::StringArray names;
        for (auto& id : sources)
            names.add (findRack (show, id)[ids::name].toString());
        in = "Bus of " + names.joinIntoString (", ");
    }
    else
    {
        in = describePorts (getPorts (rack, ids::inputs));
    }

    return in + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + describePorts (getPorts (rack, ids::outputs));
}

bool saveToFile (const juce::ValueTree& show, const juce::File& file)
{
    auto xml = show.createXml();
    if (xml == nullptr)
        return false;

    file.getParentDirectory().createDirectory();
    auto temp = file.getSiblingFile (file.getFileName() + ".tmp");
    return xml->writeTo (temp) && temp.moveFileTo (file);
}

juce::ValueTree loadFromFile (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (ids::SHOW))
        {
            if (! getRacks (tree).isValid())
                tree.appendChild (juce::ValueTree (ids::RACKS), nullptr);
            return tree;
        }
    }
    return {};
}

} // namespace lr::show
