#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace lr
{
// Carries audio from one interface's callback (producer) to another's
// (consumer) when their clocks are independent. Lock-free SPSC ring; the
// consumer resamples with a ratio steered by a PI controller so the fill level
// holds at a target that is as small as the callbacks' timing allows.
//
// Starts at the minimum target; during warm-up an underrun (a late callback)
// outputs silence, re-primes and raises the target by half a producer block.
// After warm-up the target — i.e. the added latency — is locked: a late
// callback costs one glitch but never shifts the timing of the signal, which
// matters for inserts summed on a console. Added latency ~= target samples.
class ClockBridge
{
public:
    explicit ClockBridge (int numChannels, int capacity = 16384);

    // Message thread, before use (or with both devices stopped).
    void configure (int producerBlock, int consumerBlock);
    void setWarmupSamples (juce::int64 samples) noexcept { warmupSamples = samples; } // default 10 s @ 48k

    int getNumChannels() const noexcept { return numChannels; }

    // Producer callback. `channels` has getNumChannels() entries (nullptr = silence).
    void write (const float* const* channels, int numSamples) noexcept;

    // Consumer callback. Writes (or adds) numSamples into each dest channel.
    void read (float* const* dest, int numSamples, bool add = false) noexcept;

    struct Stats
    {
        double fill = 0, target = 0, ratioPpm = 0;
        int underruns = 0, overruns = 0;
        bool primed = false;
        bool locked = false;   // warm-up over: latency fixed
    };
    Stats getStats() const noexcept;

private:
    float sampleAt (int ch, juce::int64 index) const noexcept
    {
        return ring[(size_t) ch][(size_t) (index & mask)];
    }

    const int numChannels;
    const int capacity;
    const juce::int64 mask;
    std::vector<std::vector<float>> ring;

    std::atomic<juce::int64> writePos { 0 };

    // Consumer-only state.
    double readPos = 0.0;
    double smoothedError = 0.0, integral = 0.0, ratio = 1.0;
    double target = 64.0, maxTarget = 512.0, step = 16.0;
    bool primed = false;
    juce::int64 consumed = 0, warmupSamples = 480000;

    // Published for the UI.
    std::atomic<double> statFill { 0 }, statTarget { 0 }, statRatio { 1.0 };
    std::atomic<int> statUnderruns { 0 }, statOverruns { 0 };
    std::atomic<bool> statPrimed { false }, statLocked { false };
};

} // namespace lr
