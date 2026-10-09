#pragma once

#include "Core/PlanBuilder.h"

// Shorthand for tests that only care about one interface called "dev".
namespace lr::test
{
inline std::vector<PortRef> ports (std::initializer_list<int> channels, const juce::String& device = "dev")
{
    std::vector<PortRef> result;
    for (int c : channels)
        result.push_back ({ device, c });
    return result;
}

inline PlanSpec singleDevice (std::vector<RackSpec> racks, int inputs, int outputs, int block, double sampleRate = 48000.0)
{
    PlanSpec s;
    s.racks = std::move (racks);
    s.devices = { DeviceSpec { "dev", inputs, outputs, block } };
    s.sampleRate = sampleRate;
    return s;
}

// The plan of one interface out of a build result (moved out).
inline std::unique_ptr<RenderPlan> takePlan (BuildResult& r, const juce::String& device = "dev")
{
    auto it = r.plans.find (device);
    return it != r.plans.end() ? std::move (it->second) : nullptr;
}
} // namespace lr::test
