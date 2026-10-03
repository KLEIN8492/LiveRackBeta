#pragma once

#include "AppContext.h"

namespace lr
{
// Searchable list of scanned plugins, shown in a call-out next to "+ Add plugin".
class PluginBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    PluginBrowser (juce::KnownPluginList&, std::function<void (const juce::PluginDescription&)> onChosen);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void filter();
    void choose (int row);

    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { choose (row); }
    void returnKeyPressed (int row) override { choose (row); }

    std::vector<juce::PluginDescription> all, shown;
    std::function<void (const juce::PluginDescription&)> chosen;
    juce::TextEditor search;
    juce::ListBox list { "Plugins", this };
    juce::TextButton addButton { "Add" };
};
} // namespace lr
