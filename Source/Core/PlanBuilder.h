#pragma once

#include "RenderPlan.h"

namespace lr
{
// Turns a PlanSpec into a ready-to-run RenderPlan: resolves bus sources,
// groups racks into dependency levels, drops channels the device doesn't
// have, and preallocates every buffer. Runs on the message thread.
BuildResult buildPlan (const PlanSpec&);

} // namespace lr
