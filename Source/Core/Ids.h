#pragma once

#include <juce_data_structures/juce_data_structures.h>

// ValueTree schema of a show:
//
//   SHOW version
//     RACKS
//       RACK id name channels(1|2) critical bypassed inputs sources outputs
//         SLOT id bypassed faultBypassed plugin(XML) state(base64)
//
// Channel lists are comma separated, 0-based device channel indices.
// `sources` is a comma separated list of rack ids summed into this rack (a bus).
namespace lr::ids
{
#define LR_DECLARE_ID(name) inline const juce::Identifier name { #name };

LR_DECLARE_ID (SHOW)
LR_DECLARE_ID (RACKS)
LR_DECLARE_ID (RACK)
LR_DECLARE_ID (SLOT)

LR_DECLARE_ID (version)
LR_DECLARE_ID (deviceState)
LR_DECLARE_ID (id)
LR_DECLARE_ID (name)
LR_DECLARE_ID (channels)
LR_DECLARE_ID (critical)
LR_DECLARE_ID (bypassed)
LR_DECLARE_ID (faultBypassed)
LR_DECLARE_ID (inputs)
LR_DECLARE_ID (sources)
LR_DECLARE_ID (outputs)
LR_DECLARE_ID (plugin)
LR_DECLARE_ID (state)

#undef LR_DECLARE_ID
} // namespace lr::ids
