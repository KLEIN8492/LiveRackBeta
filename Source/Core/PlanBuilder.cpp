#include "PlanBuilder.h"
#include <map>

namespace lr
{
namespace
{
std::vector<int> keepValidChannels (const std::vector<int>& channels, int numAvailable)
{
    std::vector<int> result;
    for (int c : channels)
        if (c >= 0 && c < numAvailable)
            result.push_back (c);
    return result;
}

int pluginChannels (const juce::AudioPluginInstance& p)
{
    return juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
}
} // namespace

BuildResult buildPlan (const PlanSpec& spec)
{
    const auto numRacks = spec.racks.size();

    std::map<juce::String, int> indexOf;
    for (size_t i = 0; i < numRacks; ++i)
        indexOf[spec.racks[i].id] = (int) i;

    // Resolve sources.
    std::vector<std::vector<int>> sourceJobs (numRacks);
    for (size_t i = 0; i < numRacks; ++i)
    {
        for (auto& src : spec.racks[i].sources)
        {
            auto it = indexOf.find (src);
            if (it == indexOf.end())
                return { nullptr, "Unknown source rack: " + src };
            sourceJobs[i].push_back (it->second);
        }
    }

    // Kahn's algorithm, one level at a time.
    std::vector<int> pending (numRacks);
    std::vector<std::vector<int>> feeds (numRacks);
    for (size_t i = 0; i < numRacks; ++i)
    {
        pending[i] = (int) sourceJobs[i].size();
        for (int s : sourceJobs[i])
            feeds[(size_t) s].push_back ((int) i);
    }

    auto plan = std::make_unique<RenderPlan>();
    plan->maxBlockSize = spec.maxBlockSize;
    plan->sampleRate = spec.sampleRate;

    std::vector<int> current;
    for (size_t i = 0; i < numRacks; ++i)
        if (pending[i] == 0)
            current.push_back ((int) i);

    size_t placed = 0;
    while (! current.empty())
    {
        placed += current.size();
        std::vector<int> next;
        for (int j : current)
            for (int f : feeds[(size_t) j])
                if (--pending[(size_t) f] == 0)
                    next.push_back (f);

        plan->levels.push_back (std::move (current));
        current = std::move (next);
    }

    if (placed != numRacks)
        return { nullptr, "Routing has a cycle: a bus feeds itself through other racks" };

    // Jobs keep the spec's order so indices match sourceJobs.
    plan->jobs.resize (numRacks);
    for (size_t i = 0; i < numRacks; ++i)
    {
        auto& job = plan->jobs[i];
        const auto& rs = spec.racks[i];

        job.spec = rs;
        job.spec.slots.clear();
        job.spec.numChannels = juce::jlimit (1, 2, rs.numChannels);
        job.spec.inputs = keepValidChannels (rs.inputs, spec.numDeviceInputs);
        job.spec.outputs = keepValidChannels (rs.outputs, spec.numDeviceOutputs);
        job.sourceJobs = sourceJobs[i];

        int bufferChannels = job.spec.numChannels;
        for (auto& slot : rs.slots)
        {
            if (slot == nullptr || slot->plugin == nullptr)
                continue;

            SlotJob sj;
            sj.slot = slot;
            sj.procChannels = juce::jmax (job.spec.numChannels, pluginChannels (*slot->plugin));
            bufferChannels = juce::jmax (bufferChannels, sj.procChannels);
            job.slots.push_back (std::move (sj));
        }

        job.buffer.setSize (bufferChannels, spec.maxBlockSize);
        job.dry.setSize (job.spec.numChannels, spec.maxBlockSize);
        job.backup.setSize (bufferChannels, spec.maxBlockSize);
        job.buffer.clear();
        job.dry.clear();
        job.backup.clear();
        job.midi.ensureSize (4096);
    }

    return { std::move (plan), {} };
}

} // namespace lr
