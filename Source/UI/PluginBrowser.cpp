#include "PluginBrowser.h"
#include <set>

namespace lr
{
PluginBrowser::PluginBrowser (juce::KnownPluginList& known, juce::PropertiesFile& s,
                              std::function<void (const juce::PluginDescription&)> onChosen)
    : settings (s), chosen (std::move (onChosen))
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
        all.push_back ({ d, PluginCategory::other });
    }

    std::sort (all.begin(), all.end(), [] (auto& a, auto& b)
    {
        return a.desc.name.compareIgnoreCase (b.desc.name) < 0
            || (a.desc.name.equalsIgnoreCase (b.desc.name) && a.desc.pluginFormatName > b.desc.pluginFormatName);
    });

    categorise();

    currentCategory = settings.getIntValue ("browserCategory", -1);
    if (currentCategory >= (int) PluginCategory::count)
        currentCategory = -1;

    categoryView.setViewedComponent (&categoryHolder, false);
    categoryView.setScrollBarsShown (true, false);
    addAndMakeVisible (categoryView);

    search.setTextToShowWhenEmpty ("Search plugins or makers...", theme::dim);
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { choose (juce::jmax (0, list.getSelectedRow())); };
    addAndMakeVisible (search);

    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, theme::panel);
    addAndMakeVisible (list);

    addButton.onClick = [this] { choose (list.getSelectedRow()); };
    addAndMakeVisible (addButton);

    rebuildCategoryButtons();
    filter();
    setSize (720, 560);
}

juce::String PluginBrowser::overrideKey (const juce::PluginDescription& d)
{
    return "category." + d.createIdentifierString();
}

void PluginBrowser::categorise()
{
    for (auto& e : all)
    {
        const auto stored = settings.getValue (overrideKey (e.desc));
        e.overridden = stored.isNotEmpty();
        e.category = e.overridden ? categoryFromKey (stored) : classify (e.desc);
    }
}

void PluginBrowser::rebuildCategoryButtons()
{
    categoryButtons.clear();

    std::map<int, int> counts;
    for (auto& e : all)
        ++counts[(int) e.category];

    auto add = [this] (int id, const juce::String& label)
    {
        auto* b = categoryButtons.add (new juce::TextButton (label));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (9);
        b->setColour (juce::TextButton::buttonOnColourId, theme::select.brighter (0.2f));
        b->setColour (juce::TextButton::textColourOnId, theme::text);
        b->setToggleState (id == currentCategory, juce::dontSendNotification);
        b->onClick = [this, id]
        {
            currentCategory = id;
            settings.setValue ("browserCategory", id);
            filter();
        };
        categoryHolder.addAndMakeVisible (b);
    };

    add (-1, "ALL  (" + juce::String ((int) all.size()) + ")");
    for (int c = 0; c < (int) PluginCategory::count; ++c)
        if (counts[c] > 0)
            add (c, juce::String (displayName ((PluginCategory) c)).toUpperCase() + "  (" + juce::String (counts[c]) + ")");

    // The remembered category may have emptied (override moved its last plugin).
    if (currentCategory >= 0 && counts[currentCategory] == 0)
        currentCategory = -1;
    if (currentCategory < 0)
        categoryButtons[0]->setToggleState (true, juce::dontSendNotification);

    resized();
}

void PluginBrowser::filter()
{
    const auto words = juce::StringArray::fromTokens (search.getText(), true);
    shown.clear();

    for (auto& e : all)
    {
        // A search looks across every category; otherwise show the chosen one.
        if (words.isEmpty() && currentCategory >= 0 && (int) e.category != currentCategory)
            continue;

        const auto hay = e.desc.name + " " + e.desc.manufacturerName + " " + displayName (e.category) + " "
                       + e.desc.pluginFormatName;
        bool match = true;
        for (auto& w : words)
            match &= hay.containsIgnoreCase (w);
        if (match)
            shown.push_back (&e);
    }

    list.updateContent();
    list.selectRow (0);
    repaint();
}

void PluginBrowser::choose (int row)
{
    if (row < 0 || row >= (int) shown.size() || ! chosen)
        return;

    chosen (shown[(size_t) row]->desc);

    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void PluginBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showOverrideMenu (row);
}

