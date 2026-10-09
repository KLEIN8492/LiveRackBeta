#include "TopBar.h"

namespace lr
{
TopBar::TopBar (AppContext& c) : ctx (c)
{
    for (auto* b : { &racksButton, &routingButton, &settingsButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        b->setColour (juce::TextButton::buttonOnColourId, theme::select.brighter (0.2f));
        b->setColour (juce::TextButton::textColourOnId, theme::text);
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
    std::vector<Field> next;
    const auto ok = theme::text;

    if (auto* d = ctx.devices.getCurrentAudioDevice())
    {
        const double sr = d->getCurrentSampleRate();
        const int bs = d->getCurrentBufferSizeSamples();
        const int rtl = d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
        const double load = ctx.engine.getCpuLoad() * 100.0;
        const int xruns = juce::jmax (0, d->getXRunCount());

        next.push_back ({ "Device", d->getName(), 230, ok });
        next.push_back ({ "SR", juce::String ((int) sr), 64, ok });
        next.push_back ({ "Buf", juce::String (bs), 50, bs > 128 ? theme::warn : ok });
        next.push_back ({ "RTL ms", juce::String (sr > 0 ? 1000.0 * rtl / sr : 0.0, 2), 66, ok });
        next.push_back ({ "DSP %", juce::String (juce::roundToInt (load)), 54, load > 80 ? theme::fault : load > 60 ? theme::warn : ok });
        next.push_back ({ "Xrun", juce::String (xruns), 54, xruns > 0 ? theme::warn : ok });
        next.push_back ({ "Boost", ctx.engine.isBoostActive() ? "ON" : "OFF", 50, ctx.engine.isBoostActive() ? theme::accent : theme::dim });
    }
    else
    {
        next.push_back ({ "Device", "NONE - open SETTINGS", 230, theme::fault });
    }

    bool changed = next.size() != fields.size();
    for (size_t i = 0; ! changed && i < next.size(); ++i)
        changed = next[i] != fields[i];

    if (changed)
    {
        fields = std::move (next);
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

    auto r = getLocalBounds().reduced (8, 6);

    g.setColour (theme::text);
    g.setFont (theme::mono (13.0f, true));
    g.drawText ("LIVERACK", r.removeFromLeft (84), juce::Justification::centredLeft);

    for (auto& f : fields)
    {
        auto cell = r.removeFromLeft (f.width);
        r.removeFromLeft (4);

        g.setColour (theme::bg);
        g.fillRect (cell);
        g.setColour (theme::line);
        g.drawRect (cell);

        auto inner = cell.reduced (5, 2);
        theme::caption (g, f.label, inner.removeFromTop (12));
        g.setColour (f.colour);
        g.setFont (theme::mono (12.0f, true));
        g.drawText (f.value, inner, juce::Justification::centredLeft, true);
    }
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (8, 7);
    bypassAll.setBounds (r.removeFromRight (104));
    r.removeFromRight (12);
    settingsButton.setBounds (r.removeFromRight (84));
    r.removeFromRight (2);
    routingButton.setBounds (r.removeFromRight (78));
    r.removeFromRight (2);
    racksButton.setBounds (r.removeFromRight (70));
}
} // namespace lr
