#include "DelayLine.h"

namespace lr
{
DelayLine::DelayLine (int numChannels, int minCapacity)
{
    capacity = 64;
    while (capacity < minCapacity)
        capacity <<= 1;
    mask = capacity - 1;
    buf.assign ((size_t) juce::jmax (1, numChannels), std::vector<float> ((size_t) capacity, 0.0f));
}

void DelayLine::push (const juce::AudioBuffer<float>& src, int numChannels, int n) noexcept
{
    const int chans = juce::jmin (numChannels, (int) buf.size(), src.getNumChannels());
    for (int c = 0; c < chans; ++c)
    {
        const float* s = src.getReadPointer (c);
        auto& b = buf[(size_t) c];
        for (int i = 0; i < n; ++i)
            b[(size_t) ((writePos + i) & mask)] = s[i];
    }
    writePos += n;
}

void DelayLine::readLast (juce::AudioBuffer<float>& dst, int numChannels, int n, int delay) const noexcept
{
    const int chans = juce::jmin (numChannels, (int) buf.size(), dst.getNumChannels());
    const auto start = writePos - n - delay;
    for (int c = 0; c < chans; ++c)
    {
        float* d = dst.getWritePointer (c);
        const auto& b = buf[(size_t) c];
        for (int i = 0; i < n; ++i)
        {
            const auto idx = start + i;
            d[i] = idx < 0 ? 0.0f : b[(size_t) (idx & mask)];
        }
    }
}

} // namespace lr
