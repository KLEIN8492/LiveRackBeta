#include "SettingsView.h"

namespace lr
{
SettingsView::SettingsView (AppContext& c)
    : ctx (c),
      // min == max hides JUCE's channel-enable lists: LiveRack always opens every channel.
      deviceSelector (c.devices, 256, 256, 256, 256, false, false, false, false),
      pluginList (c.host.getFormatManager(), c.host.getKnownPlugins(), c.session.getScanPedalFile(), &c.settings, true)
{
    addAndMakeVisible (deviceSelector);

    pluginList.setNumberOfThreadsForScanning (4);
    addAndMakeVisible (pluginList);

    for (auto* b : { &scanVst3, &scanAu })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.25f));
        b->setTooltip ("Finds new or updated plugins. Each one is tested in a separate process, so a bad plugin can't crash LiveRack.");
        addAndMakeVisible (*b);
    }
    boostToggle.setTooltip ("Keeps one CPU core busy so the Mac never slows down between tiny audio blocks. "
                            "Without it, 32-64 sample buffers click. Uses more power: best on mains.");
    boostToggle.setToggleState (ctx.settings.getBoolValue ("lowLatencyBoost", true), juce::dontSendNotification);
    boostToggle.onClick = [this]
    {
        const bool on = boostToggle.getToggleState();
        ctx.settings.setValue ("lowLatencyBoost", on);
        ctx.engine.setLowLatencyBoost (on);
        ctx.devices.closeAudioDevice();       // takes effect when the device starts
        ctx.devices.restartLastAudioDevice();
    };
    addAndMakeVisible (boostToggle);

    scanVst3.onClick = [this] { scan ("VST3"); };
    scanAu.onClick = [this] { scan ("AudioUnit"); };
}

void SettingsView::scan (const juce::String& formatName)
{
    for (auto* format : ctx.host.getFormatManager().getFormats())
        if (format->getName() == formatName && format->canScanForPlugins())
            pluginList.scanFor (*format);
}

void SettingsView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    auto r = getLocalBounds().reduced (24, 16);
    g.setColour (theme::text);
    g.setFont (theme::font (16.0f, true));
    g.drawText ("Audio device", r.removeFromTop (24), juce::Justification::centredLeft);

    auto right = getLocalBounds().reduced (24, 16).withTrimmedLeft (getWidth() / 2);
    g.drawText ("Plugins", right.removeFromTop (24), juce::Justification::centredLeft);

    g.setColour (theme::dim);
    g.setFont (theme::font (12.5f));
    g.drawFittedText ("Pick Dante Virtual Soundcard, your AVB device or USB interface. "
                      "Lower buffer = lower latency; 64 or 32 samples is typical live. "
                      "If you hear clicks, raise it one step.",
                      getLocalBounds().reduced (24, 16).withTrimmedTop (30).withWidth (getWidth() / 2 - 48).withHeight (52),
                      juce::Justification::topLeft, 3);
}

void SettingsView::resized()
{
    auto r = getLocalBounds().reduced (24, 16);
    auto left = r.removeFromLeft (r.getWidth() / 2).withTrimmedRight (24);
    boostToggle.setBounds (left.removeFromBottom (28));
    deviceSelector.setBounds (left.withTrimmedTop (86).withTrimmedBottom (8));

    r.removeFromTop (30);
    auto buttons = r.removeFromTop (32);
    scanVst3.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    scanAu.setBounds (buttons.removeFromLeft (150));
    r.removeFromTop (10);
    pluginList.setBounds (r);
}
} // namespace lr
