// Hammers the lock-free parts while audio runs:
//  - two "interfaces" (engines A and B), each on its own real-time-paced thread
//    with worker threads, ~40 racks incl. latent plugins, buses and crossings
//  - the main thread rebuilds and swaps plans every ~2 ms with random edits and
//    flips bypass / fault / reset flags
// Checks: output always finite, no crash, retired plans get freed.
// "Core" runs 3 s; "Soak" runs LIVERACK_SOAK_SECONDS (default 600).

#include "Core/AudioEngine.h"
#include "Core/PlanBuilder.h"
#include "Core/PlanDeploy.h"
#include <set>
#include <thread>

using namespace lr;

namespace
{
class StressPlugin final : public juce::AudioPluginInstance
{
public:
    StressPlugin (int latency, float gain)
        : juce::AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                      .withOutput ("Out", juce::AudioChannelSet::stereo())),
          g (gain), ring ((size_t) latency + 1, 0.0f)
    {
        setLatencySamples (latency);
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float in = b.getSample (0, i) * g;
            b.setSample (0, i, ring[(pos + 1) % ring.size()]);
            ring[pos] = in;
            pos = (pos + 1) % ring.size();
        }
        b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
    }

    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "Stress"; }
    const juce::String getName() const override { return "Stress"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void reset() override { std::fill (ring.begin(), ring.end(), 0.0f); }
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
    float g;
    std::vector<float> ring;
    size_t pos = 0;
};

struct AudioThread
{
    AudioEngine& engine;
    std::atomic<bool> run { true };
    std::atomic<int> nonFinite { 0 };
    std::atomic<juce::int64> callbacks { 0 };
    std::thread thread;

    explicit AudioThread (AudioEngine& e) : engine (e)
    {
        thread = std::thread ([this]
        {
            constexpr int block = 64, chans = 8;
            std::vector<std::vector<float>> in (chans, std::vector<float> (block)), out (chans, std::vector<float> (block));
            std::vector<const float*> ins;
            std::vector<float*> outs;
            for (int c = 0; c < chans; ++c) { ins.push_back (in[(size_t) c].data()); outs.push_back (out[(size_t) c].data()); }

            juce::Random rng;
            auto next = std::chrono::steady_clock::now();
            const auto period = std::chrono::microseconds (1333);

            while (run)
            {
                for (auto& ch : in)
                    for (auto& v : ch)
                        v = rng.nextFloat() - 0.5f;

                engine.audioDeviceIOCallbackWithContext (ins.data(), chans, outs.data(), chans, block, {});
                ++callbacks;

                for (auto& ch : out)
                    for (float v : ch)
                        if (! std::isfinite (v))
                            ++nonFinite;

                next += period;
                std::this_thread::sleep_until (next);
            }
        });
    }

    ~AudioThread()
    {
        run = false;
        thread.join();
    }
};

