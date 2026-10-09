// Runs the engine with De-Feedback on a real Core Audio device thread
// (BlackHole, output only, so no microphone permission is needed) at the
// buffer sizes the user's 1-3 ms target requires. Not in the default run:
//   LiveRackTests DeviceBench

#include "Core/AudioEngine.h"
#include "Core/PlanBuilder.h"
#include "TestHelpers.h"
#include "Core/PluginLayout.h"
#include <juce_audio_processors/juce_audio_processors.h>

using namespace lr;

namespace
{
// Feeds a synthetic vocal-ish signal into the engine as if it came from inputs.
class SyntheticInput final : public juce::AudioIODeviceCallback
{
public:
    explicit SyntheticInput (AudioEngine& e) : engine (e) {}

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        engine.audioDeviceAboutToStart (d);
        period = d->getCurrentBufferSizeSamples() / d->getCurrentSampleRate();
        in.assign (2, std::vector<float> ((size_t) d->getCurrentBufferSizeSamples() * 4));
    }

    void audioDeviceStopped() override { engine.audioDeviceStopped(); }

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& ctx) override
    {
        for (int i = 0; i < numSamples; ++i, ++phase)
            in[0][(size_t) i] = 0.25f * (float) std::sin (0.0288 * (double) phase) + 0.01f * (rng.nextFloat() - 0.5f);
        std::fill (in[1].begin(), in[1].begin() + numSamples, 0.0f);

        const float* ins[] = { in[0].data(), in[1].data() };
        const auto t0 = juce::Time::getHighResolutionTicks();
        engine.audioDeviceIOCallbackWithContext (ins, 2, outputs, numOutputs, numSamples, ctx);
        const double s = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);

        ++callbacks;
        total += s;
        worst = juce::jmax (worst, s);
        if (s > period) ++overruns;
    }

    AudioEngine& engine;
    std::vector<std::vector<float>> in;
    juce::Random rng;
    juce::int64 phase = 0;
    double period = 0, total = 0, worst = 0;
    std::atomic<int> callbacks { 0 }, overruns { 0 };
};
} // namespace

class DeviceBenchTests final : public juce::UnitTest
{
public:
    DeviceBenchTests() : juce::UnitTest ("Device bench", "DeviceBench") {}

    void runTest() override
    {
        juce::AudioPluginFormatManager formats;
        juce::addDefaultFormatsToManager (formats);

        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* f : formats.getFormats())
            if (f->getName() == "VST3")
                f->findAllTypesForFile (found, "/Library/Audio/Plug-Ins/VST3/Defeedback.vst3");

        if (found.isEmpty())
            return;

        for (int channels : { 1, 2 })
            for (int block : { 32, 64 })
                run (formats, *found[0], channels, block);
    }

private:
    void run (juce::AudioPluginFormatManager& formats, const juce::PluginDescription& desc, int channels, int block)
    {
        beginTest ("De-Feedback " + juce::String (channels == 1 ? "mono" : "stereo") + " on BlackHole @ "
                   + juce::String (block) + " smp");

        juce::AudioDeviceManager dm;
        dm.initialise (0, 2, nullptr, false);
        auto setup = dm.getAudioDeviceSetup();
        setup.inputDeviceName = {};
        setup.outputDeviceName = "BlackHole 16ch";
        setup.sampleRate = 48000.0;
        setup.bufferSize = block;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange (0, 2, true);
        const auto err = dm.setAudioDeviceSetup (setup, true);
        auto* device = dm.getCurrentAudioDevice();
        expect (err.isEmpty() && device != nullptr, err);
        if (device == nullptr)
            return;

        const double sr = device->getCurrentSampleRate();
        const int bs = device->getCurrentBufferSizeSamples();

        juce::String error;
        auto instance = formats.createPluginInstance (desc, sr, bs, error);
        negotiateLayout (*instance, channels);
        instance->prepareToPlay (sr, bs);
        instance->audioWorkgroupContextChanged (device->getWorkgroup());

        auto slot = std::make_shared<SlotRuntime> ("s");
        slot->plugin = std::move (instance);

        RackSpec rack;
        rack.id = "vox";
        rack.numChannels = channels;
        rack.inputs = test::ports ({ 0 });
        rack.outputs = test::ports ({ 0, 1 });
        rack.runtime = std::make_shared<RackRuntime> ("vox");
        rack.slots = { slot };

        auto spec = test::singleDevice ({ rack }, 2, 2, bs, sr);

        AudioEngine engine;
        auto built = buildPlan (spec);
        engine.setPlan (test::takePlan (built));
        SyntheticInput input (engine);

        dm.addAudioCallback (&input);
        juce::Thread::sleep (10000);
        dm.removeAudioCallback (&input);

        const int n = juce::jmax (1, input.callbacks.load());
        logMessage ("  " + juce::String (sr, 0) + " Hz, " + juce::String (bs) + " smp: avg "
                    + juce::String (1000.0 * input.total / n, 3) + " ms, worst " + juce::String (1000.0 * input.worst, 3)
                    + " ms of " + juce::String (1000.0 * input.period, 3) + " ms; over budget "
                    + juce::String (input.overruns.load()) + "/" + juce::String (n)
                    + "; device xruns " + juce::String (device->getXRunCount()));

        engine.clearPlan();
        slot->plugin->releaseResources();
    }
};

static DeviceBenchTests deviceBenchTests;
