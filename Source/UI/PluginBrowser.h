#pragma once

#include "AppContext.h"
#include "Core/PluginCategory.h"

namespace lr
{
// Plugin picker shown next to "+ ADD PLUGIN": category column (with counts) on
// the left, searchable list on the right. Right-click a plugin to override its
// category; overrides and the last category viewed are remembered.
class PluginBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    PluginBrowser (juce::KnownPluginList&, juce::PropertiesFile& settings,
                   std::function<void (const juce::PluginDescription&)> onChosen);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Entry
    {
        juce::PluginDescription desc;
        PluginCategory category;
        bool overridden = false;
    };

    void categorise();
    void rebuildCategoryButtons();
    void filter();
    void choose (int row);
    void showOverrideMenu (int row);
    static juce::String overrideKey (const juce::PluginDescription&);

    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { choose (row); }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { choose (row); }

    juce::PropertiesFile& settings;
    std::vector<Entry> all;
    std::vector<const Entry*> shown;
    int currentCategory = -1;              // -1 = all
    std::function<void (const juce::PluginDescription&)> chosen;

    juce::OwnedArray<juce::TextButton> categoryButtons;
    juce::Component categoryHolder;
    juce::Viewport categoryView;
    juce::TextEditor search;
    juce::ListBox list { "Plugins", this };
    juce::TextButton addButton { "ADD" };
    juce::Rectangle<int> hintArea;
};
} // namespace lr
