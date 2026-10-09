#include "TopBar.h"

namespace lr
{
TopBar::TopBar (AppContext& c) : ctx (c)
{
    settingsButton.setTooltip ("Audio interfaces, routing, plugins, performance");
    settingsButton.onClick = [this] { if (ctx.openSettings) ctx.openSettings(); };
    addAndMakeVisible (settingsButton);

    bypassAll.setClickingTogglesState (true);
    bypassAll.setTooltip ("Pass every rack through dry (panic). Audio keeps flowing.");
    bypassAll.setColour (juce::TextButton::buttonOnColourId, theme::fault);
    bypassAll.onClick = [this] { ctx.devices.setGlobalBypass (bypassAll.getToggleState()); };
    addAndMakeVisible (bypassAll);
}

void TopBar::tick()
{
    std::vector<Field> next;
    const auto ok = theme::text;
    auto& hub = ctx.devices;
    const auto& open = hub.getOpen();

    if (! open.empty())
    {
        juce::String names = open.front()->name;
        if (open.size() > 1)
            names << "  +" << (int) open.size() - 1;

        double worstRtl = 0.0, worstLoad = 0.0;
        int xruns = 0, block = 0;
        for (auto& o : open)
        {
            auto* d = o->device.get();
            const double sr = d->getCurrentSampleRate();
            const int rtl = d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
            worstRtl = juce::jmax (worstRtl, sr > 0 ? 1000.0 * rtl / sr : 0.0);
            worstLoad = juce::jmax (worstLoad, o->engine->getCpuLoad() * 100.0);
            xruns += juce::jmax (0, d->getXRunCount());
            block = juce::jmax (block, d->getCurrentBufferSizeSamples());
        }

        bool missing = false;
        for (auto& name : hub.getWanted())
            missing |= hub.deviceFor (name) == nullptr;

        next.push_back ({ "Interfaces", names, 230, missing ? theme::warn : ok });
        next.push_back ({ "SR", juce::String ((int) hub.getSampleRate()), 64, ok });
        next.push_back ({ "Buf", juce::String (block), 50, block > 128 ? theme::warn : ok });
        next.push_back ({ "RTL ms", juce::String (worstRtl, 2), 66, ok });
        next.push_back ({ "DSP %", juce::String (juce::roundToInt (worstLoad)), 54, worstLoad > 80 ? theme::fault : worstLoad > 60 ? theme::warn : ok });
        next.push_back ({ "Xrun", juce::String (xruns), 54, xruns > 0 ? theme::warn : ok });
        next.push_back ({ "Boost", hub.isBoostActive() ? "ON" : "OFF", 50, hub.isBoostActive() ? theme::accent : theme::dim });
    }
    else
    {
        next.push_back ({ "Interfaces", "NONE - pick a rack input", 230, theme::warn });
    }

    bool changed = next.size() != fields.size();
    for (size_t i = 0; ! changed && i < next.size(); ++i)
        changed = next[i] != fields[i];

    if (changed)
    {
        fields = std::move (next);
        repaint();
    }

    if (bypassAll.getToggleState() != hub.isGlobalBypassed())
        bypassAll.setToggleState (hub.isGlobalBypassed(), juce::dontSendNotification);
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
    r.removeFromRight (8);
    settingsButton.setBounds (r.removeFromRight (120));
}
} // namespace lr
