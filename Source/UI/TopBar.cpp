#include "TopBar.h"

namespace lr
{
TopBar::TopBar (AppContext& c) : ctx (c)
{
    for (auto* b : { &racksButton, &routingButton, &settingsButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        addAndMakeVisible (*b);
    }

    racksButton.onClick    = [this] { if (racksButton.getToggleState() && onViewChanged) onViewChanged (racksView); };
    routingButton.onClick  = [this] { if (routingButton.getToggleState() && onViewChanged) onViewChanged (routingView); };
    settingsButton.onClick = [this] { if (settingsButton.getToggleState() && onViewChanged) onViewChanged (settingsView); };
    racksButton.setToggleState (true, juce::dontSendNotification);

    bypassAll.setClickingTogglesState (true);
    bypassAll.setTooltip ("Pass every rack through dry (panic). Audio keeps flowing.");
    bypassAll.setColour (juce::TextButton::buttonOnColourId, theme::fault);
    bypassAll.onClick = [this] { ctx.engine.setGlobalBypass (bypassAll.getToggleState()); };
    addAndMakeVisible (bypassAll);
}

void TopBar::setView (View v)
{
    juce::Button* b = v == racksView ? &racksButton : v == routingView ? &routingButton : &settingsButton;
    b->setToggleState (true, juce::sendNotification);
}

void TopBar::tick()
{
    juce::String newDevice, newStats;
    auto colour = theme::dim;

    if (auto* d = ctx.devices.getCurrentAudioDevice())
    {
        const double sr = d->getCurrentSampleRate();
        const int bs = d->getCurrentBufferSizeSamples();
        const int latency = d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
        const double ms = sr > 0 ? 1000.0 * latency / sr : 0.0;

        newDevice = d->getName() + "   " + juce::String (sr / 1000.0, 1) + " kHz   "
                  + juce::String (bs) + " smp   " + juce::String (ms, 1) + " ms round trip";

        const double load = ctx.engine.getCpuLoad() * 100.0;
        const int xruns = d->getXRunCount();
        newStats = "DSP " + juce::String (juce::roundToInt (load)) + "%   Dropouts " + juce::String (juce::jmax (0, xruns));
        colour = load > 80 || xruns > 0 ? theme::warn : theme::dim;
    }
    else
    {
        newDevice = "No audio device - open Settings";
        colour = theme::fault;
    }

    if (newDevice != deviceText || newStats != statsText || colour != statsColour)
    {
        deviceText = newDevice;
        statsText = newStats;
        statsColour = colour;
        repaint();
    }

    if (bypassAll.getToggleState() != ctx.engine.isGlobalBypassed())
        bypassAll.setToggleState (ctx.engine.isGlobalBypassed(), juce::dontSendNotification);
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    auto r = getLocalBounds().reduced (16, 0);
    g.setColour (theme::accent);
    g.setFont (theme::font (15.0f, true));
    g.drawText ("LIVERACK", r.removeFromLeft (96), juce::Justification::centredLeft);

    g.setColour (theme::text);
    g.setFont (theme::font (13.0f));
    g.drawText (deviceText, r.removeFromLeft (440), juce::Justification::centredLeft, true);

    g.setColour (statsColour);
    g.drawText (statsText, r.removeFromLeft (200), juce::Justification::centredLeft, true);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    bypassAll.setBounds (r.removeFromRight (110));
    r.removeFromRight (16);
    settingsButton.setBounds (r.removeFromRight (80));
    r.removeFromRight (4);
    routingButton.setBounds (r.removeFromRight (80));
    r.removeFromRight (4);
    racksButton.setBounds (r.removeFromRight (80));
}
} // namespace lr
