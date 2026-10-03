#include "Core/PlanBuilder.h"

using namespace lr;

namespace
{
RackSpec rack (juce::String id, std::vector<int> ins, std::vector<juce::String> sources = {}, std::vector<int> outs = { 0 })
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
    PlanSpec s;
    s.racks = std::move (racks);
    s.numDeviceInputs = 8;
    s.numDeviceOutputs = 8;
    s.maxBlockSize = 256;
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
} // namespace

class PlanBuilderTests final : public juce::UnitTest
{
public:
    PlanBuilderTests() : juce::UnitTest ("PlanBuilder", "Core") {}

    void runTest() override
    {
        beginTest ("independent racks share level 0");
        {
            auto r = buildPlan (spec ({ rack ("a", { 0 }), rack ("b", { 1 }) }));
            expect (r.plan != nullptr, r.error);
            expectEquals ((int) r.plan->levels.size(), 1);
            expectEquals ((int) r.plan->levels[0].size(), 2);
        }

        beginTest ("bus runs after the racks feeding it");
        {
            auto r = buildPlan (spec ({ rack ("bus", {}, { "a", "b" }), rack ("a", { 0 }), rack ("b", { 1 }) }));
            expect (r.plan != nullptr, r.error);
            expectEquals (levelOf (*r.plan, "a"), 0);
            expectEquals (levelOf (*r.plan, "b"), 0);
            expectEquals (levelOf (*r.plan, "bus"), 1);

            auto& bus = r.plan->jobs[0];
            expectEquals (bus.spec.id, juce::String ("bus"));
            expectEquals ((int) bus.sourceJobs.size(), 2);
            expectEquals (bus.sourceJobs[0], 1);
            expectEquals (bus.sourceJobs[1], 2);
        }

        beginTest ("bus of a bus gets level 2");
        {
            auto r = buildPlan (spec ({ rack ("a", { 0 }), rack ("sub", {}, { "a" }), rack ("main", {}, { "sub" }) }));
            expect (r.plan != nullptr, r.error);
            expectEquals (levelOf (*r.plan, "main"), 2);
        }

        beginTest ("cycles are rejected");
        {
            auto r = buildPlan (spec ({ rack ("a", {}, { "b" }), rack ("b", {}, { "a" }) }));
            expect (r.plan == nullptr);
            expect (r.error.containsIgnoreCase ("cycle"), r.error);

            auto self = buildPlan (spec ({ rack ("a", {}, { "a" }) }));
            expect (self.plan == nullptr);
        }

        beginTest ("unknown source is an error naming it");
        {
            auto r = buildPlan (spec ({ rack ("a", {}, { "ghost" }) }));
            expect (r.plan == nullptr);
            expect (r.error.contains ("ghost"), r.error);
        }

        beginTest ("channels the device doesn't have are dropped");
        {
            auto r = buildPlan (spec ({ rack ("a", { 0, 99, -1 }, {}, { 1, 42 }) }));
            expect (r.plan != nullptr, r.error);
            auto& job = r.plan->jobs[0];
            expect (job.spec.inputs == std::vector<int> { 0 });
            expect (job.spec.outputs == std::vector<int> { 1 });
        }

        beginTest ("buffers are preallocated to the block size");
        {
            auto s = spec ({ rack ("a", { 0, 1 }) });
            s.racks[0].numChannels = 2;
            auto r = buildPlan (s);
            auto& job = r.plan->jobs[0];
            expectEquals (job.buffer.getNumSamples(), 256);
            expectEquals (job.buffer.getNumChannels(), 2);
            expectEquals (job.dry.getNumSamples(), 256);
            expectEquals (job.backup.getNumSamples(), 256);
        }

        beginTest ("slots without a loaded plugin are skipped");
        {
            auto s = spec ({ rack ("a", { 0 }) });
            s.racks[0].slots.push_back (std::make_shared<SlotRuntime> ("not-loaded"));
            auto r = buildPlan (s);
            expect (r.plan->jobs[0].slots.empty());
        }
    }
};

static PlanBuilderTests planBuilderTests;
