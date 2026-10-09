#pragma once

#include "AppContext.h"
#include <map>

namespace lr
{
// Left column: every rack with routing, status light and meters.
class RackList final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit RackList (AppContext&);

    void refresh();             // structure changed
    void tick();                // 30 Hz: meters & status
    void selectRack (const juce::String& rackId);
    juce::String getSelectedRackId() const { return selectedId; }

    std::function<void (const juce::String&)> onSelectionChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void addRack (int channels);

    AppContext& ctx;
    juce::ListBox list { "Racks", this };
    juce::TextButton addMono { "+ MONO RACK" }, addStereo { "+ STEREO RACK" };
    juce::String selectedId;

    struct Meters { MeterSmoother in, out; };
    std::map<juce::String, Meters> meters;
};
} // namespace lr
