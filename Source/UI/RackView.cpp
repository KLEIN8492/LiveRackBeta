#include "RackView.h"
#include "PluginBrowser.h"

namespace lr
{
// ---------------------------------------------------------------------------
class RackView::SlotRow final : public juce::Component
{
public:
    SlotRow (AppContext& c, juce::ValueTree s) : ctx (c), slot (std::move (s))
    {
        for (auto* b : { &editButton, &bypassButton, &rearmButton, &upButton, &downButton, &removeButton })
            addAndMakeVisible (*b);

        bypassButton.setClickingTogglesState (true);
        bypassButton.setColour (juce::TextButton::buttonOnColourId, theme::warn);
        bypassButton.setToggleState ((bool) slot[ids::bypassed], juce::dontSendNotification);
        bypassButton.onClick = [this] { slot.setProperty (ids::bypassed, bypassButton.getToggleState(), nullptr); };

        editButton.onClick = [this] { ctx.host.openEditor (slotId()); };
        rearmButton.setColour (juce::TextButton::buttonColourId, theme::fault);
        rearmButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
        rearmButton.setTooltip ("Re-arm: turn this plugin back on after a fault");
        editButton.setTooltip ("Open the plugin window");
        bypassButton.setTooltip ("Bypass this plugin");
        rearmButton.onClick = [this] { ctx.host.rearmSlot (slotId()); };

        upButton.onClick = [this] { move (-1); };
        downButton.onClick = [this] { move (+1); };
        removeButton.setTooltip ("Remove plugin");
        removeButton.onClick = [this]
        {
            auto parent = slot.getParent();
            parent.removeChild (slot, nullptr);
        };

        update();
    }

    juce::String slotId() const { return slot[ids::id].toString(); }

    void move (int delta)
    {
        auto parent = slot.getParent();
        const int i = parent.indexOf (slot), j = i + delta;
        if (j >= 0 && j < parent.getNumChildren())
            parent.moveChild (i, j, nullptr);
    }

