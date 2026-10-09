#include "RackList.h"
#include <set>

namespace lr
{
RackList::RackList (AppContext& c) : ctx (c)
{
    list.setRowHeight (44);
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

    // Default routing: the next free input(s) on an interface already in use
    // (or the first one with inputs), straight to the same-numbered outputs.
    auto& hub = ctx.devices;
    const DeviceHub::Info* iface = nullptr;
    for (auto& o : hub.getOpen())
        if ((iface = hub.findAvailable (o->name)) != nullptr && ! iface->inputs.isEmpty())
            break;
    if (iface == nullptr || iface->inputs.isEmpty())
        for (auto& info : hub.getAvailable())
            if (! info.inputs.isEmpty()) { iface = &info; break; }

    if (iface != nullptr)
    {
        std::set<int> used;
        for (auto r : show::getRacks (show))
            for (auto& p : show::getPorts (r, ids::inputs))
                if (p.device == iface->name)
                    used.insert (p.channel);

        std::vector<PortRef> ins, outs;
        for (int ch = 0; ch < iface->inputs.size() && (int) ins.size() < channels; ++ch)
            if (used.count (ch) == 0)
                ins.push_back ({ iface->name, ch });
        for (auto& p : ins)
            if (p.channel < iface->outputs.size())
                outs.push_back (p);

        show::setPorts (rack, ids::inputs, ins);
        show::setPorts (rack, ids::outputs, outs);
    }

    refresh();
    selectRack (rack[ids::id].toString());
}

void RackList::refresh()
{
    list.updateContent();

    auto show = ctx.host.getShow();
    auto racks = show::getRacks (show);

    routingText.clear();
    for (auto rack : racks)
        routingText[rack[ids::id].toString()] = juce::String ((int) rack[ids::channels] == 2 ? "ST " : "MO ")
                                              + show::describeRouting (rack, show);

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

// 30 Hz. Meters decay here (not in paint), so a row is only redrawn when a
// lit segment or its status light actually changes: an idle show costs
// almost nothing, and a busy one repaints meter strips, not whole rows.
void RackList::tick()
{
    auto racks = show::getRacks (ctx.host.getShow());
    const int meterWidth = meterArea (list.getVisibleRowWidth(), list.getRowHeight()).getWidth();

    for (int row = 0; row < racks.getNumChildren(); ++row)
    {
        auto rack = racks.getChild (row);
        const auto id = rack[ids::id].toString();
        auto& m = meters[id];

        if (auto rt = ctx.host.getRack (id))
        {
            m.in.update (rt->inPeak);
            m.out.update (rt->outPeak);
        }

        const int inLit = theme::meterSegmentsLit (m.in.shown, meterWidth);
        const int outLit = theme::meterSegmentsLit (m.out.shown, meterWidth);
        const auto status = statusColour (rack);

        if (status != m.status)
        {
            list.repaintRow (row);
        }
        else if (inLit != m.inLit || outLit != m.outLit)
        {
            const auto bounds = list.getRowPosition (row, true);
            if (! bounds.isEmpty())
                list.repaint (meterArea (bounds.getWidth(), bounds.getHeight()) + bounds.getPosition());
        }

        m.status = status;
        m.inLit = inLit;
        m.outLit = outLit;
    }
}

// Red = dry fallback, amber = a plugin faulted, green = fine, grey = bypassed.
juce::Colour RackList::statusColour (const juce::ValueTree& rack) const
{
    if (auto rt = ctx.host.getRack (rack[ids::id].toString()); rt != nullptr && rt->dryFallback.load())
        return theme::fault;

    for (auto slot : rack)
        if (auto s = ctx.host.getSlot (slot[ids::id].toString()); s != nullptr && s->fault.load() != FaultReason::none)
            return theme::warn;

    return (bool) rack[ids::bypassed] ? theme::dim : theme::accent;
}

juce::Rectangle<int> RackList::meterArea (int rowWidth, int rowHeight)
{
    return juce::Rectangle<int> (0, 0, rowWidth, rowHeight).reduced (10, 5).removeFromBottom (14).removeFromRight (96);
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

    g.setColour (selected ? theme::select : (row % 2 == 0 ? theme::panel : theme::stripe));
    g.fillRect (0, 0, width, height);
    g.setColour (theme::line);
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
    if (selected)
    {
        g.setColour (theme::text);
        g.fillRect (0, 0, 3, height);
    }

    const auto status = statusColour (rack);

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 5);
    auto top = r.removeFromTop (18);

    theme::led (g, top.removeFromLeft (10).withSizeKeepingCentre (9, 9), status);
    top.removeFromLeft (6);

    g.setColour (theme::dim);
    g.setFont (theme::mono (11.0f));
    g.drawText (juce::String (row + 1).paddedLeft ('0', 2), top.removeFromLeft (20), juce::Justification::centredLeft);

    if ((bool) rack[ids::critical])
    {
        auto tag = top.removeFromRight (36).withSizeKeepingCentre (36, 14);
        g.setColour (theme::critical);
        g.drawRect (tag);
        g.setFont (theme::font (9.5f, true));
        g.drawText ("CRIT", tag, juce::Justification::centred);
        top.removeFromRight (4);
    }

    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText (rack[ids::name].toString(), top, juce::Justification::centredLeft, true);

    auto bottom = r.removeFromBottom (14);
    const auto meterRect = meterArea (width, height);
    bottom.removeFromRight (meterRect.getWidth());
    g.setColour (theme::dim);
    g.setFont (theme::mono (10.5f));
    if (auto it = routingText.find (id); it != routingText.end())
        g.drawText (it->second, bottom.withTrimmedLeft (36), juce::Justification::centredLeft, true);

    if (rt != nullptr)
    {
        auto& m = meters[id];
        auto bars = meterRect.withSizeKeepingCentre (meterRect.getWidth(), 10);
        theme::drawMeter (g, bars.removeFromTop (4), m.in.shown);
        bars.removeFromTop (2);
        theme::drawMeter (g, bars.removeFromTop (4), m.out.shown);
    }
}

void RackList::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    auto header = getLocalBounds().removeFromTop (24).withTrimmedRight (1);
    g.setColour (theme::raised);
    g.fillRect (header);
    g.setColour (theme::line);
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) header.getRight());
    theme::caption (g, "Racks", header.reduced (10, 0));
    theme::caption (g, "In / Out", header.reduced (10, 0), juce::Justification::centredRight);

    if (getNumRows() == 0)
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (12.0f));
        g.drawFittedText ("No racks.\nAdd one per channel to process.",
                          getLocalBounds().withTrimmedTop (24).withTrimmedBottom (44).reduced (16), juce::Justification::centred, 3);
    }
}

void RackList::resized()
{
    auto r = getLocalBounds().withTrimmedRight (1);
    r.removeFromTop (24);
    auto buttons = r.removeFromBottom (40).reduced (8, 7);
    addMono.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
    buttons.removeFromLeft (4);
    addStereo.setBounds (buttons);
    list.setBounds (r);
}
} // namespace lr
