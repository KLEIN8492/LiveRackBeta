#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Ids.h"
#include <vector>

// Helpers for the show ValueTree (schema in Ids.h).
namespace lr::show
{
juce::ValueTree createEmpty();
juce::ValueTree getRacks (const juce::ValueTree& show);

juce::ValueTree addRack (juce::ValueTree show, const juce::String& name, int channels);
juce::ValueTree addSlot (juce::ValueTree rack, const juce::PluginDescription&, int index = -1);

juce::ValueTree findRack (const juce::ValueTree& show, const juce::String& rackId);
juce::ValueTree findSlot (const juce::ValueTree& show, const juce::String& slotId);

std::vector<int> parseChannels (const juce::String&);
juce::String formatChannels (const std::vector<int>&);
juce::StringArray parseIds (const juce::String&);

std::vector<int> getChannels (const juce::ValueTree& rack, const juce::Identifier& prop);
void setChannels (juce::ValueTree rack, const juce::Identifier& prop, const std::vector<int>&);

bool getPluginDescription (const juce::ValueTree& slot, juce::PluginDescription& out);

// Human-readable 1-based summary, e.g. "In 1-2 → Out 3-4".
juce::String describeRouting (const juce::ValueTree& rack, const juce::ValueTree& show);

bool saveToFile (const juce::ValueTree& show, const juce::File&);
juce::ValueTree loadFromFile (const juce::File&);

} // namespace lr::show