    void update()
    {
        auto rt = ctx.host.getSlot (slotId());
        const auto error = ctx.host.getSlotError (slotId());

        juce::PluginDescription desc;
        show::getPluginDescription (slot, desc);

        Cells next;
        next.name = desc.name;
        next.maker = desc.manufacturerName + " / " + desc.pluginFormatName;
        next.colour = theme::accent;
        bool faulted = false;

        if (error.isNotEmpty())
        {
            next.status = "ERROR: " + error;
            next.colour = theme::fault;
        }
        else if (rt == nullptr || rt->plugin == nullptr)
        {
            next.status = "LOADING";
            next.colour = theme::dim;
        }
        else
        {
            const auto fault = rt->fault.load();
            const double sr = ctx.host.getSampleRate();
            next.latency = juce::String (1000.0 * ctx.host.getSlotLatencySamples (slotId()) / sr, 2);
            next.cpu = juce::String (rt->processMs.load(), 3);

            if (fault != FaultReason::none)
            {
                next.status = juce::String ("BYPASSED: ") + juce::String (describe (fault)).toUpperCase();
                next.colour = theme::fault;
                faulted = true;
            }
            else if (rt->silentOutput.load())
            {
                next.status = "NO OUTPUT";
                next.colour = theme::warn;
            }
            else if (rt->userBypassed.load())
            {
                next.status = "BYPASS";
                next.colour = theme::warn;
            }
            else
            {
                next.status = "OK";
            }
        }

        rearmButton.setVisible (faulted);
        editButton.setEnabled (rt != nullptr && rt->plugin != nullptr);

        if (next != cells)
        {
            cells = next;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const int index = slot.getParent().indexOf (slot);
        g.setColour (index % 2 == 0 ? theme::panel : theme::stripe);
        g.fillAll();
        g.setColour (theme::line);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        auto cols = Columns (getLocalBounds());
        theme::led (g, cols.index.removeFromLeft (8).withSizeKeepingCentre (8, 8), cells.colour);

        g.setFont (theme::mono (11.0f));
        g.setColour (theme::dim);
        g.drawText (juce::String (index + 1).paddedLeft ('0', 2), cols.index.withTrimmedLeft (6), juce::Justification::centredLeft);

        g.setColour (theme::text);
        g.setFont (theme::font (12.5f, true));
        g.drawText (cells.name, cols.name, juce::Justification::centredLeft, true);

        g.setColour (theme::dim);
        g.setFont (theme::font (11.5f));
        g.drawText (cells.maker, cols.maker, juce::Justification::centredLeft, true);

        g.setColour (theme::text);
        g.setFont (theme::mono (11.5f));
        g.drawText (cells.latency, cols.latency, juce::Justification::centredRight);
        g.drawText (cells.cpu, cols.cpu, juce::Justification::centredRight);

        g.setColour (cells.colour);
        g.setFont (theme::font (11.0f, true));
        g.drawText (cells.status, cols.status, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = Columns (getLocalBounds()).buttons.reduced (0, 4);
        removeButton.setBounds (r.removeFromRight (24));
        r.removeFromRight (2);
        downButton.setBounds (r.removeFromRight (24));
        r.removeFromRight (2);
        upButton.setBounds (r.removeFromRight (24));
        r.removeFromRight (6);
        bypassButton.setBounds (r.removeFromRight (42));
        r.removeFromRight (2);
        editButton.setBounds (r.removeFromRight (42));
        r.removeFromRight (2);
        rearmButton.setBounds (r.removeFromRight (40));
    }

    // Column layout shared by the rows and the table header.
    struct Columns
    {
        juce::Rectangle<int> index, name, maker, latency, cpu, status, buttons;

        explicit Columns (juce::Rectangle<int> r)
        {
            r = r.reduced (8, 0);
            buttons = r.removeFromRight (214);
            r.removeFromRight (8);
            index   = r.removeFromLeft (34);
            status  = r.removeFromRight (118);
            r.removeFromRight (8);
            cpu     = r.removeFromRight (52);
            r.removeFromRight (4);
            latency = r.removeFromRight (52);
            r.removeFromRight (12);
            maker   = r.removeFromRight (juce::jmin (170, r.getWidth() * 2 / 5));
            r.removeFromRight (8);
            name    = r;
        }
    };

private:
    struct Cells
    {
        juce::String name, maker, latency { "-" }, cpu { "-" }, status;
        juce::Colour colour;
        bool operator!= (const Cells& o) const
        {
            return name != o.name || maker != o.maker || latency != o.latency || cpu != o.cpu
                || status != o.status || colour != o.colour;
        }
    };

    AppContext& ctx;
    juce::ValueTree slot;
    Cells cells;

    juce::TextButton editButton { "EDIT" }, bypassButton { "BYP" }, rearmButton { "ARM" };
    juce::TextButton upButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")) };
    juce::TextButton downButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc")) };
    juce::TextButton removeButton { "X" };
};

// ---------------------------------------------------------------------------
RackView::RackView (AppContext& c) : ctx (c)
{
    nameLabel.setEditable (true);
    nameLabel.setFont (theme::font (15.0f, true));
    nameLabel.setColour (juce::Label::outlineColourId, theme::line);
    nameLabel.setColour (juce::Label::backgroundColourId, theme::bg);
    nameLabel.setColour (juce::Label::textWhenEditingColourId, theme::text);
    nameLabel.onTextChange = [this]
    {
        if (rack.isValid() && nameLabel.getText().trim().isNotEmpty())
            rack.setProperty (ids::name, nameLabel.getText().trim(), nullptr);
    };
    addAndMakeVisible (nameLabel);

    widthBox.addItem ("MONO", 1);
    widthBox.addItem ("STEREO", 2);
    widthBox.onChange = [this]
    {
        if (rack.isValid())
            rack.setProperty (ids::channels, widthBox.getSelectedId(), nullptr);
    };
    addAndMakeVisible (widthBox);

    criticalToggle.setTooltip ("If any plugin misbehaves, this rack falls back to its clean input instead of dropping out.");
    criticalToggle.onClick = [this] { if (rack.isValid()) rack.setProperty (ids::critical, criticalToggle.getToggleState(), nullptr); };
    bypassToggle.onClick = [this] { if (rack.isValid()) rack.setProperty (ids::bypassed, bypassToggle.getToggleState(), nullptr); };
    addAndMakeVisible (criticalToggle);
    addAndMakeVisible (bypassToggle);

    inputButton.onClick = [this] { showInputMenu(); };
    outputButton.onClick = [this] { showOutputMenu(); };
    addAndMakeVisible (inputButton);
    addAndMakeVisible (outputButton);

    deleteButton.onClick = [this] { deleteRack(); };
    addAndMakeVisible (deleteButton);

    restoreButton.setColour (juce::TextButton::buttonColourId, theme::fault);
    restoreButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    restoreButton.onClick = [this] { if (rack.isValid()) ctx.host.restoreRack (rack[ids::id].toString()); };
    addChildComponent (restoreButton);

    addPluginButton.onClick = [this] { showPluginBrowser(); };
    addAndMakeVisible (addPluginButton);

    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

RackView::~RackView()
{
    rack.removeListener (this);
}

void RackView::setRack (const juce::String& rackId)
{
    rack.removeListener (this);
    rack = show::findRack (ctx.host.getShow(), rackId);
    rack.addListener (this);
    refresh();
}

void RackView::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier&)
{
    if (node == rack)
        refresh();
}

void RackView::refresh()
{
    const bool valid = rack.isValid();
    for (auto* c : getChildren())
        c->setVisible (valid);

    if (! valid)
    {
        repaint();
        return;
    }

    nameLabel.setText (rack[ids::name].toString(), juce::dontSendNotification);
    widthBox.setSelectedId ((int) rack[ids::channels], juce::dontSendNotification);
    criticalToggle.setToggleState ((bool) rack[ids::critical], juce::dontSendNotification);
    bypassToggle.setToggleState ((bool) rack[ids::bypassed], juce::dontSendNotification);

    auto show = ctx.host.getShow();
    auto routing = show::describeRouting (rack, show);
    const auto arrow = routing.indexOf (juce::CharPointer_UTF8 ("\xe2\x86\x92"));
    inputButton.setButtonText (routing.substring (0, arrow).trim().toUpperCase());
    outputButton.setButtonText (routing.substring (arrow + 1).trim().toUpperCase());

    rebuildRows();
    tick();
}

void RackView::rebuildRows()
{
    rows.clear();
    if (rack.isValid())
    {
        for (auto slot : rack)
            rowHolder.addAndMakeVisible (rows.add (new SlotRow (ctx, slot)));
    }
    resized();
}

void RackView::tick()
{
    if (! rack.isValid())
        return;

    for (auto* row : rows)
        row->update();

    auto rt = ctx.host.getRack (rack[ids::id].toString());
    const bool dry = rt != nullptr && rt->dryFallback.load();

    juce::String text;
    juce::Colour colour = theme::dim;

    if (dry)
    {
        text = "DRY FALLBACK  -  plugin fault, rack is passing its clean input";
        colour = theme::fault;
    }
    else if ((bool) rack[ids::bypassed] || ctx.engine.isGlobalBypassed())
    {
        text = "BYPASSED  -  passing input through";
        colour = theme::warn;
    }
    else if (ctx.host.getRoutingError().isNotEmpty())
    {
        text = ctx.host.getRoutingError();
        colour = theme::fault;
    }
    else if (rt != nullptr)
    {
        text = "RUNNING   rack cpu " + juce::String (rt->processMs.load(), 3) + " ms";
        colour = theme::accent;
    }

    if (restoreButton.isVisible() != dry)
        restoreButton.setVisible (dry);

    if (text != statusText || colour != statusColour)
    {
        statusText = text;
        statusColour = colour;
        repaint();
    }
}

void RackView::showInputMenu()
{
    auto show = ctx.host.getShow();
    const auto names = ctx.inputNames();
    auto inputs = show::getChannels (rack, ids::inputs);
    auto sources = show::parseIds (rack[ids::sources].toString());

    juce::PopupMenu menu;
    menu.addSectionHeader ("Device inputs");
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (1 + i, juce::String (i + 1) + "   " + names[i], true,
                      std::find (inputs.begin(), inputs.end(), i) != inputs.end());

    juce::PopupMenu busMenu;
    int k = 0;
    for (auto other : show::getRacks (show))
    {
        ++k;
        if (other == rack)
            continue;
        busMenu.addItem (10000 + k, other[ids::name].toString(), true, sources.contains (other[ids::id].toString()));
    }
    menu.addSeparator();
    menu.addSubMenu ("Bus: sum of other racks", busMenu);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (inputButton),
        [this, inputs, sources, show] (int result) mutable
        {
            if (result <= 0 || ! rack.isValid())
                return;

            if (result >= 10000)
            {
                const auto id = show::getRacks (show).getChild (result - 10001)[ids::id].toString();
                if (sources.contains (id)) sources.removeString (id);
                else                       sources.add (id);
                rack.setProperty (ids::sources, sources.joinIntoString (","), nullptr);
                show::setChannels (rack, ids::inputs, {});
            }
            else
            {
                const int ch = result - 1;
                auto it = std::find (inputs.begin(), inputs.end(), ch);
                if (it != inputs.end()) inputs.erase (it);
                else                    inputs.push_back (ch);
                std::sort (inputs.begin(), inputs.end());
                show::setChannels (rack, ids::inputs, inputs);
                rack.setProperty (ids::sources, "", nullptr);
            }
            juce::MessageManager::callAsync ([this] { showInputMenu(); }); // keep the menu up for multi-select
        });
}

void RackView::showOutputMenu()
{
    const auto names = ctx.outputNames();
    auto outputs = show::getChannels (rack, ids::outputs);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Device outputs");
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (1 + i, juce::String (i + 1) + "   " + names[i], true,
                      std::find (outputs.begin(), outputs.end(), i) != outputs.end());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (outputButton),
        [this, outputs] (int result) mutable
        {
            if (result <= 0 || ! rack.isValid())
                return;

            const int ch = result - 1;
            auto it = std::find (outputs.begin(), outputs.end(), ch);
            if (it != outputs.end()) outputs.erase (it);
            else                     outputs.push_back (ch);
            std::sort (outputs.begin(), outputs.end());
            show::setChannels (rack, ids::outputs, outputs);
            juce::MessageManager::callAsync ([this] { showOutputMenu(); });
        });
}

void RackView::showPluginBrowser()
{
    auto browser = std::make_unique<PluginBrowser> (ctx.host.getKnownPlugins(),
        [this] (const juce::PluginDescription& desc)
        {
            if (rack.isValid())
                show::addSlot (rack, desc);
        });

    auto& box = juce::CallOutBox::launchAsynchronously (std::move (browser), addPluginButton.getScreenBounds(), nullptr);
    juce::ignoreUnused (box);
}

void RackView::deleteRack()
{
    if (! rack.isValid())
        return;

    const auto name = rack[ids::name].toString();
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon,
                                                                 "Delete \"" + name + "\"?",
                                                                 "Its plugins and their settings will be removed.",
                                                                 "Delete", "Cancel", this);

