#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Core/AudioEngine.h"
#include "Host/PluginHost.h"

namespace lr
{
// Everything that keeps a show alive across crashes:
//  - a "running" marker (exists while the app runs; missing after a clean quit)
//  - the crash-persistent InflightLog, read for suspects on the next launch
//  - debounced + periodic autosave of the show (with plugin states)
//  - a stall detector that aborts if a plugin hangs the audio thread
//  - the LiveRackGuard helper that relaunches the app after a crash
class SessionGuard final : private juce::Timer, private juce::ValueTree::Listener
{
public:
    SessionGuard();
    ~SessionGuard() override;

    static juce::File getSupportDirectory();
    juce::File getAutosaveFile() const     { return dir.getChildFile ("autosave.liverack"); }
    juce::File getBackupFile() const       { return dir.getChildFile ("autosave-backup.liverack"); }
    juce::File getDeviceFile() const       { return dir.getChildFile ("device.xml"); }
    juce::File getPluginListFile() const   { return dir.getChildFile ("plugins.xml"); }
    juce::File getScanPedalFile() const    { return dir.getChildFile ("scan-in-progress.txt"); }

    // Call first thing at startup: detects an unclean previous exit and opens the log.
    void begin();
    bool previousRunCrashed() const        { return crashed; }
    const std::vector<juce::String>& getSuspectSlots() const { return suspects; }
    InflightLog& getInflightLog()          { return inflight; }

    // Starts autosave and the stall detector for this host/engine.
    void watch (PluginHost&, DeviceHub&);
    void saveNow (bool allPluginStates = false);   // false: only plugins with edits

    // Call from a clean shutdown, after the final save.
    void endCleanly();

private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { markDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override         { markDirty(); }
    void markDirty();
    void spawnGuard();
    void checkForStall();

    juce::File dir = getSupportDirectory();
    juce::File runningMarker = dir.getChildFile ("running");
    juce::File inflightFile = dir.getChildFile ("inflight.bin");

    InflightLog inflight;
    bool crashed = false;
    std::vector<juce::String> suspects;

    PluginHost* host = nullptr;
    DeviceHub* hub = nullptr;
    juce::ValueTree watchedShow;

    bool dirty = false;
    juce::uint32 lastChange = 0, lastFullSave = 0;
    std::map<juce::String, uint64_t> lastHeartbeat;
    int stalledTicks = 0;
};

} // namespace lr
