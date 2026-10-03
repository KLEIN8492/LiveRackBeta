#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "Core/AudioEngine.h"

namespace lr
{
// Writes one line per second to ~/Library/Application Support/LiveRack/diagnostics.log
// (recreated each launch): device, buffer, DSP load, worst callback time and
// worst gap between callbacks against the buffer period, and dropouts. Lets a
// dropout report be traced to "engine too slow" vs "device / driver".
class DiagnosticsLog final : private juce::Timer
{
public:
    DiagnosticsLog (juce::AudioDeviceManager& dm, AudioEngine& e, const juce::File& file)
        : devices (dm), engine (e), out (file)
    {
        out.replaceWithText ("time  device | sr buf | workers | dsp% | worstCallback/period us | worstGap us | dropouts(+new)\n");
        startTimer (1000);
    }

private:
    void timerCallback() override
    {
        juce::String line = juce::Time::getCurrentTime().toString (false, true, true, true) + "  ";
        const auto callbackUs = engine.maxCallbackUs.exchange (0);
        const auto gapUs = engine.maxGapUs.exchange (0);

        if (auto* d = devices.getCurrentAudioDevice())
        {
            const double sr = d->getCurrentSampleRate();
            const int bs = d->getCurrentBufferSizeSamples();
            const int periodUs = sr > 0 ? (int) (1.0e6 * bs / sr) : 0;
            const int xruns = d->getXRunCount();

            line << d->getTypeName() << ":" << d->getName()
                 << " | " << (int) sr << " " << bs << " (cb " << engine.lastNumSamples.load() << ")"
                 << " | w" << engine.getNumWorkers()
                 << " | " << juce::String (engine.getCpuLoad() * 100.0, 1)
                 << " | " << (int) callbackUs << "/" << periodUs
                 << " | " << (int) gapUs
                 << " | " << xruns << " (+" << juce::jmax (0, xruns - lastXruns) << ")";

            if (gapUs > (uint32_t) periodUs * 2)  line << "  LATE-CALLBACK";
            if (callbackUs > (uint32_t) periodUs) line << "  OVERRUN";
            lastXruns = xruns;
        }
        else
        {
            line << "no device";
        }

        out.appendText (line + "\n");
    }

    juce::AudioDeviceManager& devices;
    AudioEngine& engine;
    juce::File out;
    int lastXruns = 0;
};
} // namespace lr
