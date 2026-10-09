#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Core/DeviceHub.h"
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
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    PluginHost (DeviceHub&, InflightLog&);
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
    juce::String getHome (const juce::String& rackId) const;   // interface a rack runs on
    int getRackLatency (const juce::String& rackId) const;     // total samples, constant
    int getAlignedLatency() const noexcept                      { return alignedLatency; }
    void setAlignRacks (bool);
    bool isAligningRacks() const noexcept                       { return alignRacks; }
    DeviceHub& getDevices() noexcept                            { return devices; }

    int getSlotLatencySamples (const juce::String& slotId) const;
    double getSampleRate() const;

    // Writes plugin states into their SLOT nodes (message thread). Saving state
    // interrupts a plugin (many lock against their own processing), so the
    // autosave only asks plugins that reported an edit or have an editor open.
    void captureStates (bool onlyChanged = false);
    bool hasUncapturedState() const;

    void openEditor (const juce::String& slotId);
    void closeEditor (const juce::String& slotId);

    void rearmSlot (const juce::String& slotId);
    void restoreRack (const juce::String& rackId);

    // Called on the message thread whenever plugins load/fail or the plan changes.
    std::function<void()> onStateChanged;

private:
    class SlotListener;

    void attachSlot (juce::ValueTree slot);
    void detachSlot (const juce::String& slotId);
    void createInstance (juce::ValueTree slot);
    bool resolveScanned (const juce::PluginDescription& wanted, juce::PluginDescription& out) const;
    void configure (juce::AudioPluginInstance&, int rackChannels);
    void prepare (juce::AudioPluginInstance&);
    void reconfigureSlots (const std::vector<juce::String>& slotIds);
    void prepareUnprepared();
    void devicesChanged();
    void clearAllPlans();
    bool waitForEngines (int timeoutMs = 1000);

    void rebuildPlan();
    void notify();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override; // scanned list changed

    DeviceHub& devices;
    InflightLog& inflight;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    juce::ValueTree show;
    std::map<juce::String, std::shared_ptr<SlotRuntime>> slots;
    std::map<juce::String, std::shared_ptr<RackRuntime>> racks;
    std::map<juce::String, juce::String> slotErrors;
    std::map<juce::String, std::unique_ptr<PluginWindow>> windows;
    std::set<juce::String> excluded;   // slots temporarily kept out of the plan
    std::set<juce::String> unprepared; // excluded until the engines let go, then re-prepared
    std::set<juce::String> awaitingScan; // plugin not scanned on this Mac: retried after a scan
    juce::String routingError;
    std::map<juce::String, juce::String> homes;
    std::map<juce::String, std::shared_ptr<ClockBridge>> bridges;
    std::map<juce::String, std::pair<double, bool>> bridgeState;   // target, locked at last build
    std::map<juce::String, int> latencies;
    int alignedLatency = 0;
    bool alignRacks = true;
    std::atomic<bool> latencyDirty { false };
    std::map<juce::String, std::unique_ptr<SlotListener>> listeners; // after latencyDirty: they use it

    double preparedRate = 0.0;
    int preparedBlock = 0;

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace lr
