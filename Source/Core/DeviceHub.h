#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "AudioEngine.h"
#include "CpuBoost.h"
#include <set>

namespace lr
{
// Owns every audio interface LiveRack uses. Each interface is opened on its
// own (one AudioIODevice per interface, never JUCE's in/out combiner) with all
// channels active at the shared sample rate and buffer size, and runs its own
// AudioEngine in its own callback thread. Interfaces are opened when wanted
// (referenced by a rack) and reopened when they come back after unplugging.
class DeviceHub final : private juce::AudioIODeviceType::Listener
{
public:
    DeviceHub();
    ~DeviceHub() override;

    // ---- catalogue -------------------------------------------------------------
    struct Info
    {
        juce::String name;
        juce::StringArray inputs, outputs; // channel names
    };
    const std::vector<Info>& getAvailable();               // every interface on the Mac
    const Info* findAvailable (const juce::String& name);

    // ---- format ----------------------------------------------------------------
    void setFormat (double sampleRate, int blockSize);     // reopens what's open
    double getSampleRate() const noexcept   { return sampleRate; }
    int getBlockSize() const noexcept       { return blockSize; }
    int getMaxOpenBlockSize() const;                       // actual, across open interfaces

    // ---- open set ----------------------------------------------------------------
    void setWanted (const std::set<juce::String>& names);
    void setOpeningAllowed (bool allowed)   { openingAllowed = allowed; } // false: never touch audio (snapshots)
    std::set<juce::String> getWanted() const { return wanted; }

    struct Open
    {
        juce::String name;
        std::unique_ptr<AudioEngine> engine;               // destroyed after the device
        std::unique_ptr<juce::AudioIODevice> device;
    };
    const std::vector<std::unique_ptr<Open>>& getOpen() const noexcept { return open; }
    AudioEngine* engineFor (const juce::String& name) const;
    juce::AudioIODevice* deviceFor (const juce::String& name) const;
    juce::String getError (const juce::String& name) const; // why a wanted interface isn't open

    // ---- engine-wide controls ---------------------------------------------------
    void setInflightLog (InflightLog* log);
    void setGlobalBypass (bool);
    bool isGlobalBypassed() const noexcept  { return globalBypass; }
    void setBoostEnabled (bool);
    bool isBoostEnabled() const noexcept    { return boostEnabled; }
    bool isBoostActive() const noexcept     { return boost.isRunning(); }
    static constexpr int boostMaxBlock = 128;

    // Message thread: interfaces appeared/disappeared, or the open set changed.
    std::function<void()> onChanged;

private:
    void audioDeviceListChanged() override;
    void rescan();
    bool openOne (const juce::String& name);
    void closeOne (const juce::String& name);
    void reconcile();
    void updateBoost();

    std::unique_ptr<juce::AudioIODeviceType> type;
    std::vector<Info> available;
    bool scanned = false;

    std::vector<std::unique_ptr<Open>> open;
    std::set<juce::String> wanted;
    std::map<juce::String, juce::String> errors;

    double sampleRate = 48000.0;
    int blockSize = 64;
    InflightLog* inflight = nullptr;
    bool globalBypass = false;
    bool boostEnabled = true;
    bool openingAllowed = true;
    CpuBoost boost;
};

} // namespace lr
