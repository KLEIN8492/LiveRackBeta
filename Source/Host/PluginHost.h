#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Core/AudioEngine.h"
#include "Core/InflightLog.h"
#include "Core/ShowModel.h"
#include <map>
#include <set>

namespace lr
{
class PluginWindow;

// Owns plugin instances and keeps the audio engine in sync with the show:
// listens to the ValueTree, loads plugins asynchronously, prepares them for
// the current device and rebuilds/swaps the RenderPlan after every change.
class PluginHost final : private juce::ValueTree::Listener,
                         private juce::AsyncUpdater,
                         private juce::ChangeListener
{
public:
    PluginHost (juce::AudioDeviceManager&, AudioEngine&, InflightLog&);
    ~PluginHost() override;

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() noexcept           { return knownPlugins; }
    void loadKnownPlugins (const juce::File&);
    void saveKnownPlugins (const juce::File&) const;

    void setShow (juce::ValueTree newShow);
    juce::ValueTree getShow() const                             { return show; }

    std::shared_ptr<SlotRuntime> getSlot (const juce::String& slotId) const;
    std::shared_ptr<RackRuntime> getRack (const juce::String& rackId) const;
    juce::String getSlotError (const juce::String& slotId) const;
    juce::String getRoutingError() const                        { return routingError; }

    int getSlotLatencySamples (const juce::String& slotId) const;
    double getSampleRate() const;

    // Writes every plugin's current state into its SLOT node (message thread).
    void captureStates();

    void openEditor (const juce::String& slotId);
    void closeEditor (const juce::String& slotId);

    void rearmSlot (const juce::String& slotId);
    void restoreRack (const juce::String& rackId);

    // Called on the message thread whenever plugins load/fail or the plan changes.
    std::function<void()> onStateChanged;

private:
    void attachSlot (juce::ValueTree slot);
    void detachSlot (const juce::String& slotId);
    void createInstance (juce::ValueTree slot);
    void configure (juce::AudioPluginInstance&, int rackChannels);
    void prepare (juce::AudioPluginInstance&);
    void reconfigureSlots (const std::vector<juce::String>& slotIds);
    void ensureAllChannelsActive();

    void rebuildPlan();
    void notify();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::AudioDeviceManager& deviceManager;
    AudioEngine& engine;
    InflightLog& inflight;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    juce::ValueTree show;
    std::map<juce::String, std::shared_ptr<SlotRuntime>> slots;
    std::map<juce::String, std::shared_ptr<RackRuntime>> racks;
    std::map<juce::String, juce::String> slotErrors;
    std::map<juce::String, std::unique_ptr<PluginWindow>> windows;
    std::set<juce::String> excluded;   // slots temporarily kept out of the plan
    juce::String routingError;

    double preparedRate = 0.0;
    int preparedBlock = 0;

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace lr
