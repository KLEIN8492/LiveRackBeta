#include "TestHelpers.h"

using namespace lr;
using namespace lr::test;

namespace
{
RackSpec rack (juce::String id, std::vector<PortRef> ins, std::vector<juce::String> sources = {},
               std::vector<PortRef> outs = { { "dev", 0 } })
{
    RackSpec r;
    r.id = id;
    r.inputs = std::move (ins);
    r.sources = std::move (sources);
    r.outputs = std::move (outs);
    r.runtime = std::make_shared<RackRuntime> (id);
    return r;
}

PlanSpec spec (std::vector<RackSpec> racks)
{
    return singleDevice (std::move (racks), 8, 8, 256);
}

PlanSpec twoDevices (std::vector<RackSpec> racks)
{
    PlanSpec s;
    s.racks = std::move (racks);
    s.devices = { DeviceSpec { "A", 4, 4, 32 }, DeviceSpec { "B", 2, 2, 64 } };
    return s;
}

int levelOf (const RenderPlan& plan, const juce::String& id)
{
    for (size_t l = 0; l < plan.levels.size(); ++l)
        for (int j : plan.levels[l])
            if (plan.jobs[(size_t) j].spec.id == id)
                return (int) l;
    return -1;
}

const RackJob* findJob (const RenderPlan& plan, const juce::String& id)
{
    for (auto& j : plan.jobs)
        if (j.spec.id == id)
            return &j;
    return nullptr;
}
} // namespace

class PlanBuilderTests final : public juce::UnitTest
{
public:
    PlanBuilderTests() : juce::UnitTest ("PlanBuilder", "Core") {}