    juce::AlertWindow::showAsync (options, [this, target = rack] (int result)
    {
        if (result != 1)
            return;

        auto show = ctx.host.getShow();
        const auto id = target[ids::id].toString();

        // Remove this rack from any bus that uses it.
        for (auto other : show::getRacks (show))
        {
            auto srcs = show::parseIds (other[ids::sources].toString());
            if (srcs.contains (id))
            {
                srcs.removeString (id);
                other.setProperty (ids::sources, srcs.joinIntoString (","), nullptr);
            }
        }

        show::getRacks (show).removeChild (target, nullptr);
    });
}

void RackView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    if (! rack.isValid())
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (13.0f));
        g.drawText ("NO RACK SELECTED", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Header panel.
    g.setColour (theme::panel);
    g.fillRect (headerArea);
    g.setColour (theme::line);
    g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());

    for (auto& [label, area] : captions)
        theme::caption (g, label, area);

    // Status box.
    g.setColour (theme::bg);
    g.fillRect (statusArea);
    g.setColour (theme::line);
    g.drawRect (statusArea);
    theme::led (g, statusArea.reduced (8, 0).removeFromLeft (9).withSizeKeepingCentre (9, 9), statusColour);
    g.setColour (statusColour == theme::accent ? theme::text : statusColour);
    g.setFont (theme::mono (11.5f, statusColour == theme::fault));
    g.drawText (statusText, statusArea.reduced (8, 0).withTrimmedLeft (17), juce::Justification::centredLeft, true);

    // Table header.
    g.setColour (theme::raised);
    g.fillRect (tableHeader);
    g.setColour (theme::line);
    g.drawRect (tableHeader);
    SlotRow::Columns cols (tableHeader.withWidth (viewport.getWidth() - viewport.getScrollBarThickness()));
    theme::caption (g, "#", cols.index);
    theme::caption (g, "Plugin", cols.name);
    theme::caption (g, "Maker / Format", cols.maker);
    theme::caption (g, "Lat ms", cols.latency, juce::Justification::centredRight);
    theme::caption (g, "Cpu ms", cols.cpu, juce::Justification::centredRight);
    theme::caption (g, "Status", cols.status);

    if (rows.isEmpty())
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (12.0f));
        g.drawText ("EMPTY CHAIN  -  use + ADD PLUGIN", viewport.getBounds().withHeight (60), juce::Justification::centred);
    }

    // Footer.
    g.setColour (theme::panel);
    g.fillRect (footerArea);
    g.setColour (theme::line);
    g.drawHorizontalLine (footerArea.getY(), 0.0f, (float) getWidth());
}

