#include "Core/ClockBridge.h"
#include <cmath>

using namespace lr;

namespace
{
// Event-driven simulation of two interfaces with independent clocks.
struct SimResult
{
    int underruns = 0, overruns = 0, underrunsAfterSettle = 0;
    double minFill = 1e9, maxFill = -1e9;
    double worstSecondDiff = 0.0;   // output discontinuity measure
    double measuredDelay = 0.0;     // samples, from cross-correlation of a click
    double target = 0.0;
    double finalRatioPpm = 0.0;
};

SimResult simulate (double driftPpm, int producerBlock, int consumerBlock, double jitterBlocks,
                    double seconds, unsigned seed = 1)
{
    constexpr double fs = 48000.0;
    const double producerRate = fs * (1.0 + driftPpm * 1e-6);
    const double freq = 440.0, amp = 0.5;

    ClockBridge bridge (1);
    bridge.configure (producerBlock, consumerBlock);

    std::vector<float> in ((size_t) producerBlock), out ((size_t) consumerBlock);
    juce::Random rng ((juce::int64) seed);

    double tProd = 0.0, tCons = 0.0;
    juce::int64 produced = 0, consumed = 0;
    SimResult r;
    float y1 = 0.0f, y2 = 0.0f;
    bool settled = false;
    int underrunsAtSettle = 0;
    double ratioSum = 0.0;
    int ratioCount = 0;
    const double settle = 3.0;

    while (tCons < seconds)
    {
        const double jp = jitterBlocks * producerBlock / producerRate * (rng.nextDouble() - 0.5);
        const double jc = jitterBlocks * consumerBlock / fs * (rng.nextDouble() - 0.5);

        if (tProd + jp <= tCons + jc)
        {
            for (int i = 0; i < producerBlock; ++i)
                in[(size_t) i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) (produced + i) / producerRate));
            float* chans[] = { in.data() };
            bridge.write (chans, producerBlock);
            produced += producerBlock;
            tProd += producerBlock / producerRate;
        }
        else
        {
            float* chans[] = { out.data() };
            bridge.read (chans, consumerBlock);
            consumed += consumerBlock;
            tCons += consumerBlock / fs;

            if (tCons > settle && ! settled)
            {
                settled = true;
                underrunsAtSettle = bridge.getStats().underruns;
            }

            if (tCons > settle)
            {
                const auto s = bridge.getStats();
                r.minFill = juce::jmin (r.minFill, s.fill);
                r.maxFill = juce::jmax (r.maxFill, s.fill);
                for (float y : out)
                {
                    r.worstSecondDiff = juce::jmax (r.worstSecondDiff, (double) std::abs (y - 2.0f * y1 + y2));
                    y2 = y1;
                    y1 = y;
                }
            }
            else
            {
                y2 = out[out.size() - 2];
                y1 = out.back();
            }

            if (tCons > seconds - 20.0)
            {
                ratioSum += bridge.getStats().ratioPpm;
                ++ratioCount;
            }
        }
    }

    const auto s = bridge.getStats();
    r.underruns = s.underruns;
    r.underrunsAfterSettle = s.underruns - underrunsAtSettle;
    r.overruns = s.overruns;
    r.target = s.target;
    r.finalRatioPpm = ratioCount > 0 ? ratioSum / ratioCount : s.ratioPpm; // mean over the last 20 s
    return r;
}
} // namespace

class ClockBridgeTests final : public juce::UnitTest
{
public:
    ClockBridgeTests() : juce::UnitTest ("ClockBridge", "Core") {}

