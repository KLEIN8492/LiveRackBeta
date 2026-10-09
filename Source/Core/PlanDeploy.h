#pragma once

#include "PlanBuilder.h"
#include "AudioEngine.h"

namespace lr
{
// Builds plans for `spec` and swaps them into the engines (one per interface).
//
// A plugin must never run on two audio threads at once. When a rack moves to
// another interface (its home changes), the old interface could still run it
// for one block while the new one starts. So moved racks' plugins are first
// taken out of every plan and the old plans released, then the full plans go
// in. The moved rack passes dry for those few milliseconds.
//
// `homes` holds the previous rack -> interface map and is updated.
BuildResult deployPlans (const PlanSpec& spec,
                         std::map<juce::String, juce::String>& homes,
                         const std::map<juce::String, AudioEngine*>& engines,
                         int releaseTimeoutMs = 1000);

} // namespace lr
