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
        rearmButton.setColour (juce::TextButton::buttonColourId, theme::fault.withAlpha (0.35f));
        rearmButton.setTooltip ("Turn this plugin back on after a fault");
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

        juce::String newName = desc.name;
        juce::String newDetail = desc.manufacturerName + "  " + desc.pluginFormatName;
        juce::String newStatus;
        juce::Colour colour = theme::accent;
        bool faulted = false;

        if (error.isNotEmpty())
        {
            newStatus = error;
            colour = theme::fault;
        }
        else if (rt == nullptr || rt->plugin == nullptr)
        {
            newStatus = "Loading...";
            colour = theme::dim;
        }
        else
        {
            const auto fault = rt->fault.load();
            const double sr = ctx.host.getSampleRate();
            const int latency = ctx.host.getSlotLatencySamples (slotId());
            newDetail += "   latency " + juce::String (1000.0 * latency / sr, 2) + " ms"
                       + "   cpu " + juce::String (rt->processMs.load(), 2) + " ms";

            if (fault != FaultReason::none)
            {
                newStatus = juce::String ("BYPASSED: ") + describe (fault);
                colour = theme::fault;
                faulted = true;
            }
            else if (rt->silentOutput.load())
            {
                newStatus = "No output - check plugin";
                colour = theme::warn;
            }
            else if (rt->userBypassed.load())
            {
                newStatus = "Bypassed";
                colour = theme::warn;
            }
            else
            {
                newStatus = "Active";
            }
        }

        rearmButton.setVisible (faulted);
        editButton.setEnabled (rt != nullptr && rt->plugin != nullptr);

        if (newName != name || newDetail != detail || newStatus != status || colour != statusColour)
        {
            name = newName;
            detail = newDetail;
            status = newStatus;
            statusColour = colour;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (0, 3);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (statusColour);
        g.fillRoundedRectangle (r.removeFromLeft (4).toFloat(), 2.0f);

        r = r.reduced (12, 6);
        r.removeFromRight (buttonsWidth);

        const int index = slot.getParent().indexOf (slot) + 1;
        g.setColour (theme::dim);
        g.setFont (theme::font (13.0f, true));
        g.drawText (juce::String (index), r.removeFromLeft (22), juce::Justification::centredLeft);

        auto statusArea = r.removeFromRight (200);
        g.setColour (statusColour);
        g.setFont (theme::font (12.0f, true));
        g.drawText (status, statusArea, juce::Justification::centredRight, true);

        g.setColour (theme::text);
        g.setFont (theme::font (15.0f, true));
        g.drawText (name, r.removeFromTop (r.getHeight() / 2), juce::Justification::bottomLeft, true);
        g.setColour (theme::dim);
        g.setFont (theme::font (12.0f));
        g.drawText (detail, r, juce::Justification::topLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 14).removeFromRight (buttonsWidth - 8);
        removeButton.setBounds (r.removeFromRight (30));
        r.removeFromRight (4);
        downButton.setBounds (r.removeFromRight (30));
        r.removeFromRight (2);
        upButton.setBounds (r.removeFromRight (30));
        r.removeFromRight (8);
        bypassButton.setBounds (r.removeFromRight (70));
        r.removeFromRight (4);
        editButton.setBounds (r.removeFromRight (56));
        r.removeFromRight (4);
        rearmButton.setBounds (r.removeFromRight (70));
    }

private:
    static constexpr int buttonsWidth = 320;

    AppContext& ctx;
    juce::ValueTree slot;
    juce::String name, detail, status;
    juce::Colour statusColour;

    juce::TextButton editButton { "Edit" }, bypassButton { "Bypass" }, rearmButton { "Re-arm" };
    juce::TextButton upButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x91")) };
    juce::TextButton downButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x93")) };
    juce::TextButton removeButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
};

// ---------------------------------------------------------------------------
RackView::RackView (AppContext& c) : ctx (c)
{
    nameLabel.setEditable (true);
    nameLabel.setFont (theme::font (22.0f, true));
    nameLabel.setColour (juce::Label::textWhenEditingColourId, theme::text);
    nameLabel.onTextChange = [this]
    {
        if (rack.isValid() && nameLabel.getText().trim().isNotEmpty())
            rack.setProperty (ids::name, nameLabel.getText().trim(), nullptr);
    };
    addAndMakeVisible (nameLabel);

    widthBox.addItem ("Mono", 1);
    widthBox.addItem ("Stereo", 2);
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

    restoreButton.setColour (juce::TextButton::buttonColourId, theme::fault.withAlpha (0.4f));
    restoreButton.onClick = [this] { if (rack.isValid()) ctx.host.restoreRack (rack[ids::id].toString()); };
    addChildComponent (restoreButton);

    addPluginButton.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.25f));
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
    inputButton.setButtonText ("Input:  " + routing.substring (0, arrow).trim());
    outputButton.setButtonText ("Output:  " + routing.substring (arrow + 1).trim());

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
        text = "DRY FALLBACK - a plugin failed, this rack is passing its clean input";
        colour = theme::fault;
    }
    else if ((bool) rack[ids::bypassed] || ctx.engine.isGlobalBypassed())
    {
        text = "Bypassed - passing input through";
        colour = theme::warn;
    }
    else if (ctx.host.getRoutingError().isNotEmpty())
    {
        text = ctx.host.getRoutingError();
        colour = theme::fault;
    }
    else if (rt != nullptr)
    {
        text = "Rack cpu " + juce::String (rt->processMs.load(), 2) + " ms";
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
        g.setFont (theme::font (15.0f));
        g.drawText ("Select or add a rack", getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setColour (statusColour);
    g.setFont (theme::font (13.0f, statusColour == theme::fault));
    g.drawText (statusText, getLocalBounds().reduced (24, 0).withTop (108).withHeight (24),
                juce::Justification::centredLeft, true);

    if (rows.isEmpty())
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (14.0f));
        g.drawText ("No plugins yet. Add one below.", viewport.getBounds().withHeight (80), juce::Justification::centred);
    }
}

void RackView::resized()
{
    auto r = getLocalBounds().reduced (24, 16);

    auto row1 = r.removeFromTop (36);
    deleteButton.setBounds (row1.removeFromRight (100).reduced (0, 4));
    row1.removeFromRight (16);
    bypassToggle.setBounds (row1.removeFromRight (110));
    criticalToggle.setBounds (row1.removeFromRight (90));
    row1.removeFromRight (8);
    widthBox.setBounds (row1.removeFromRight (100).reduced (0, 5));
    row1.removeFromRight (12);
    nameLabel.setBounds (row1.translated (-4, 0));

    r.removeFromTop (12);
    auto row2 = r.removeFromTop (32);
    inputButton.setBounds (row2.removeFromLeft (240));
    row2.removeFromLeft (8);
    outputButton.setBounds (row2.removeFromLeft (200));
    restoreButton.setBounds (row2.removeFromRight (160));

    r.removeFromTop (34); // status line painted here
    auto bottom = r.removeFromBottom (40);
    addPluginButton.setBounds (bottom.removeFromLeft (160).withTrimmedTop (6));

    viewport.setBounds (r);
    const int rowHeight = 64;
    const int width = viewport.getWidth() - viewport.getScrollBarThickness();
    rowHolder.setSize (width, juce::jmax (1, rows.size() * rowHeight));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, width, rowHeight);
}
} // namespace lr
