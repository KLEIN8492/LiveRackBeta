#include "PlanBuilder.h"
#include <map>
#include <set>

namespace lr
{
namespace
{
int pluginChannels (const juce::AudioPluginInstance& p)
{
    return juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
}

// Rack channel fed by the k-th input port (or -1 = all rack channels).
int rackChannelForInput (size_t k, size_t numPorts, int rackChannels)
{
    return numPorts == 1 ? -1 : (int) (k % (size_t) rackChannels);
}

// Rack channel sent to the k-th output port.
int rackChannelForOutput (size_t k, int rackChannels)
{
    return rackChannels == 1 ? 0 : (int) (k % (size_t) rackChannels);
}
} // namespace

BuildResult buildPlan (const PlanSpec& spec)
{
    BuildResult result;
    const auto numRacks = spec.racks.size();

    std::map<juce::String, const DeviceSpec*> devices;
    for (auto& d : spec.devices)
        devices[d.name] = &d;

    auto valid = [&] (const PortRef& p, bool isInput)
    {
        auto it = devices.find (p.device);
        if (it == devices.end() || p.channel < 0)
            return false;
        return p.channel < (isInput ? it->second->numInputs : it->second->numOutputs);
    };

    std::map<juce::String, int> indexOf;
    for (size_t i = 0; i < numRacks; ++i)
        indexOf[spec.racks[i].id] = (int) i;

    // ---- sources + cycle check (global graph) -------------------------------
    std::vector<std::vector<int>> sourcesOf (numRacks);
    for (size_t i = 0; i < numRacks; ++i)
        for (auto& src : spec.racks[i].sources)
        {
            auto it = indexOf.find (src);
            if (it == indexOf.end())
                return { {}, {}, "Unknown source rack: " + src };
            sourcesOf[i].push_back (it->second);
        }

    std::vector<int> pending (numRacks);
    std::vector<std::vector<int>> feeds (numRacks);
    for (size_t i = 0; i < numRacks; ++i)
    {
        pending[i] = (int) sourcesOf[i].size();
        for (int s : sourcesOf[i])
            feeds[(size_t) s].push_back ((int) i);
    }

    std::vector<std::vector<int>> globalLevels;
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
        globalLevels.push_back (std::move (current));
        current = std::move (next);
    }

    if (placed != numRacks)
        return { {}, {}, "Routing has a cycle: a bus feeds itself through other racks" };

    if (spec.devices.empty())
        return result; // nothing open: no plans, not an error

    // ---- home interface of every rack --------------------------------------
    std::vector<juce::String> home (numRacks);
    for (size_t i = 0; i < numRacks; ++i)
    {
        for (auto& p : spec.racks[i].outputs)
            if (valid (p, false)) { home[i] = p.device; break; }
        if (home[i].isEmpty())
            for (auto& p : spec.racks[i].inputs)
                if (valid (p, true)) { home[i] = p.device; break; }
    }
    for (auto& level : globalLevels)            // buses inherit from their first source
        for (int i : level)
            if (home[(size_t) i].isEmpty())
                for (int s : sourcesOf[(size_t) i])
                    if (home[(size_t) s].isNotEmpty()) { home[(size_t) i] = home[(size_t) s]; break; }
    for (auto& h : home)
        if (h.isEmpty())
            h = spec.devices.front().name;

    for (size_t i = 0; i < numRacks; ++i)
        result.homes[spec.racks[i].id] = home[i];

    // ---- one plan per interface ---------------------------------------------
    std::map<juce::String, std::map<int, int>> jobIndex; // device -> rack index -> job index
    for (auto& d : spec.devices)
    {
        auto plan = std::make_unique<RenderPlan>();
        plan->device = d.name;
        plan->maxBlockSize = d.blockSize;
        plan->sampleRate = spec.sampleRate;
        result.plans[d.name] = std::move (plan);
    }

    for (auto& level : globalLevels)
    {
        std::map<juce::String, std::vector<int>> perDevice;
        for (int i : level)
        {
            auto& plan = *result.plans[home[(size_t) i]];
            const int j = (int) plan.jobs.size();
            plan.jobs.emplace_back();
            jobIndex[home[(size_t) i]][i] = j;
            perDevice[home[(size_t) i]].push_back (j);
        }
        for (auto& [dev, jobs] : perDevice)
            result.plans[dev]->levels.push_back (jobs);
    }

    auto bridgeFor = [&] (const juce::String& key, int channels, const juce::String& from, const juce::String& to)
    {
        return spec.bridges ? spec.bridges (key, channels, devices[from]->blockSize, devices[to]->blockSize)
                            : std::make_shared<ClockBridge> (channels);
    };

    // Import index for a bridge key on a plan (creating it on first use).
    std::map<std::pair<juce::String, juce::String>, int> importOf; // (device, key) -> import
    auto importFor = [&] (RenderPlan& plan, const juce::String& key, const std::shared_ptr<ClockBridge>& bridge) -> int
    {
        auto k = std::make_pair (plan.device, key);
        if (auto it = importOf.find (k); it != importOf.end())
            return it->second;

        Import im;
        im.bridge = bridge;
        im.buffer.setSize (bridge->getNumChannels(), plan.maxBlockSize);
        im.buffer.clear();
        plan.imports.push_back (std::move (im));
        return importOf[k] = (int) plan.imports.size() - 1;
    };

