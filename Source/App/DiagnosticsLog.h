#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "Core/DeviceHub.h"

namespace lr
{
// Writes one line per second to ~/Library/Application Support/LiveRack/diagnostics.log
// (recreated each launch): device, buffer, DSP load, worst callback time and
// worst gap between callbacks against the buffer period, and dropouts. Lets a
// dropout report be traced to "engine too slow" vs "device / driver".
class DiagnosticsLog final : private juce::Timer
{
public:
    DiagnosticsLog (DeviceHub& h, const juce::File& file) : hub (h), out (file)
    {
        out.replaceWithText ("time  interface | sr buf | workers | dsp% | worstCallback/period us | worstGap us | dropouts(+new)\n");
        startTimer (1000);
    }

private:
    void timerCallback() override
    {
        const auto time = juce::Time::getCurrentTime().toString (false, true, true, true) + "  ";
        juce::String text;

        if (hub.getOpen().empty())
            text << time << "no interface open" << (hub.isBoostActive() ? "" : "") << "\n";

        for (auto& o : hub.getOpen())
        {
            auto& engine = *o->engine;
            auto* d = o->device.get();
            const auto callbackUs = engine.maxCallbackUs.exchange (0);
            const auto gapUs = engine.maxGapUs.exchange (0);
            const double sr = d->getCurrentSampleRate();
            const int bs = d->getCurrentBufferSizeSamples();
            const int periodUs = sr > 0 ? (int) (1.0e6 * bs / sr) : 0;
            const int xruns = d->getXRunCount();
            auto& last = lastXruns[o->name];

            text << time << d->getName()
                 << " | " << (int) sr << " " << bs << " (cb " << engine.lastNumSamples.load() << ")"
                 << " | w" << engine.getNumWorkers() << (hub.isBoostActive() ? " boost" : "")
                 << " | " << juce::String (engine.getCpuLoad() * 100.0, 1)
                 << " | " << (int) callbackUs << "/" << periodUs
                 << " | " << (int) gapUs
                 << " | " << xruns << " (+" << juce::jmax (0, xruns - last) << ")";

            if (gapUs > (uint32_t) periodUs * 2)  text << "  LATE-CALLBACK";
            if (callbackUs > (uint32_t) periodUs) text << "  OVERRUN";
            text << "\n";
            last = xruns;
        }

        out.appendText (text);
    }

    DeviceHub& hub;
    juce::File out;
    std::map<juce::String, int> lastXruns;
};
} // namespace lr
