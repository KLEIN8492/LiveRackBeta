// Integration test with real installed plugins. Not part of the default run:
//   LiveRackTests RealPlugins
// Loads each plugin, runs 10 s of audio at 64 samples through the AudioEngine,
// and checks the output stays valid and how long each block takes.

#include "Core/AudioEngine.h"
#include "Core/PlanBuilder.h"
#include "Core/PluginLayout.h"
#include <juce_audio_processors/juce_audio_processors.h>

using namespace lr;

class RealPluginTests final : public juce::UnitTest
{
public:
    RealPluginTests() : juce::UnitTest ("Real plugins", "RealPlugins") {}

    void runTest() override
    {
        juce::addDefaultFormatsToManager (formats);

        runPlugin ("VST3", "/Library/Audio/Plug-Ins/VST3/Defeedback.vst3", "De-Feedback");
        runPlugin ("VST3", "/Library/Audio/Plug-Ins/VST3/WaveShell1-VST3 15.5.vst3", "L2 Mono");
        runPlugin ("VST3", "/Library/Audio/Plug-Ins/VST3/FabFilter Pro-Q 3.vst3", "Pro-Q 3");
        runPlugin ("AudioUnit", findAudioUnit ("oDin", "ValhallaDelay"), "ValhallaDelay");
        // Known: FabFilter's "aumf" (MIDI-effect) AU variants output silence in
        // JUCE hosts; their VST3s work. The browser prefers VST3 for that reason.
    }

private:
    // AU identifiers look like "AudioUnit:Effects/aufx,FQ3p,FabF", so filter by
    // manufacturer code first, then load candidates to match the name.
    juce::String findAudioUnit (const juce::String& manufacturerCode, const juce::String& name)
    {
        for (auto* f : formats.getFormats())
            if (f->getName() == "AudioUnit")
                for (auto& id : f->searchPathsForPlugins ({}, false, false))
                    if (id.endsWith (manufacturerCode))
                    {
                        juce::OwnedArray<juce::PluginDescription> found;
                        f->findAllTypesForFile (found, id);
                        for (auto* d : found)
                            if (d->name.containsIgnoreCase (name))
                                return id;
                    }
        return {};
    }

    void runPlugin (const juce::String& formatName, const juce::String& fileOrId, const juce::String& pluginName)
    {
        beginTest (pluginName + " (" + formatName + ")");

        if (fileOrId.isEmpty())
        {
            logMessage ("  not installed, skipped");
            return;
        }

        juce::AudioPluginFormat* format = nullptr;
        for (auto* f : formats.getFormats())
            if (f->getName() == formatName)
                format = f;

        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, fileOrId);

        const juce::PluginDescription* desc = nullptr;
        for (auto* d : found)
            if (d->name.containsIgnoreCase (pluginName))
                desc = d;

        if (desc == nullptr)
        {
            logMessage ("  not found in " + fileOrId + ", skipped");
            return;
        }

        constexpr double sr = 48000.0;
        constexpr int block = 64;

        juce::String error;
        auto instance = formats.createPluginInstance (*desc, sr, block, error);
        expect (instance != nullptr, error);
        if (instance == nullptr)
            return;

        const int step = negotiateLayout (*instance, 1); // same as the host, mono rack
        instance->prepareToPlay (sr, block);
        logMessage ("  layout step " + juce::String (step) + ": ins " + juce::String (instance->getTotalNumInputChannels()) + " outs "
                    + juce::String (instance->getTotalNumOutputChannels()) + " buses in "
                    + juce::String (instance->getBusCount (true)) + " out " + juce::String (instance->getBusCount (false))
                    + " main in " + instance->getChannelLayoutOfBus (true, 0).getDescription()
                    + " main out " + instance->getChannelLayoutOfBus (false, 0).getDescription());

        auto slot = std::make_shared<SlotRuntime> ("slot");
        slot->plugin = std::move (instance);

        RackSpec rack;
        rack.id = "vox";
        rack.critical = true;
        rack.inputs = { 0 };
        rack.outputs = { 0, 1 };
        rack.runtime = std::make_shared<RackRuntime> ("vox");
        rack.slots = { slot };

        PlanSpec spec;
        spec.racks = { rack };
        spec.numDeviceInputs = 2;
        spec.numDeviceOutputs = 2;
        spec.maxBlockSize = block;
        spec.sampleRate = sr;

        AudioEngine engine;
        engine.setPlan (buildPlan (spec).plan);

        std::vector<float> in (block), out0 (block), out1 (block), silence (block, 0.0f);
        const float* ins[] = { in.data(), silence.data() };
        float* outs[] = { out0.data(), out1.data() };

        juce::Random rng (42);
        double worstMs = 0.0, totalMs = 0.0;
        float outPeak = 0.0f;
        const int blocks = (int) (10.0 * sr / block);

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const double t = (double) (b * block + i) / sr;
                in[(size_t) i] = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                               + 0.02f * (rng.nextFloat() - 0.5f);
            }

            const auto t0 = juce::Time::getHighResolutionTicks();
            engine.audioDeviceIOCallbackWithContext (ins, 2, outs, 2, block, {});
            const double ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0;

            if (b > 50) // ignore warm-up
                worstMs = juce::jmax (worstMs, ms);
            totalMs += ms;

            for (float v : out0)
                outPeak = juce::jmax (outPeak, std::abs (v));
        }

        const double budgetMs = 1000.0 * block / sr;
        logMessage ("  " + desc->name + " by " + desc->manufacturerName
                    + ": avg " + juce::String (totalMs / blocks, 3) + " ms, worst " + juce::String (worstMs, 3)
                    + " ms of a " + juce::String (budgetMs, 2) + " ms block; latency "
                    + juce::String (slot->plugin->getLatencySamples()) + " samples; out peak "
                    + juce::String (juce::Decibels::gainToDecibels (outPeak), 1) + " dBFS");

        expect (! slot->silentOutput.load(), "plugin output digital silence");
        expect (slot->fault.load() == FaultReason::none, juce::String ("watchdog fault: ") + describe (slot->fault.load()));
        expect (! rack.runtime->dryFallback.load(), "rack fell back to dry");
        expect (outPeak > 0.0f, "no audio came out");
        expect (totalMs / blocks < budgetMs * 0.5, "plugin too slow on average for a 64-sample buffer");

        slot->plugin->releaseResources();
        engine.clearPlan();
    }

    juce::AudioPluginFormatManager formats;
};

static RealPluginTests realPluginTests;
