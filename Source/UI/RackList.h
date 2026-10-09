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
    juce::Colour statusColour (const juce::ValueTree& rack) const;
    static juce::Rectangle<int> meterArea (int rowWidth, int rowHeight);

    AppContext& ctx;
    juce::ListBox list { "Racks", this };
    juce::TextButton addMono { "+ MONO RACK" }, addStereo { "+ STEREO RACK" };
    juce::String selectedId;

    // Per rack, what is on screen: tick() repaints only what changed.
    struct Meters
    {
        MeterSmoother in, out;
        int inLit = -1, outLit = -1;
        juce::Colour status;
    };
    std::map<juce::String, Meters> meters;
    std::map<juce::String, juce::String> routingText; // built in refresh(), not per paint
};
} // namespace lr
