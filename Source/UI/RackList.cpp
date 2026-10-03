#include "RackList.h"

namespace lr
{
RackList::RackList (AppContext& c) : ctx (c)
{
    list.setRowHeight (58);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    addMono.onClick = [this] { addRack (1); };
    addStereo.onClick = [this] { addRack (2); };
    addAndMakeVisible (addMono);
    addAndMakeVisible (addStereo);
}

void RackList::addRack (int channels)
{
    auto show = ctx.host.getShow();
    const int n = show::getRacks (show).getNumChildren() + 1;
    auto rack = show::addRack (show, (channels == 1 ? "Channel " : "Stereo ") + juce::String (n), channels);

    // Sensible default routing: next free input(s) straight to the same output(s).
    const int numIn = ctx.inputNames().size(), numOut = ctx.outputNames().size();
    std::vector<int> used;
    for (auto r : show::getRacks (show))
        for (int ch : show::getChannels (r, ids::inputs))
            used.push_back (ch);

    std::vector<int> chosen;
    for (int ch = 0; ch < numIn && (int) chosen.size() < channels; ++ch)
        if (std::find (used.begin(), used.end(), ch) == used.end())
            chosen.push_back (ch);

    show::setChannels (rack, ids::inputs, chosen);

    std::vector<int> outs;
    for (int ch : chosen)
        if (ch < numOut)
            outs.push_back (ch);
    show::setChannels (rack, ids::outputs, outs);

    refresh();
    selectRack (rack[ids::id].toString());
}

void RackList::refresh()
{
    list.updateContent();

    auto racks = show::getRacks (ctx.host.getShow());
    if (! racks.getChildWithProperty (ids::id, selectedId).isValid())
        selectRack (racks.getNumChildren() > 0 ? racks.getChild (0)[ids::id].toString() : juce::String());

    list.repaint();
}

void RackList::selectRack (const juce::String& rackId)
{
    auto racks = show::getRacks (ctx.host.getShow());
    const int row = racks.indexOf (racks.getChildWithProperty (ids::id, rackId));

    if (row >= 0)
        list.selectRow (row);
    else
    {
        list.deselectAllRows();
        selectedId = {};
        if (onSelectionChanged)
            onSelectionChanged (selectedId);
    }
}

void RackList::tick()
{
    list.repaint();
}

int RackList::getNumRows()
{
    return show::getRacks (ctx.host.getShow()).getNumChildren();
}

void RackList::selectedRowsChanged (int row)
{
    auto rack = show::getRacks (ctx.host.getShow()).getChild (row);
    const auto id = rack.isValid() ? rack[ids::id].toString() : juce::String();

    if (id != selectedId)
    {
        selectedId = id;
        if (onSelectionChanged)
            onSelectionChanged (selectedId);
    }
}

void RackList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    auto show = ctx.host.getShow();
    auto rack = show::getRacks (show).getChild (row);
    if (! rack.isValid())
        return;

    const auto id = rack[ids::id].toString();
    auto rt = ctx.host.getRack (id);

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (8, 3);
    g.setColour (selected ? theme::raised : theme::panel);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    if (selected)
    {
        g.setColour (theme::accent);
        g.fillRoundedRectangle (r.removeFromLeft (3).toFloat(), 1.5f);
    }
    else
    {
        r.removeFromLeft (3);
    }

    r.reduce (10, 6);

    // Status: red = dry fallback, amber = a plugin faulted, green = fine, grey = bypassed.
    bool slotFault = false;
    for (auto slot : rack)
        if (auto s = ctx.host.getSlot (slot[ids::id].toString()))
            slotFault |= s->fault.load() != FaultReason::none;

    juce::Colour status = theme::accent;
    if ((bool) rack[ids::bypassed])                    status = theme::dim;
    if (slotFault)                                     status = theme::warn;
    if (rt != nullptr && rt->dryFallback.load())       status = theme::fault;

    auto top = r.removeFromTop (20);
    g.setColour (status);
    g.fillEllipse (top.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat());
    top.removeFromLeft (6);

    if ((bool) rack[ids::critical])
    {
        auto badge = top.removeFromRight (58).withSizeKeepingCentre (58, 16);
        g.setColour (theme::critical.withAlpha (0.2f));
        g.fillRoundedRectangle (badge.toFloat(), 3.0f);
        g.setColour (theme::critical);
        g.setFont (theme::font (10.0f, true));
        g.drawText ("CRITICAL", badge, juce::Justification::centred);
    }

    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, true));
    g.drawText (rack[ids::name].toString(), top, juce::Justification::centredLeft, true);

    auto mid = r.removeFromTop (16);
    g.setColour (theme::dim);
    g.setFont (theme::font (12.0f));
    g.drawText ((int) rack[ids::channels] == 2 ? "ST  " + show::describeRouting (rack, show) : show::describeRouting (rack, show),
                mid, juce::Justification::centredLeft, true);

    if (rt != nullptr)
    {
        auto& m = meters[id];
        auto bars = r.removeFromTop (8).withTrimmedTop (2);
        const int half = bars.getWidth() / 2 - 3;
        theme::drawMeter (g, bars.removeFromLeft (half).toFloat(), m.in.update (rt->inPeak));
        bars.removeFromLeft (6);
        theme::drawMeter (g, bars.toFloat(), m.out.update (rt->outPeak));
    }
}

void RackList::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    if (getNumRows() == 0)
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (13.0f));
        g.drawFittedText ("No racks yet.\nAdd one below for each\nchannel you want to process.",
                          getLocalBounds().withTrimmedBottom (60).reduced (20), juce::Justification::centred, 4);
    }
}

void RackList::resized()
{
    auto r = getLocalBounds();
    auto buttons = r.removeFromBottom (52).reduced (10, 10);
    addMono.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
    buttons.removeFromLeft (8);
    addStereo.setBounds (buttons);
    list.setBounds (r.withTrimmedRight (1).withTrimmedTop (6));
}
} // namespace lr
