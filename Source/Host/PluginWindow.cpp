#include "PluginWindow.h"

namespace lr
{
PluginWindow::PluginWindow (juce::AudioProcessor& processor, const juce::String& title, std::function<void()> onCloseRequested)
    : juce::DocumentWindow (title, juce::Colour (0xff1e2126), juce::DocumentWindow::closeButton),
      onClose (std::move (onCloseRequested))
{
    setUsingNativeTitleBar (true);

    juce::AudioProcessorEditor* editor = processor.hasEditor() ? processor.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (processor);

    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);

    const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
    setTopLeftPosition (area.getCentreX() - getWidth() / 2 + juce::Random::getSystemRandom().nextInt (80),
                        area.getY() + 80 + juce::Random::getSystemRandom().nextInt (60));
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    // Editor must go before the plugin; clearing content deletes it now.
    clearContentComponent();
}

void PluginWindow::closeButtonPressed()
{
    if (onClose)
        onClose();
}

} // namespace lr
