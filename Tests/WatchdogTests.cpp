#include "Core/Watchdog.h"
#include <limits>

using namespace lr;

class WatchdogTests final : public juce::UnitTest
{
public:
    WatchdogTests() : juce::UnitTest ("Watchdog", "Core") {}

    void runTest() override
    {
        const WatchdogConfig cfg;
        const double sr = 48000.0;
        const int block = 480; // 10 ms

        juce::AudioBuffer<float> buf (2, block);

        beginTest ("clean audio passes");
        {
            SlotHealth h;
            fill (buf, 0.5f);
            expect (checkSlotOutput (h, buf, 2, block, sr, cfg) == FaultReason::none);
        }

        beginTest ("NaN and Inf are caught on any channel");
        {
            SlotHealth h;
            fill (buf, 0.1f);
            buf.setSample (1, 100, std::numeric_limits<float>::quiet_NaN());
            expect (checkSlotOutput (h, buf, 2, block, sr, cfg) == FaultReason::nonFinite);

            fill (buf, 0.1f);
            buf.setSample (0, 7, std::numeric_limits<float>::infinity());
            expect (checkSlotOutput (h, buf, 2, block, sr, cfg) == FaultReason::nonFinite);
        }

        beginTest ("short loud burst is tolerated, sustained overload is not");
        {
            SlotHealth h;
            fill (buf, 31.6f); // +30 dBFS
            expect (checkSlotOutput (h, buf, 2, block, sr, cfg) == FaultReason::none);

            FaultReason r = FaultReason::none;
            for (int i = 0; i < 5 && r == FaultReason::none; ++i)
                r = checkSlotOutput (h, buf, 2, block, sr, cfg);
            expect (r == FaultReason::overload);
        }

        beginTest ("a quiet block resets the overload counter");
        {
            SlotHealth h;
            fill (buf, 31.6f);
            for (int i = 0; i < 4; ++i)
                checkSlotOutput (h, buf, 2, block, sr, cfg);
            fill (buf, 0.5f);
            checkSlotOutput (h, buf, 2, block, sr, cfg);
            fill (buf, 31.6f);
            expect (checkSlotOutput (h, buf, 2, block, sr, cfg) == FaultReason::none);
        }

        beginTest ("overtime needs consecutive slow blocks");
        {
            RackHealth h;
            const double period = 0.001;
            for (int i = 0; i < 7; ++i)
                expect (checkRackTiming (h, 0.00095, period, cfg) == FaultReason::none);
            expect (checkRackTiming (h, 0.00095, period, cfg) == FaultReason::overtime);

            RackHealth h2;
            for (int i = 0; i < 7; ++i)
                checkRackTiming (h2, 0.00095, period, cfg);
            checkRackTiming (h2, 0.0001, period, cfg);
            expect (checkRackTiming (h2, 0.00095, period, cfg) == FaultReason::none);
        }

        beginTest ("fault policy");
        {
            auto a = chooseFaultAction (false, FaultReason::nonFinite);
            expect (a.bypassSlot && ! a.rackToDry);

            auto b = chooseFaultAction (true, FaultReason::nonFinite);
            expect (b.bypassSlot && b.rackToDry);

            auto c = chooseFaultAction (true, FaultReason::overtime);
            expect (c.rackToDry);

            auto d = chooseFaultAction (false, FaultReason::none);
            expect (! d.bypassSlot && ! d.rackToDry);
        }
    }

private:
    static void fill (juce::AudioBuffer<float>& b, float v)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            juce::FloatVectorOperations::fill (b.getWritePointer (c), v, b.getNumSamples());
    }
};

static WatchdogTests watchdogTests;
