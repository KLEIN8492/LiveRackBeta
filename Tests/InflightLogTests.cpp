#include "Core/InflightLog.h"

using namespace lr;

class InflightLogTests final : public juce::UnitTest
{
public:
    InflightLogTests() : juce::UnitTest ("InflightLog", "Core") {}

    void runTest() override
    {
        auto file = juce::File::createTempFile ("inflight");

        beginTest ("a slot left marked is reported as suspect after close");
        {
            InflightLog log;
            expect (log.open (file));
            const int a = log.allocateRecord ("slot-a");
            const int b = log.allocateRecord ("slot-b");
            expect (a >= 0 && b >= 0 && a != b);

            log.mark (a, true);
            log.mark (b, true);
            log.mark (b, false);
            // Simulate a crash: no clean-up, just drop the mapping.
        }

        auto suspects = InflightLog::readSuspects (file);
        expectEquals ((int) suspects.size(), 1);
        if (! suspects.empty())
            expectEquals (suspects[0], juce::String ("slot-a"));

        beginTest ("re-opening clears old flags");
        {
            InflightLog log;
            expect (log.open (file));
        }
        expect (InflightLog::readSuspects (file).empty());

        beginTest ("released records are reused");
        {
            InflightLog log;
            log.open (file);
            const int a = log.allocateRecord ("x");
            log.releaseRecord (a);
            expectEquals (log.allocateRecord ("y"), a);
        }

        file.deleteFile();
    }
};

static InflightLogTests inflightLogTests;
