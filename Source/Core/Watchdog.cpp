#include "Watchdog.h"
#include <bit>

namespace lr
{
const char* describe (FaultReason r)
{
    switch (r)
    {
        case FaultReason::none:         return "OK";
        case FaultReason::nonFinite:    return "Invalid audio (NaN/Inf)";
        case FaultReason::overload:     return "Runaway level";
        case FaultReason::overtime:     return "Too slow for buffer";
        case FaultReason::stall:        return "Plugin froze";
        case FaultReason::crashSuspect: return "Crashed last session";
    }
    return "?";
}

FaultReason checkSlotOutput (SlotHealth& health, const juce::AudioBuffer<float>& buffer, int numChannels,
                             int numSamples, double sampleRate, const WatchdogConfig& cfg)
{
    // Bit test instead of std::isfinite so it survives -ffast-math.
    constexpr uint32_t expMask = 0x7f800000u;
    float peak = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer (ch);
        uint32_t nonFinite = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto bits = std::bit_cast<uint32_t> (data[i]);
            nonFinite |= ((bits & expMask) == expMask) ? 1u : 0u;
            peak = juce::jmax (peak, std::abs (data[i]));
        }

        if (nonFinite != 0)
            return FaultReason::nonFinite;
    }

    if (peak > cfg.hotGain)
    {
        health.hotSamples += numSamples;
        if (health.hotSamples >= (int) (cfg.hotSeconds * sampleRate))
        {
            health.hotSamples = 0;
            return FaultReason::overload;
        }
    }
    else
    {
        health.hotSamples = 0;
    }

    return FaultReason::none;
}

FaultReason checkRackTiming (RackHealth& health, double elapsedSeconds, double blockSeconds, const WatchdogConfig& cfg)
{
    if (elapsedSeconds > cfg.overtimeRatio * blockSeconds)
    {
        if (++health.overtimeBlocks >= cfg.overtimeBlocks)
        {
            health.overtimeBlocks = 0;
            return FaultReason::overtime;
        }
    }
    else
    {
        health.overtimeBlocks = 0;
    }

    return FaultReason::none;
}

FaultAction chooseFaultAction (bool rackIsCritical, FaultReason reason)
{
    if (reason == FaultReason::none)
        return {};

    // Critical racks (lead vocal…) must never go silent or sound broken:
    // fall back to the clean input. Others just lose the offending plugin.
    if (rackIsCritical)
        return { true, true };

    return { true, false };
}

} // namespace lr
