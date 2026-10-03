#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lr
{
// Picks a bus layout for a plugin in a rack, trying in order:
//   1. rack width (mono/stereo) on the main buses, every aux/sidechain bus off
//   2. the plugin's own main layout, aux/sidechain buses off
//   3. whatever the plugin already has
// Sidechains are always turned off when possible: nothing feeds them, and some
// plugins (e.g. FabFilter AUs) fail to initialise -> output silence otherwise.
// Call on the message thread, before prepareToPlay. Returns the step used (1-3).
int negotiateLayout (juce::AudioProcessor&, int rackChannels);

} // namespace lr