    void runTest() override
    {
        beginTest ("independent racks share level 0");
        {
            auto r = buildPlan (spec ({ rack ("a", ports ({ 0 })), rack ("b", ports ({ 1 })) }));
            auto plan = takePlan (r);
            expect (plan != nullptr, r.error);
            expectEquals ((int) plan->levels.size(), 1);
            expectEquals ((int) plan->levels[0].size(), 2);
        }

        beginTest ("bus runs after the racks feeding it");
        {
            auto r = buildPlan (spec ({ rack ("bus", {}, { "a", "b" }), rack ("a", ports ({ 0 })), rack ("b", ports ({ 1 })) }));
            auto plan = takePlan (r);
            expect (plan != nullptr, r.error);
            expectEquals (levelOf (*plan, "a"), 0);
            expectEquals (levelOf (*plan, "b"), 0);
            expectEquals (levelOf (*plan, "bus"), 1);
            expectEquals ((int) findJob (*plan, "bus")->sourceJobs.size(), 2);
        }

        beginTest ("bus of a bus gets level 2");
        {
            auto r = buildPlan (spec ({ rack ("a", ports ({ 0 })), rack ("sub", {}, { "a" }), rack ("main", {}, { "sub" }) }));
            auto plan = takePlan (r);
            expectEquals (levelOf (*plan, "main"), 2);
        }

        beginTest ("cycles are rejected");
        {
            auto r = buildPlan (spec ({ rack ("a", {}, { "b" }), rack ("b", {}, { "a" }) }));
            expect (r.plans.empty());
            expect (r.error.containsIgnoreCase ("cycle"), r.error);
            expect (buildPlan (spec ({ rack ("a", {}, { "a" }) })).error.isNotEmpty());
        }

        beginTest ("unknown source is an error naming it");
        {
            auto r = buildPlan (spec ({ rack ("a", {}, { "ghost" }) }));
            expect (r.error.contains ("ghost"), r.error);
        }

        beginTest ("channels or interfaces that don't exist are dropped");
        {
            auto ins = ports ({ 0, 99, -1 });
            ins.push_back ({ "unplugged", 0 });
            auto r = buildPlan (spec ({ rack ("a", ins, {}, ports ({ 1, 42 })) }));
            auto plan = takePlan (r);
            auto& job = plan->jobs[0];
            expect (job.spec.inputs == ports ({ 0 }));
            expect (job.spec.outputs == ports ({ 1 }));
            expectEquals ((int) job.taps.size(), 1);
        }

        beginTest ("buffers are preallocated to the interface's block size");
        {
            auto s = spec ({ rack ("a", ports ({ 0, 1 })) });
            s.racks[0].numChannels = 2;
            auto r = buildPlan (s);
            auto plan = takePlan (r);
            auto& job = plan->jobs[0];
            expectEquals (job.buffer.getNumSamples(), 256);
            expectEquals (job.buffer.getNumChannels(), 2);
            expectEquals (job.backup.getNumSamples(), 256);
        }

        beginTest ("slots without a loaded plugin are skipped");
        {
            auto s = spec ({ rack ("a", ports ({ 0 })) });
            s.racks[0].slots.push_back (std::make_shared<SlotRuntime> ("not-loaded"));
            auto r = buildPlan (s);
            expect (takePlan (r)->jobs[0].slots.empty());
        }

        // ---- several interfaces ------------------------------------------------

        beginTest ("same-interface rack needs no bridge");
        {
            auto r = buildPlan (twoDevices ({ rack ("a", { { "A", 0 } }, {}, { { "A", 1 } }) }));
            expectEquals (r.homes["a"], juce::String ("A"));
            auto& a = *r.plans["A"];
            expect (a.imports.empty() && a.inputExports.empty() && a.rackExports.empty());
            expect (r.plans["B"]->jobs.empty());
        }

        beginTest ("crossing rack is homed on its output interface; input is bridged");
        {
            auto r = buildPlan (twoDevices ({ rack ("x", { { "A", 2 } }, {}, { { "B", 0 } }) }));
            expectEquals (r.homes["x"], juce::String ("B"));
            auto& a = *r.plans["A"];
            auto& b = *r.plans["B"];
            expectEquals ((int) a.inputExports.size(), 1);
            expect (a.inputExports[0].deviceChannels == std::vector<int> { 2 });
            expectEquals ((int) b.imports.size(), 1);
            expect (a.inputExports[0].bridge == b.imports[0].bridge);
            auto& job = b.jobs[0];
            expect (job.taps[0].fromImport && job.taps[0].import == 0 && job.taps[0].channel == 0);
            expect (job.localOutputs == std::vector<std::pair<int, int>> { { 0, 0 } });
        }

        beginTest ("racks sharing an input interface share one bridge");
        {
            auto r = buildPlan (twoDevices ({ rack ("x", { { "A", 3 } }, {}, { { "B", 0 } }),
                                              rack ("y", { { "A", 1 } }, {}, { { "B", 1 } }) }));
            auto& a = *r.plans["A"];
            expectEquals ((int) a.inputExports.size(), 1);
            expect (a.inputExports[0].deviceChannels == std::vector<int> { 1, 3 });
            expectEquals ((int) r.plans["B"]->imports.size(), 1);
        }

        beginTest ("output on a second interface is bridged from the home interface");
        {
            auto r = buildPlan (twoDevices ({ rack ("m", { { "A", 0 } }, {}, { { "A", 0 }, { "B", 1 } }) }));
            expectEquals (r.homes["m"], juce::String ("A"));
            auto& a = *r.plans["A"];
            auto& b = *r.plans["B"];
            expectEquals ((int) a.rackExports.size(), 1);
            expectEquals ((int) b.imports.size(), 1);
            expect (b.imports[0].toOutputs == std::vector<std::pair<int, int>> { { 1, 0 } });
        }

        beginTest ("bus with a source on another interface imports it");
        {
            auto r = buildPlan (twoDevices ({ rack ("src", { { "A", 0 } }, {}, { { "A", 0 } }),
                                              rack ("bus", {}, { "src" }, { { "B", 0 } }) }));
            auto& b = *r.plans["B"];
            auto* bus = findJob (b, "bus");
            expect (bus != nullptr && bus->sourceJobs.empty() && bus->sourceImports.size() == 1);
            expectEquals ((int) r.plans["A"]->rackExports.size(), 1);
        }

        beginTest ("bridges come from the provider with the right block sizes");
        {
            auto s = twoDevices ({ rack ("x", { { "A", 0 } }, {}, { { "B", 0 } }) });
            juce::StringArray keys;
            int producer = 0, consumer = 0;
            s.bridges = [&] (const juce::String& key, int channels, int pb, int cb)
            {
                keys.add (key);
                producer = pb;
                consumer = cb;
                return std::make_shared<ClockBridge> (channels);
            };
            buildPlan (s);
            expectEquals (keys.size(), 1);
            expectEquals (producer, 32);
            expectEquals (consumer, 64);
        }
    }
};

static PlanBuilderTests planBuilderTests;
