#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>

namespace lr
{
enum class FaultReason : uint8_t
{
    none,
    nonFinite,    // NaN or Inf in the plugin's output
    overload,     // sustained output far above full scale (feedback / runaway)
    overtime,     // rack took too much of the audio deadline for several blocks
    stall,        // plugin hung inside processBlock (detected off the audio thread)
    crashSuspect  // plugin was inside processBlock when the app last crashed
};

const char* describe (FaultReason);

struct WatchdogConfig
{
    float  hotGain        = 15.85f; // +24 dBFS
    double hotSeconds     = 0.05;
    double overtimeRatio  = 0.9;    // fraction of the block period
    int    overtimeBlocks = 8;
};

// Audio-thread-only counters.
struct SlotHealth { int hotSamples = 0; };
struct RackHealth { int overtimeBlocks = 0; };

// Checks a plugin's output after processBlock. Real-time safe.
FaultReason checkSlotOutput (SlotHealth&, const juce::AudioBuffer<float>&, int numChannels,
                             int numSamples, double sampleRate, const WatchdogConfig&);

// Checks how long a whole rack took against the block period. Real-time safe.
FaultReason checkRackTiming (RackHealth&, double elapsedSeconds, double blockSeconds, const WatchdogConfig&);

struct FaultAction
{
    bool bypassSlot = false; // stop processing the offending plugin
    bool rackToDry  = false; // the whole rack outputs its unprocessed input
};

// The policy: what a fault does to a rack, depending on whether the rack is critical.
FaultAction chooseFaultAction (bool rackIsCritical, FaultReason);

} // namespace lr
