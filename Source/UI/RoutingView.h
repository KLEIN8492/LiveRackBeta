#pragma once

#include "AppContext.h"

namespace lr
{
// Patchbay: device inputs -> racks on the left, racks -> device outputs on the right.
class RoutingView final : public juce::Component
{
public:
    explicit RoutingView (AppContext&);
    ~RoutingView() override;

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Grid;
    juce::Viewport viewport;
    std::unique_ptr<Grid> grid;
};
} // namespace lr
