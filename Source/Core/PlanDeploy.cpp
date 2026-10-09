#include "PlanDeploy.h"
#include <set>

namespace lr
{
namespace
{
void install (BuildResult& result, const std::map<juce::String, AudioEngine*>& engines)
{
    for (auto& [name, engine] : engines)
    {
        auto it = result.plans.find (name);
        engine->setPlan (it != result.plans.end() ? std::move (it->second) : nullptr);
    }
}

bool waitAll (const std::map<juce::String, AudioEngine*>& engines, int timeoutMs)
{
    bool ok = true;
    for (auto& [name, engine] : engines)
        ok &= engine->waitUntilOldPlansReleased (timeoutMs);
    return ok;
}
} // namespace

BuildResult deployPlans (const PlanSpec& spec,
                         std::map<juce::String, juce::String>& homes,
                         const std::map<juce::String, AudioEngine*>& engines,
                         int releaseTimeoutMs)
{
    auto result = buildPlan (spec);
    if (result.error.isNotEmpty())
        return result;

    std::set<juce::String> moved;
    for (auto& [rack, home] : result.homes)
        if (auto it = homes.find (rack); it != homes.end() && it->second != home)
            moved.insert (rack);

    if (! moved.empty())
    {
        // Phase 1: same routing, but moved racks carry no plugins and use a
        // throwaway runtime (meters, health, delay lines), so nothing of
        // theirs is touched by two interfaces' threads at once.
        auto interim = spec;
        for (auto& r : interim.racks)
            if (moved.count (r.id) > 0)
            {
                r.slots.clear();
                r.runtime = std::make_shared<RackRuntime> (r.id);
            }

        auto first = buildPlan (interim);
        install (first, engines);

        // Every interface must have finished any block still using the old
        // plans before the plugins reappear on their new interface. If one is
        // stuck (hung plugin), keep the plugin-free routing rather than risk it.
        if (! waitAll (engines, releaseTimeoutMs))
        {
            first.error = "An interface did not release the previous routing; moved racks run dry until it recovers";
            homes = first.homes;
            return first;
        }
    }

    install (result, engines);
    homes = result.homes;
    return result;
}

} // namespace lr