    void runTest() override
    {
        // Second difference of a clean 440 Hz sine at amplitude 0.5 ~ 0.0017.
        const double cleanLimit = 0.01;

        struct Case { double ppm; int pb, cb; double jitter; };
        const Case cases[] = {
            { 0.0, 32, 32, 0.0 },
            { 200.0, 32, 32, 0.3 },
            { -200.0, 32, 32, 0.3 },
            { 150.0, 32, 64, 0.3 },
            { -150.0, 64, 32, 0.3 },
            { 80.0, 48, 32, 0.5 },
            { 500.0, 32, 32, 0.3 },
        };

        for (auto& c : cases)
        {
            beginTest (juce::String (c.ppm) + " ppm, producer " + juce::String (c.pb) + " / consumer " + juce::String (c.cb)
                       + ", jitter " + juce::String (c.jitter) + " block");
            auto r = simulate (c.ppm, c.pb, c.cb, c.jitter, 60.0);
            logMessage ("  target " + juce::String (r.target, 1) + " smp, fill " + juce::String (r.minFill, 1) + ".."
                        + juce::String (r.maxFill, 1) + ", ratio " + juce::String (r.finalRatioPpm, 1) + " ppm, underruns "
                        + juce::String (r.underruns) + ", overruns " + juce::String (r.overruns)
                        + ", worst 2nd diff " + juce::String (r.worstSecondDiff, 5));

            expectEquals (r.overruns, 0);
            expectEquals (r.underrunsAfterSettle, 0, "glitches after settling");
            expect (r.underruns <= 3, "startup adaptation took too many steps: " + juce::String (r.underruns));
            expect (r.worstSecondDiff < cleanLimit, "discontinuity in output after settling");
            expect (r.minFill > 0.0, "ran dry after settling");
            expect (std::abs (r.finalRatioPpm - c.ppm) < 60.0, "ratio didn't track the drift");
        }

        beginTest ("latency stays near one block at 32 samples");
        {
            auto r = simulate (100.0, 32, 32, 0.3, 30.0);
            expect (r.target <= 3 * 32, "target " + juce::String (r.target));
        }

        beginTest ("consumer outputs silence until primed, then signal");
        {
            ClockBridge bridge (2);
            bridge.configure (32, 32);
            std::vector<float> a (32, 0.5f), b (32, 0.5f), oa (32, 1.0f), ob (32, 1.0f);
            float* outs[] = { oa.data(), ob.data() };
            bridge.read (outs, 32);
            expect (oa[0] == 0.0f && ob[31] == 0.0f);

            float* ins[] = { a.data(), b.data() };
            for (int i = 0; i < 8; ++i)
                bridge.write (ins, 32);
            bridge.read (outs, 32);
            expect (std::abs (oa[16] - 0.5f) < 1e-4f && std::abs (ob[16] - 0.5f) < 1e-4f);
        }

        beginTest ("after warm-up the latency is locked: a late callback glitches once, delay unchanged");
        {
            ClockBridge bridge (1);
            bridge.configure (32, 32);
            bridge.setWarmupSamples (48000);           // 1 s
            std::vector<float> a (32, 0.25f), o (32);
            float* ins[] = { a.data() };
            float* outs[] = { o.data() };

            for (int i = 0; i < 4; ++i) bridge.write (ins, 32);
            for (int i = 0; i < 2000; ++i) { bridge.write (ins, 32); bridge.read (outs, 32); } // > 1 s
            const double locked = bridge.getStats().target;
            expect (bridge.getStats().locked);

            for (int i = 0; i < 6; ++i) bridge.read (outs, 32); // producer stalls: underrun
            expectEquals (bridge.getStats().underruns, 1);
            expectEquals (bridge.getStats().target, locked);
        }

        beginTest ("add mode sums into the destination");
        {
            ClockBridge bridge (1);
            bridge.configure (32, 32);
            std::vector<float> a (32, 0.25f), o (32, 0.5f);
            float* ins[] = { a.data() };
            for (int i = 0; i < 8; ++i)
                bridge.write (ins, 32);
            float* outs[] = { o.data() };
            bridge.read (outs, 32, true);
            expect (std::abs (o[10] - 0.75f) < 1e-4f);
        }
    }
};

static ClockBridgeTests clockBridgeTests;
