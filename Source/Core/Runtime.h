#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Watchdog.h"
#include "DelayLine.h"

namespace lr
{
// Live state of one plugin slot. Shared between the message thread and every
// RenderPlan that references the slot, so it survives plan rebuilds.
struct SlotRuntime
{
    explicit SlotRuntime (juce::String slotId) : id (std::move (slotId)) {}

    const juce::String id;

    // Assigned once on the message thread, before any plan containing this slot
    // is published. Never replaced afterwards; destroyed with the last plan.
    std::unique_ptr<juce::AudioPluginInstance> plugin;

    std::atomic<bool>        userBypassed   { false };
    std::atomic<FaultReason> fault          { FaultReason::none };
    std::atomic<bool>        resetRequested { false }; // set by "re-arm"; audio thread calls reset()
    std::atomic<float>       processMs      { 0.0f };
    std::atomic<bool>        inProcess      { false };
    std::atomic<bool>        silentOutput   { false }; // signal in, digital zero out for a while
    std::atomic<bool>        stateChanged   { false }; // edited since its state was last saved

    int silentSamples = 0;    // audio thread only

    int inflightRecord = -1;  // index in the InflightLog, -1 if none

    // Delays the slot's input by the plugin's latency while it's bypassed or
    // faulted, so bypassing never shifts the rack's timing. Message thread
    // replaces it (plans keep their own reference); audio uses the plan's.
    std::shared_ptr<DelayLine> bypassDelay;
    SlotHealth health;        // audio thread only

    bool isActive() const noexcept
    {
        return ! userBypassed.load (std::memory_order_relaxed)
            && fault.load (std::memory_order_relaxed) == FaultReason::none;
    }
};

// Live state of one rack, shared across plans.
struct RackRuntime
{
    explicit RackRuntime (juce::String rackId) : id (std::move (rackId)) {}

    const juce::String id;

    std::atomic<bool>  dryFallback { false };
    std::atomic<float> inPeak      { 0.0f };  // max since the UI last read it
    std::atomic<float> outPeak     { 0.0f };
    std::atomic<float> processMs   { 0.0f };

    RackHealth health;        // audio thread only

    // Latency compensation: dry path (fallback / bypass) and output alignment.
    std::shared_ptr<DelayLine> dryDelay, outputDelay;

    static void raisePeak (std::atomic<float>& meter, float value) noexcept
    {
        if (value > meter.load (std::memory_order_relaxed))
            meter.store (value, std::memory_order_relaxed);
    }
};

} // namespace lr
