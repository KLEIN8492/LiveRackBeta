#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace lr
{
// A floating window showing a plugin's own editor (or a generic one).
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor&, const juce::String& title, std::function<void()> onCloseRequested);
    ~PluginWindow() override;

    void closeButtonPressed() override;

private:
    std::function<void()> onClose;
};

} // namespace lr
