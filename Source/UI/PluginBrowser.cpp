#include "PluginBrowser.h"
#include <set>

namespace lr
{
PluginBrowser::PluginBrowser (juce::KnownPluginList& known, std::function<void (const juce::PluginDescription&)> onChosen)
    : chosen (std::move (onChosen))
{
    // Prefer VST3: hide an AU when the same plugin is installed as VST3
    // (some AU variants, e.g. MIDI-controlled "aumf" effects, misbehave in hosts).
    const auto types = known.getTypes();
    auto key = [] (const juce::PluginDescription& d) { return (d.manufacturerName + "|" + d.name).toLowerCase(); };
    std::set<juce::String> vst3;
    for (auto& d : types)
        if (d.pluginFormatName == "VST3")
            vst3.insert (key (d));

    for (auto& d : types)
    {
        if (d.isInstrument && d.numInputChannels == 0)
            continue;
        if (d.pluginFormatName == "AudioUnit" && vst3.count (key (d)) > 0)
            continue;
        all.push_back (d);
    }

    std::sort (all.begin(), all.end(), [] (auto& a, auto& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0
            || (a.name.equalsIgnoreCase (b.name) && a.pluginFormatName > b.pluginFormatName); // VST3 before AU
    });

    search.setTextToShowWhenEmpty ("Search plugins or makers...", theme::dim);
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { choose (juce::jmax (0, list.getSelectedRow())); };
    addAndMakeVisible (search);

    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, theme::panel);
    addAndMakeVisible (list);

    addButton.onClick = [this] { choose (list.getSelectedRow()); };
    addAndMakeVisible (addButton);

    filter();
    setSize (460, 520);
}

void PluginBrowser::filter()
{
    const auto words = juce::StringArray::fromTokens (search.getText(), true);
    shown.clear();

    for (auto& d : all)
    {
        const auto hay = d.name + " " + d.manufacturerName + " " + d.category + " " + d.pluginFormatName;
        bool match = true;
        for (auto& w : words)
            match &= hay.containsIgnoreCase (w);
        if (match)
            shown.push_back (d);
    }

    list.updateContent();
    list.selectRow (0);
    repaint();
}

void PluginBrowser::choose (int row)
{
    if (row < 0 || row >= (int) shown.size() || ! chosen)
        return;

    chosen (shown[(size_t) row]);

    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void PluginBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row >= (int) shown.size())
        return;

    auto& d = shown[(size_t) row];
    if (selected)
    {
        g.setColour (theme::accent.withAlpha (0.18f));
        g.fillRect (0, 0, width, height);
    }

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);
    g.setColour (theme::dim);
    g.setFont (theme::font (11.0f, true));
    g.drawText (d.pluginFormatName, r.removeFromRight (48), juce::Justification::centredRight);

    g.setColour (theme::text);
    g.setFont (theme::font (14.0f));
    g.drawText (d.name, r.removeFromTop (height / 2 + 3).withTrimmedTop (3), juce::Justification::bottomLeft, true);
    g.setColour (theme::dim);
    g.setFont (theme::font (11.5f));
    g.drawText (d.manufacturerName, r, juce::Justification::topLeft, true);
}

void PluginBrowser::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    if (all.empty())
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (13.0f));
        g.drawFittedText ("No plugins scanned yet.\nOpen Settings and use Options > Scan for new or updated plug-ins.",
                          getLocalBounds().reduced (30), juce::Justification::centred, 4);
    }
}

void PluginBrowser::resized()
{
    auto r = getLocalBounds().reduced (10);
    search.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (32);
    addButton.setBounds (bottom.removeFromRight (90));
    r.removeFromBottom (8);
    list.setBounds (r);
    list.setVisible (! all.empty());
}
} // namespace lr