void RackView::resized()
{
    captions.clear();
    auto r = getLocalBounds();

    headerArea = r.removeFromTop (92);
    auto h = headerArea.reduced (12, 8);

    // Row 1: NAME | WIDTH | flags | delete
    auto row1 = h.removeFromTop (38);
    auto labelled = [this] (juce::Rectangle<int>& row, const char* label, int width, juce::Component& c)
    {
        auto cell = row.removeFromLeft (width);
        captions.push_back ({ label, cell.removeFromTop (12) });
        c.setBounds (cell.withTrimmedTop (2));
        row.removeFromLeft (8);
    };

    deleteButton.setBounds (row1.removeFromRight (96).withTrimmedTop (14));
    row1.removeFromRight (12);
    bypassToggle.setBounds (row1.removeFromRight (78).withTrimmedTop (14));
    criticalToggle.setBounds (row1.removeFromRight (84).withTrimmedTop (14));
    row1.removeFromRight (8);
    labelled (row1, "Name", juce::jmin (320, row1.getWidth() - 108), nameLabel);
    labelled (row1, "Width", 92, widthBox);

    // Row 2: IN | OUT | status | restore
    h.removeFromTop (6);
    auto row2 = h.removeFromTop (34);
    labelled (row2, "Input", 220, inputButton);
    labelled (row2, "Output", 160, outputButton);
    restoreButton.setBounds (row2.removeFromRight (150).withTrimmedTop (14));
    row2.removeFromRight (8);
    statusArea = row2.withTrimmedTop (14);

    footerArea = r.removeFromBottom (40);
    addPluginButton.setBounds (footerArea.reduced (12, 7).removeFromLeft (130));

    r = r.reduced (12, 10);
    tableHeader = r.removeFromTop (22);
    viewport.setBounds (r);

    const int rowHeight = 30;
    const int width = viewport.getWidth() - viewport.getScrollBarThickness();
    rowHolder.setSize (width, juce::jmax (1, rows.size() * rowHeight));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, width, rowHeight);
}
} // namespace lr
