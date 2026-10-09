#pragma once

#include "AppContext.h"

namespace lr
{
// The selected rack: header (name, width, critical, bypass, routing) and the
// plugin chain with per-slot status and controls.
class RackView final : public juce::Component, private juce::ValueTree::Listener
{
public:
    explicit RackView (AppContext&);
    ~RackView() override;

    void setRack (const juce::String& rackId);
    void refresh();   // rebuild rows (structure / plugin load changes)
    void tick();      // 30 Hz status update

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SlotRow;

    void rebuildRows();
    void showInputMenu();
    void showOutputMenu();
    void showPluginBrowser();
    void deleteRack();

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { rebuildRows(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { rebuildRows(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override         { rebuildRows(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    AppContext& ctx;
    juce::ValueTree rack;

    juce::Label nameLabel;
    juce::ComboBox widthBox;
    juce::ToggleButton criticalToggle { "CRITICAL" }, bypassToggle { "BYPASS" };
    juce::TextButton inputButton, outputButton, deleteButton { "DELETE RACK" };
    juce::TextButton restoreButton { "RESTORE PROCESSING" }, addPluginButton { "+ ADD PLUGIN" };
    juce::Rectangle<int> headerArea, statusArea, tableHeader, footerArea;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> captions;
    juce::String statusText;
    juce::Colour statusColour;

    juce::Viewport viewport;
    juce::Component rowHolder;
    juce::OwnedArray<SlotRow> rows;
};
} // namespace lr
