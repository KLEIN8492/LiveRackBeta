#include "PluginLayout.h"

namespace lr
{
namespace
{
void disableAuxBuses (juce::AudioProcessor::BusesLayout& layout)
{
    for (int i = 1; i < layout.inputBuses.size(); ++i)
        layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();
    for (int i = 1; i < layout.outputBuses.size(); ++i)
        layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();
}

bool tryApply (juce::AudioProcessor& p, const juce::AudioProcessor::BusesLayout& layout)
{
    return p.checkBusesLayoutSupported (layout) && p.setBusesLayout (layout);
}
} // namespace

int negotiateLayout (juce::AudioProcessor& plugin, int rackChannels)
{
    const auto original = plugin.getBusesLayout();
    const auto wanted = rackChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();

    auto rackWidth = original;
    if (! rackWidth.inputBuses.isEmpty())  rackWidth.inputBuses.getReference (0) = wanted;
    if (! rackWidth.outputBuses.isEmpty()) rackWidth.outputBuses.getReference (0) = wanted;
    disableAuxBuses (rackWidth);

    if (tryApply (plugin, rackWidth))
        return 1;

    auto ownWidth = original;
    disableAuxBuses (ownWidth);

    if (ownWidth != original && tryApply (plugin, ownWidth))
        return 2;

    plugin.setBusesLayout (original);
    return 3;
}

} // namespace lr
