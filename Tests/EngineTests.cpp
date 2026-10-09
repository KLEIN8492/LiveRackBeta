#include "Core/AudioEngine.h"
#include "Core/PlanBuilder.h"
#include "TestHelpers.h"
#include <limits>

using namespace lr;

namespace
{
// Minimal in-memory "plugin" whose processing is a lambda.
class FakePlugin final : public juce::AudioPluginInstance
{
public:
    using Fn = std::function<void (juce::AudioBuffer<float>&)>;

    FakePlugin (int channels, Fn f, int latency = 0)
        : juce::AudioPluginInstance (BusesProperties()
              .withInput ("In", channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo())
              .withOutput ("Out", channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo())),
          fn (std::move (f))
    {
        setLatencySamples (latency);
    }

    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "Fake"; }
    const juce::String getName() const override { return "Fake"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { fn (b); }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    Fn fn;
};

std::shared_ptr<SlotRuntime> slotWith (int channels, FakePlugin::Fn fn, int latency = 0)
{
    auto s = std::make_shared<SlotRuntime> (juce::Uuid().toString());
    s->plugin = std::make_unique<FakePlugin> (channels, std::move (fn), latency);
    return s;
}

// A plugin that really delays its input by `latency` samples (and reports it).
std::shared_ptr<SlotRuntime> latentSlot (int latency)
{
    auto ring = std::make_shared<std::vector<float>> ((size_t) latency + 1, 0.0f);
    auto pos = std::make_shared<size_t> (0);
    return slotWith (1, [ring, pos, latency] (juce::AudioBuffer<float>& b)
    {
        float* d = b.getWritePointer (0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float in = d[i];
            d[i] = (*ring)[(*pos + 1) % ring->size()];
            (*ring)[*pos] = in;
            *pos = (*pos + 1) % ring->size();
        }
        juce::ignoreUnused (latency);
    }, latency);
}

// Sends one impulse into input 0 and returns the sample index where each
// output channel peaks.
std::vector<int> impulseArrival (AudioEngine& engine, int numOutputs, int blocks = 8, int block = 64,
                                 std::function<void (int)> beforeBlock = {})
{
    std::vector<float> in ((size_t) block), silence ((size_t) block, 0.0f);
    std::vector<std::vector<float>> out ((size_t) numOutputs, std::vector<float> ((size_t) block));
    std::vector<int> arrival ((size_t) numOutputs, -1);
    std::vector<float> peak ((size_t) numOutputs, 0.0f);

    for (int b = 0; b < blocks; ++b)
    {
        if (beforeBlock)
            beforeBlock (b);
        std::fill (in.begin(), in.end(), 0.0f);
        if (b == 0)
            in[5] = 1.0f;

        std::vector<const float*> ins { in.data(), silence.data(), silence.data(), silence.data() };
        std::vector<float*> outs;
        for (auto& o : out)
            outs.push_back (o.data());
        engine.audioDeviceIOCallbackWithContext (ins.data(), 4, outs.data(), numOutputs, block, {});

        for (int c = 0; c < numOutputs; ++c)
            for (int i = 0; i < block; ++i)
                if (std::abs (out[(size_t) c][(size_t) i]) > peak[(size_t) c] + 1e-6f)
                {
                    peak[(size_t) c] = std::abs (out[(size_t) c][(size_t) i]);
                    arrival[(size_t) c] = b * block + i;
                }
    }
    return arrival;
}

FakePlugin::Fn gain (float g)
{
    return [g] (juce::AudioBuffer<float>& b) { b.applyGain (g); };
}

FakePlugin::Fn nanMaker()
{
    return [] (juce::AudioBuffer<float>& b) { b.setSample (0, 3, std::numeric_limits<float>::quiet_NaN()); };
}

RackSpec rack (juce::String id, std::vector<int> ins, std::vector<int> outs, std::vector<juce::String> sources = {})
{
    RackSpec r;
    r.id = id;
    for (int c : ins)  r.inputs.push_back ({ "dev", c });
    for (int c : outs) r.outputs.push_back ({ "dev", c });
    r.sources = std::move (sources);
    r.runtime = std::make_shared<RackRuntime> (id);
    return r;
}

// Drives the engine like a device would: 4 in, 4 out, constant input values.
struct Harness
{
    static constexpr int numChannels = 4, block = 64;
    std::vector<std::vector<float>> in, out;
    std::vector<const float*> inPtrs;
    std::vector<float*> outPtrs;