void stress (juce::UnitTest& t, double seconds)
{
    juce::Random rng (1234);

    // Pool of slots shared across rebuilds, like the host keeps them.
    std::vector<std::shared_ptr<SlotRuntime>> slotPool;
    for (int i = 0; i < 60; ++i)
    {
        auto s = std::make_shared<SlotRuntime> ("s" + juce::String (i));
        s->plugin = std::make_unique<StressPlugin> (i % 4 == 0 ? 32 + i : 0, 0.9f);
        slotPool.push_back (s);
    }

    std::vector<std::shared_ptr<RackRuntime>> rackRuntimes;
    for (int i = 0; i < 40; ++i)
        rackRuntimes.push_back (std::make_shared<RackRuntime> ("r" + juce::String (i)));

    std::map<juce::String, std::shared_ptr<ClockBridge>> bridges;

    auto makeSpec = [&]
    {
        PlanSpec spec;
        spec.devices = { DeviceSpec { "A", 8, 8, 64 }, DeviceSpec { "B", 8, 8, 64 } };
        spec.alignRacks = rng.nextBool();
        spec.bridges = [&] (const juce::String& key, int channels, int pb, int cb)
        {
            auto& b = bridges[key];
            if (b == nullptr) { b = std::make_shared<ClockBridge> (channels); b->configure (pb, cb); }
            return b;
        };

        for (int i = 0; i < 40; ++i)
        {
            RackSpec r;
            r.id = "r" + juce::String (i);
            r.numChannels = 1 + rng.nextInt (2);
            r.runtime = rackRuntimes[(size_t) i];
            r.critical = rng.nextBool();
            r.bypassed = rng.nextInt (10) == 0;

            const juce::String inDev = rng.nextBool() ? "A" : "B", outDev = rng.nextInt (4) == 0 ? (inDev == "A" ? "B" : "A") : inDev;
            if (i >= 34 && i > 0) // a few buses
            {
                r.sources = { "r" + juce::String (rng.nextInt (30)), "r" + juce::String (rng.nextInt (30)) };
                r.sources.erase (std::unique (r.sources.begin(), r.sources.end()), r.sources.end());
            }
            else
            {
                r.inputs = { { inDev, rng.nextInt (8) } };
            }
            r.outputs = { { outDev, rng.nextInt (8) } };

            // Each rack owns its plugins (as in the host); its home interface
            // changes at random, which exercises the two-phase deploy.
            if (rng.nextInt (3) > 0)            r.slots.push_back (slotPool[(size_t) i]);
            if (i < 20 && rng.nextBool())       r.slots.push_back (slotPool[(size_t) (40 + i)]);

            spec.racks.push_back (std::move (r));
        }
        return spec;
    };

    std::map<juce::String, juce::String> homes;

    AudioEngine a, b;
    a.startWorkers (2, {}, 64, 48000.0);
    b.startWorkers (2, {}, 64, 48000.0);
    a.beginStandalone();
    b.beginStandalone();

    juce::int64 rebuilds = 0;
    {
        AudioThread ta (a), tb (b);
        const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;

        while (juce::Time::getMillisecondCounterHiRes() < end)
        {
            auto spec = makeSpec();
            auto result = deployPlans (spec, homes, { { "A", &a }, { "B", &b } });
            if (result.error.isEmpty())
                ++rebuilds;

            for (int k = 0; k < 6; ++k)
            {
                auto& s = *slotPool[(size_t) rng.nextInt (60)];
                switch (rng.nextInt (4))
                {
                    case 0: s.userBypassed = ! s.userBypassed.load(); break;
                    case 1: s.fault = rng.nextBool() ? FaultReason::none : FaultReason::nonFinite; break;
                    case 2: s.resetRequested = true; break;
                    default: rackRuntimes[(size_t) rng.nextInt (40)]->dryFallback = rng.nextBool(); break;
                }
            }

            a.collectGarbage();
            b.collectGarbage();
            juce::Thread::sleep (2);
        }

        t.logMessage ("  " + juce::String (seconds, 0) + " s: " + juce::String (rebuilds) + " plan swaps, "
                      + juce::String (ta.callbacks.load()) + " + " + juce::String (tb.callbacks.load()) + " callbacks");
        t.expectEquals (ta.nonFinite.load() + tb.nonFinite.load(), 0, "non-finite samples reached an output");
        t.expect (ta.callbacks > 1000 && tb.callbacks > 1000, "audio threads didn't run");
    }

    // Audio stopped: everything retired must be freeable now.
    a.audioDeviceStopped();
    b.audioDeviceStopped();
    a.clearPlan();
    b.clearPlan();
    t.expect (a.waitUntilOldPlansReleased (100) && b.waitUntilOldPlansReleased (100), "retired plans not freed");
}
} // namespace

class StressTests final : public juce::UnitTest
{
public:
    StressTests() : juce::UnitTest ("Stress", "Core") {}
    void runTest() override
    {
        beginTest ("plan swaps + flag flips while two interfaces run (3 s)");
        stress (*this, 3.0);
    }
};

class SoakTests final : public juce::UnitTest
{
public:
    SoakTests() : juce::UnitTest ("Soak", "Soak") {}
    void runTest() override
    {
        const auto env = std::getenv ("LIVERACK_SOAK_SECONDS");
        const double seconds = env != nullptr ? juce::String (env).getDoubleValue() : 600.0;
        beginTest ("long soak (" + juce::String (seconds, 0) + " s)");
        stress (*this, seconds);
    }
};

static StressTests stressTests;
static SoakTests soakTests;
