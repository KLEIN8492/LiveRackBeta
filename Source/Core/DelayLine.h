#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace lr
{
// Fixed-capacity multichannel delay used for latency compensation. The write
// side is fed every block (even while the delayed signal isn't used), so
// switching to the delayed path is seamless. Real-time safe after creation.
class DelayLine
{
public:
    DelayLine (int numChannels, int minCapacity);

    int getCapacity() const noexcept     { return capacity; }
    int getNumChannels() const noexcept  { return (int) buf.size(); }

    // Appends n samples of the first numChannels channels of src.
    void push (const juce::AudioBuffer<float>& src, int numChannels, int n) noexcept;

    // The last n pushed samples, delayed by `delay` samples, into dst.
    // Requires delay + n <= capacity.
    void readLast (juce::AudioBuffer<float>& dst, int numChannels, int n, int delay) const noexcept;

    // push + readLast in place.
    void process (juce::AudioBuffer<float>& buffer, int numChannels, int n, int delay) noexcept
    {
        push (buffer, numChannels, n);
        readLast (buffer, numChannels, n, delay);
    }

private:
    int capacity;
    juce::int64 mask;
    juce::int64 writePos = 0;
    std::vector<std::vector<float>> buf;
};

} // namespace lr