    // Rack output streams to other interfaces: one bridge per (rack, interface).
    std::set<juce::String> exportedRackKeys;
    auto rackStream = [&] (size_t rack, const juce::String& to) -> std::pair<juce::String, std::shared_ptr<ClockBridge>>
    {
        const auto& rs = spec.racks[rack];
        const int channels = juce::jlimit (1, 2, rs.numChannels);
        const auto key = "rack|" + rs.id + "|" + to + "|" + juce::String (channels);
        auto bridge = bridgeFor (key, channels, home[rack], to);

        if (exportedRackKeys.insert (key).second)
        {
            RackExport ex;
            ex.bridge = bridge;
            ex.job = jobIndex[home[rack]][(int) rack];
            ex.ptrs.assign ((size_t) channels, nullptr);
            result.plans[home[rack]]->rackExports.push_back (std::move (ex));
        }
        return { key, bridge };
    };

    // Input channels crossing interfaces: one bridge per (from, to) pair.
    std::map<std::pair<juce::String, juce::String>, std::vector<int>> crossingInputs;
    for (size_t i = 0; i < numRacks; ++i)
        for (auto& p : spec.racks[i].inputs)
            if (valid (p, true) && p.device != home[i])
            {
                auto& chans = crossingInputs[{ p.device, home[i] }];
                if (std::find (chans.begin(), chans.end(), p.channel) == chans.end())
                    chans.push_back (p.channel);
            }

    std::map<std::pair<juce::String, juce::String>, std::pair<juce::String, std::shared_ptr<ClockBridge>>> inputStreams;
    for (auto& [route, chans] : crossingInputs)
    {
        std::sort (chans.begin(), chans.end());
        juce::StringArray list;
        for (int c : chans)
            list.add (juce::String (c));

        const auto key = "in|" + route.first + "|" + route.second + "|" + list.joinIntoString (",");
        auto bridge = bridgeFor (key, (int) chans.size(), route.first, route.second);
        inputStreams[route] = { key, bridge };

        InputExport ex;
        ex.bridge = bridge;
        ex.deviceChannels = chans;
        ex.ptrs.assign (chans.size(), nullptr);
        result.plans[route.first]->inputExports.push_back (std::move (ex));
    }

    // ---- fill every job -------------------------------------------------------
    for (size_t i = 0; i < numRacks; ++i)
    {
        const auto& rs = spec.racks[i];
        auto& plan = *result.plans[home[i]];
        auto& job = plan.jobs[(size_t) jobIndex[home[i]][(int) i]];

        job.spec = rs;
        job.spec.slots.clear();
        job.spec.numChannels = juce::jlimit (1, 2, rs.numChannels);
        const int rc = job.spec.numChannels;

        // Inputs.
        std::vector<PortRef> ins;
        for (auto& p : rs.inputs)
            if (valid (p, true))
                ins.push_back (p);
        job.spec.inputs = ins;

        for (size_t k = 0; k < ins.size(); ++k)
        {
            InputTap tap;
            tap.rackChannel = rackChannelForInput (k, ins.size(), rc);

            if (ins[k].device == home[i])
            {
                tap.channel = ins[k].channel;
            }
            else
            {
                auto& [key, bridge] = inputStreams[{ ins[k].device, home[i] }];
                const auto& chans = crossingInputs[{ ins[k].device, home[i] }];
                tap.fromImport = true;
                tap.import = importFor (plan, key, bridge);
                tap.channel = (int) (std::find (chans.begin(), chans.end(), ins[k].channel) - chans.begin());
            }
            job.taps.push_back (tap);
        }

        // Bus sources.
        for (int s : sourcesOf[i])
        {
            if (home[(size_t) s] == home[i])
            {
                job.sourceJobs.push_back (jobIndex[home[i]][s]);
            }
            else
            {
                auto [key, bridge] = rackStream ((size_t) s, home[i]);
                job.sourceImports.push_back (importFor (plan, key, bridge));
            }
        }

        // Outputs: local ones directly, others via a rack stream to that interface.
        std::vector<PortRef> outs;
        for (auto& p : rs.outputs)
            if (valid (p, false))
                outs.push_back (p);
        job.spec.outputs = outs;

        for (size_t k = 0; k < outs.size(); ++k)
        {
            const int rch = rackChannelForOutput (k, rc);
            if (outs[k].device == home[i])
            {
                job.localOutputs.push_back ({ outs[k].channel, rch });
            }
            else
            {
                auto& remote = *result.plans[outs[k].device];
                auto [key, bridge] = rackStream (i, outs[k].device);
                const int im = importFor (remote, key, bridge);
                remote.imports[(size_t) im].toOutputs.push_back ({ outs[k].channel, rch });
            }
        }

        // Plugins and buffers.
        int bufferChannels = rc;
        for (auto& slot : rs.slots)
        {
            if (slot == nullptr || slot->plugin == nullptr)
                continue;

            SlotJob sj;
            sj.slot = slot;
            sj.procChannels = juce::jmax (rc, pluginChannels (*slot->plugin));
            bufferChannels = juce::jmax (bufferChannels, sj.procChannels);
            job.slots.push_back (std::move (sj));
        }

        job.buffer.setSize (bufferChannels, plan.maxBlockSize);
        job.dry.setSize (rc, plan.maxBlockSize);
        job.backup.setSize (bufferChannels, plan.maxBlockSize);
        job.buffer.clear();
        job.dry.clear();
        job.backup.clear();
        job.midi.ensureSize (4096);
    }

    return result;
}

} // namespace lr
