#include "ClockBridge.h"
#include <cmath>

namespace lr
{
namespace
{
constexpr double maxDeviation = 0.002;   // +-2000 ppm: far beyond real clock drift
constexpr double kp = 4.0e-5;            // ratio change per sample of fill error
constexpr double errorSmoothing = 0.02;  // EMA over callbacks (fill is a sawtooth)

int nextPowerOfTwo (int v)
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

// 4-point Catmull-Rom interpolation between x0 and x1.
inline float cubic (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float a = -0.5f * xm1 + 1.5f * x0 - 1.5f * x1 + 0.5f * x2;
    const float b = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c = -0.5f * xm1 + 0.5f * x1;
    return ((a * t + b) * t + c) * t + x0;
}
} // namespace

ClockBridge::ClockBridge (int channels, int cap)
    : numChannels (juce::jmax (1, channels)),
      capacity (nextPowerOfTwo (juce::jmax (1024, cap))),
      mask ((juce::int64) capacity - 1),
      ring ((size_t) numChannels, std::vector<float> ((size_t) capacity, 0.0f))
{
    configure (64, 64);
}

void ClockBridge::configure (int producerBlock, int consumerBlock)
{
    producerBlock = juce::jmax (1, producerBlock);
    consumerBlock = juce::jmax (1, consumerBlock);

    // Fill seen by the consumer swings by about one producer block (sawtooth)
    // plus callback jitter; start just above that and adapt on underrun.
    target   = consumerBlock + 0.75 * producerBlock + 2.0;
    step     = 0.5 * juce::jmax (producerBlock, consumerBlock);
    maxTarget = consumerBlock + 6.0 * juce::jmax (producerBlock, consumerBlock);

    primed = false;
    consumed = 0;
    smoothedError = integral = 0.0;
    ratio = 1.0;
    statTarget = target;
    statLocked = false;
}

void ClockBridge::write (const float* const* channels, int numSamples) noexcept
{
    const auto w = writePos.load (std::memory_order_relaxed);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& buf = ring[(size_t) ch];
        const float* src = channels[ch];
        for (int i = 0; i < numSamples; ++i)
            buf[(size_t) ((w + i) & mask)] = src != nullptr ? src[i] : 0.0f;
    }

    writePos.store (w + numSamples, std::memory_order_release);
}

void ClockBridge::read (float* const* dest, int numSamples, bool add) noexcept
{
    auto silence = [&]
    {
        if (! add)
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::clear (dest[ch], numSamples);
    };

    const auto w = writePos.load (std::memory_order_acquire);
    double avail = (double) w - readPos;

    // Producer lapped us (consumer stalled) or positions are nonsense: resync.
    if (primed && (avail > capacity - 4.0 * numSamples - 8.0 || avail < 0.0))
    {
        readPos = (double) w - target;
        avail = target;
        smoothedError = 0.0;
        statOverruns.fetch_add (1, std::memory_order_relaxed);
    }

    if (! primed)
    {
        if (avail < target + 2.0)
        {
            silence();
            statFill.store (avail, std::memory_order_relaxed);
            statPrimed.store (false, std::memory_order_relaxed);
            return;
        }

        primed = true;
        readPos = (double) w - target;
        avail = target;
        smoothedError = 0.0; // keep `integral`: it is the learned drift
    }

    // PI control of the resampling ratio on the (smoothed) fill error.
    const double error = avail - target;
    smoothedError += errorSmoothing * (error - smoothedError);

    const double a = kp * numSamples;                 // per-callback proportional gain
    const double ki = a * a / (4.0 * numSamples);     // critical damping
    integral = juce::jlimit (-maxDeviation, maxDeviation, integral + ki * smoothedError);
    ratio = juce::jlimit (1.0 - maxDeviation, 1.0 + maxDeviation, 1.0 + kp * smoothedError + integral);

    // Need samples up to floor(last read position) + 2 for the interpolator.
    const double lastPos = readPos + (numSamples - 1) * ratio;
    if (std::floor (lastPos) + 2.0 >= (double) w)
    {
        // Underrun: a callback came late. One glitch; during warm-up also a
        // safer target, after it the latency stays exactly where it was.
        silence();
        primed = false;
        if (consumed < warmupSamples)
            target = juce::jmin (maxTarget, target + step);
        statUnderruns.fetch_add (1, std::memory_order_relaxed);
        statTarget.store (target, std::memory_order_relaxed);
        statPrimed.store (false, std::memory_order_relaxed);
        return;
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* out = dest[ch];
        double pos = readPos;
        for (int i = 0; i < numSamples; ++i, pos += ratio)
        {
            const auto i0 = (juce::int64) std::floor (pos);
            const auto t = (float) (pos - (double) i0);
            const float y = cubic (sampleAt (ch, i0 - 1), sampleAt (ch, i0), sampleAt (ch, i0 + 1), sampleAt (ch, i0 + 2), t);
            out[i] = add ? out[i] + y : y;
        }
    }

    readPos += numSamples * ratio;
    consumed += numSamples;

    statFill.store (avail, std::memory_order_relaxed);
    statRatio.store (ratio, std::memory_order_relaxed);
    statTarget.store (target, std::memory_order_relaxed);
    statPrimed.store (true, std::memory_order_relaxed);
    statLocked.store (consumed >= warmupSamples, std::memory_order_relaxed);
}

ClockBridge::Stats ClockBridge::getStats() const noexcept
{
    Stats s;
    s.fill = statFill.load();
    s.target = statTarget.load();
    s.ratioPpm = (statRatio.load() - 1.0) * 1.0e6;
    s.underruns = statUnderruns.load();
    s.overruns = statOverruns.load();
    s.primed = statPrimed.load();
    s.locked = statLocked.load();
    return s;
}

} // namespace lr