void PluginBrowser::showOverrideMenu (int row)
{
    if (row < 0 || row >= (int) shown.size())
        return;

    const auto desc = shown[(size_t) row]->desc;
    const auto current = shown[(size_t) row]->category;
    const bool overridden = shown[(size_t) row]->overridden;

    juce::PopupMenu categories;
    categories.addItem (1000, "Automatic  (" + juce::String (displayName (classify (desc))) + ")", true, ! overridden);
    categories.addSeparator();
    for (int c = 0; c < (int) PluginCategory::count; ++c)
        categories.addItem (1 + c, displayName ((PluginCategory) c), true, overridden && (int) current == c);

    juce::PopupMenu menu;
    menu.addSectionHeader (desc.name);
    menu.addSubMenu ("Set category", categories);

    menu.showMenuAsync (juce::PopupMenu::Options(), [this, desc] (int result)
    {
        if (result <= 0)
            return;
        if (result == 1000)
            settings.removeValue (overrideKey (desc));
        else
            settings.setValue (overrideKey (desc), categoryKey ((PluginCategory) (result - 1)));

        categorise();
        rebuildCategoryButtons();
        filter();
    });
}

void PluginBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row >= (int) shown.size())
        return;

    const auto& e = *shown[(size_t) row];
    g.setColour (selected ? theme::select : (row % 2 == 0 ? theme::panel : theme::stripe));
    g.fillRect (0, 0, width, height);

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);
    g.setColour (theme::dim);
    g.setFont (theme::font (10.5f, true));
    g.drawText (e.desc.pluginFormatName == "AudioUnit" ? "AU" : e.desc.pluginFormatName,
                r.removeFromRight (36), juce::Justification::centredRight);

    auto tag = r.removeFromRight (150);
    g.setColour (e.overridden ? theme::text : theme::dim);
    g.setFont (theme::font (10.5f, true));
    g.drawText (juce::String (displayName (e.category)).toUpperCase() + (e.overridden ? " *" : ""),
                tag, juce::Justification::centredRight, true);

    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText (e.desc.name, r.removeFromTop (height / 2 + 2).withTrimmedTop (3), juce::Justification::bottomLeft, true);
    g.setColour (theme::dim);
    g.setFont (theme::font (11.0f));
    g.drawText (e.desc.manufacturerName, r, juce::Justification::topLeft, true);
}

void PluginBrowser::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);

    auto left = getLocalBounds().removeFromLeft (200);
    g.setColour (theme::bg);
    g.fillRect (left);
    g.setColour (theme::line);
    g.drawVerticalLine (left.getRight(), 0.0f, (float) getHeight());
    theme::caption (g, "Categories", left.reduced (10, 0).removeFromTop (28));

    if (all.empty())
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (12.0f));
        g.drawFittedText ("No plugins scanned yet.\nOpen Settings (⌘,) > PLUGINS and scan.",
                          getLocalBounds().withTrimmedLeft (200).reduced (30), juce::Justification::centred, 3);
    }
    else
    {
        theme::caption (g, "Double-click to add.  Right-click to set a plugin's category.", hintArea);
    }

    if (! all.empty() && shown.empty())
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (12.0f));
        g.drawText ("NO MATCH", getLocalBounds().withTrimmedLeft (200), juce::Justification::centred);
    }
}

void PluginBrowser::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (200).withTrimmedTop (28).reduced (6, 0).withTrimmedBottom (6);
    categoryView.setBounds (left);

    const int w = left.getWidth() - categoryView.getScrollBarThickness() - 2;
    int y = 0;
    for (auto* b : categoryButtons)
    {
        b->setBounds (0, y, w, 26);
        y += 28;
    }
    categoryHolder.setSize (w, juce::jmax (1, y));

    r = r.reduced (10);
    search.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (30);
    addButton.setBounds (bottom.removeFromRight (90));
    hintArea = bottom;
    r.removeFromBottom (8);
    list.setBounds (r);
    list.setVisible (! all.empty());
}
} // namespace lr