    Harness()
        : in (numChannels, std::vector<float> (block)), out (numChannels, std::vector<float> (block))
    {
        for (int c = 0; c < numChannels; ++c)
        {
            std::fill (in[(size_t) c].begin(), in[(size_t) c].end(), 0.1f * (float) (c + 1));
            inPtrs.push_back (in[(size_t) c].data());
            outPtrs.push_back (out[(size_t) c].data());
        }
    }

    void run (AudioEngine& engine)
    {
        engine.audioDeviceIOCallbackWithContext (inPtrs.data(), numChannels, outPtrs.data(), numChannels, block, {});
    }

    float outAt (int ch, int i = 10) const { return out[(size_t) ch][(size_t) i]; }
};

std::unique_ptr<RenderPlan> build (std::vector<RackSpec> racks)
{
    auto r = buildPlan (test::singleDevice (std::move (racks), Harness::numChannels, Harness::numChannels, Harness::block));
    return test::takePlan (r);
}
} // namespace

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("AudioEngine", "Core") {}

    void runTest() override
    {
        beginTest ("a gain plugin processes input 0 into output 0");
        {
            AudioEngine engine;
            Harness h;
            auto r = rack ("a", { 0 }, { 0 });
            r.slots.push_back (slotWith (1, gain (2.0f)));
            engine.setPlan (build ({ r }));
            h.run (engine);
            expectWithinAbsoluteError (h.outAt (0), 0.2f, 1e-6f);
            expectWithinAbsoluteError (h.outAt (1), 0.0f, 1e-6f);
        }

        beginTest ("racks sharing an output are summed");
        {
            AudioEngine engine;
            Harness h;
            engine.setPlan (build ({ rack ("a", { 0 }, { 2 }), rack ("b", { 1 }, { 2 }) }));
            h.run (engine);
            expectWithinAbsoluteError (h.outAt (2), 0.1f + 0.2f, 1e-6f);
        }

        beginTest ("bus sums its source racks before processing");
        {
            AudioEngine engine;
            Harness h;
            auto bus = rack ("bus", {}, { 3 }, { "a", "b" });
            bus.slots.push_back (slotWith (1, gain (0.5f)));
            engine.setPlan (build ({ rack ("a", { 0 }, {}), rack ("b", { 1 }, {}), bus }));
            h.run (engine);
            expectWithinAbsoluteError (h.outAt (3), (0.1f + 0.2f) * 0.5f, 1e-6f);
        }

        beginTest ("NaN on a normal rack bypasses just that plugin");
        {
            AudioEngine engine;
            Harness h;
            auto r = rack ("a", { 0 }, { 0 });
            auto g = slotWith (1, gain (2.0f));
            auto bad = slotWith (1, nanMaker());
            r.slots = { g, bad };
            engine.setPlan (build ({ r }));
            h.run (engine);

            expect (bad->fault.load() == FaultReason::nonFinite);
            expect (g->fault.load() == FaultReason::none);
            expect (! r.runtime->dryFallback.load());
            expectWithinAbsoluteError (h.outAt (0, 3), 0.2f, 1e-6f); // gain kept, NaN undone

            h.run (engine); // next block: bad slot is skipped
            expectWithinAbsoluteError (h.outAt (0, 3), 0.2f, 1e-6f);
        }

        beginTest ("NaN on a critical rack falls back to dry input");
        {
            AudioEngine engine;
            Harness h;
            auto r = rack ("vox", { 0 }, { 0 });
            r.critical = true;
            r.slots = { slotWith (1, gain (2.0f)), slotWith (1, nanMaker()) };
            engine.setPlan (build ({ r }));
            h.run (engine);

            expect (r.runtime->dryFallback.load());
            expectWithinAbsoluteError (h.outAt (0, 3), 0.1f, 1e-6f);
        }

        beginTest ("user-bypassed slot is skipped");
        {
            AudioEngine engine;
            Harness h;
            auto r = rack ("a", { 0 }, { 0 });
            auto s = slotWith (1, gain (4.0f));
            s->userBypassed = true;
            r.slots = { s };
            engine.setPlan (build ({ r }));
            h.run (engine);
            expectWithinAbsoluteError (h.outAt (0), 0.1f, 1e-6f);
        }

        beginTest ("mono rack with a stereo plugin stays mono and correct");
        {
            AudioEngine engine;
            Harness h;
            auto r = rack ("a", { 0 }, { 0, 1 });
            r.slots = { slotWith (2, gain (2.0f)) };
            engine.setPlan (build ({ r }));
            h.run (engine);
            expectWithinAbsoluteError (h.outAt (0), 0.2f, 1e-6f);
            expectWithinAbsoluteError (h.outAt (1), 0.2f, 1e-6f);
        }

        beginTest ("parallel workers produce the same output as one thread");
        {
            auto makeRacks = []
            {
                std::vector<RackSpec> racks;
                for (int i = 0; i < 12; ++i)
                {
                    auto r = rack ("r" + juce::String (i), { i % 4 }, { i % 4 });
                    r.slots = { slotWith (1, gain (1.0f + 0.1f * (float) i)) };
                    racks.push_back (r);
                }
                racks.push_back (rack ("bus", {}, { 0 }, { "r0", "r5", "r11" }));
                return racks;
            };

            AudioEngine single;
            Harness h1;
            single.setPlan (build (makeRacks()));
            h1.run (single);

            AudioEngine parallel;
            parallel.startWorkers (3, {}, Harness::block, 48000.0);
            Harness h2;
            parallel.setPlan (build (makeRacks()));
            for (int i = 0; i < 200; ++i)
                h2.run (parallel);

            for (int c = 0; c < Harness::numChannels; ++c)
                expectWithinAbsoluteError (h2.outAt (c), h1.outAt (c), 1e-5f);

            // The host re-shares cores while audio runs: any cap, including
            // none and changing it mid-stream, must give identical audio.
            AudioEngine capped;
            capped.startWorkers (3, {}, Harness::block, 48000.0);
            Harness h3;
            capped.setPlan (build (makeRacks()));
            for (int i = 0; i < 200; ++i)
            {
                capped.setMaxWorkers (i % 4); // 0 (caller only) .. 3
                h3.run (capped);
            }

            expectEquals (capped.getNumWorkers(), 3);
            for (int c = 0; c < Harness::numChannels; ++c)
                expectWithinAbsoluteError (h3.outAt (c), h1.outAt (c), 1e-5f);
        }

        beginTest ("a plugin that outputs silence for 2 s is flagged, not bypassed");
        {
            AudioEngine engine;
            Harness h;
            auto r = rack ("a", { 0 }, { 0 });
            auto mute = slotWith (1, [] (juce::AudioBuffer<float>& b) { b.clear(); });
            r.slots = { mute };
            engine.setPlan (build ({ r }));

            for (int i = 0; i < (int) (1.5 * 48000 / Harness::block); ++i)
                h.run (engine);
            expect (! mute->silentOutput.load());

            for (int i = 0; i < (int) (1.0 * 48000 / Harness::block); ++i)
                h.run (engine);
            expect (mute->silentOutput.load());
            expect (mute->fault.load() == FaultReason::none);
        }

        beginTest ("two interfaces: a rack crossing from A's input to B's output");
        {
            RackSpec x;
            x.id = "x";
            x.inputs = { { "A", 1 } };
            x.outputs = { { "B", 0 } };
            x.runtime = std::make_shared<RackRuntime> ("x");
            x.slots = { slotWith (1, gain (2.0f)) };

            RackSpec local;
            local.id = "local";
            local.inputs = { { "A", 0 } };
            local.outputs = { { "A", 3 } };
            local.runtime = std::make_shared<RackRuntime> ("local");

            PlanSpec s;
            s.racks = { x, local };
            s.devices = { DeviceSpec { "A", 4, 4, Harness::block }, DeviceSpec { "B", 4, 4, Harness::block } };
            auto r = buildPlan (s);

            AudioEngine a, b;
            a.setPlan (test::takePlan (r, "A"));
            b.setPlan (test::takePlan (r, "B"));
            Harness ha, hb;

            for (int i = 0; i < 200; ++i) // interleave like two devices
            {
                ha.run (a);
                hb.run (b);
            }

            expectWithinAbsoluteError (hb.outAt (0), 0.2f * 2.0f, 1e-4f);  // A in 2 (0.2) x2 on B out 1
            expectWithinAbsoluteError (ha.outAt (3), 0.1f, 1e-6f);         // local rack: no bridge
            expectWithinAbsoluteError (ha.outAt (0), 0.0f, 1e-6f);
        }

        beginTest ("latency stays the same when a latent plugin is bypassed or faults");
        {
            for (int mode = 0; mode < 3; ++mode)   // 0 active, 1 user-bypassed, 2 faulted
            {
                AudioEngine engine;
                auto r = rack ("a", { 0 }, { 0 });
                auto s = latentSlot (10);
                if (mode == 1) s->userBypassed = true;
                if (mode == 2) s->fault = FaultReason::nonFinite;
                r.slots = { s };
                engine.setPlan (build ({ r }));
                expectEquals (impulseArrival (engine, 1)[0], 15, "mode " + juce::String (mode));
            }
        }

        beginTest ("bypassing mid-stream doesn't move the signal in time");
        {
            AudioEngine engine;
            auto r = rack ("a", { 0 }, { 0 });
            auto s = latentSlot (100);
            r.slots = { s };
            engine.setPlan (build ({ r }));
            // Impulse enters the plugin in block 0; bypass flips on in block 1,
            // while the impulse is still inside the plugin's delay.
            auto arrivals = impulseArrival (engine, 1, 8, 64, [&] (int b) { s->userBypassed = (b >= 1); });
            expectEquals (arrivals[0], 105);
        }

        beginTest ("dry fallback keeps the rack's latency");
        {
            AudioEngine engine;
            auto r = rack ("a", { 0 }, { 0 });
            r.critical = true;
            r.slots = { latentSlot (10) };
            engine.setPlan (build ({ r }));
            r.runtime->dryFallback = true;
            expectEquals (impulseArrival (engine, 1)[0], 15);
        }

        beginTest ("aligned racks: a plain rack waits for the latent one");
        {
            AudioEngine engine;
            auto slow = rack ("slow", { 0 }, { 0 });
            slow.slots = { latentSlot (24) };
            auto fast = rack ("fast", { 0 }, { 1 });
            engine.setPlan (build ({ slow, fast }));
            auto a = impulseArrival (engine, 2);
            expectEquals (a[0], 29);
            expectEquals (a[1], 29);
        }

        beginTest ("alignment can be turned off");
        {
            AudioEngine engine;
            auto slow = rack ("slow", { 0 }, { 0 });
            slow.slots = { latentSlot (24) };
            auto fast = rack ("fast", { 0 }, { 1 });
            auto s = test::singleDevice ({ slow, fast }, Harness::numChannels, Harness::numChannels, Harness::block);
            s.alignRacks = false;
            auto built = buildPlan (s);
            expectEquals (built.latencies["fast"], 0);
            expectEquals (built.latencies["slow"], 24);
            engine.setPlan (test::takePlan (built));
            auto a = impulseArrival (engine, 2);
            expectEquals (a[0], 29);
            expectEquals (a[1], 5);
        }

        beginTest ("old plans are freed once the audio thread has moved on");
        {
            AudioEngine engine;
            Harness h;
            auto r = rack ("a", { 0 }, { 0 });
            auto s = slotWith (1, gain (1.0f));
            r.slots = { s };
            const auto before = s.use_count();
            engine.setPlan (build ({ r }));
            expectEquals ((int) s.use_count(), (int) before + 1);
            engine.setPlan (build ({}));
            h.run (engine);
            engine.collectGarbage();
            expectEquals ((int) s.use_count(), (int) before);
        }
    }
};

static EngineTests engineTests;
