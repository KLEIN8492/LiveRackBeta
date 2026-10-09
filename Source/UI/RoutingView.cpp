#include "RoutingView.h"

namespace lr
{
class RoutingView::Grid final : public juce::Component, public juce::TooltipClient
{
public:
    explicit Grid (AppContext& c) : ctx (c) {}

    static constexpr int cell = 22, nameWidth = 170, headerHeight = 54, gap = 28;

    void layout()
    {
        numIn = ctx.inputNames().size();
        numOut = ctx.outputNames().size();
        numRacks = show::getRacks (ctx.host.getShow()).getNumChildren();
        setSize (nameWidth + (numIn + numOut) * cell + gap + 20, headerHeight + juce::jmax (1, numRacks) * cell + 20);
        repaint();
    }

    juce::Rectangle<int> cellBounds (int row, int col) const
    {
        const int x = nameWidth + col * cell + (col >= numIn ? gap : 0);
        return { x, headerHeight + row * cell, cell, cell };
    }

    // Returns {row, col} under the point or {-1,-1}.
    std::pair<int, int> hit (juce::Point<int> p) const
    {
        for (int row = 0; row < numRacks; ++row)
            for (int col = 0; col < numIn + numOut; ++col)
                if (cellBounds (row, col).contains (p))
                    return { row, col };
        return { -1, -1 };
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto [row, col] = hit (e.getPosition());
        if (row < 0)
            return;

        auto rack = show::getRacks (ctx.host.getShow()).getChild (row);
        const bool isInput = col < numIn;
        const int ch = isInput ? col : col - numIn;
        const auto& prop = isInput ? ids::inputs : ids::outputs;

        auto chans = show::getChannels (rack, prop);
        auto it = std::find (chans.begin(), chans.end(), ch);
        if (it != chans.end()) chans.erase (it);
        else                   chans.push_back (ch);
        std::sort (chans.begin(), chans.end());

        show::setChannels (rack, prop, chans);
        if (isInput)
            rack.setProperty (ids::sources, "", nullptr);

        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto h = hit (e.getPosition());
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = { -1, -1 };
        repaint();
    }

    juce::String getTooltip() override
    {
        auto [row, col] = hover;
        if (row < 0)
            return {};
        auto rack = show::getRacks (ctx.host.getShow()).getChild (row);
        if (col < numIn)
            return ctx.inputNames()[col] + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + rack[ids::name].toString();
        return rack[ids::name].toString() + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + ctx.outputNames()[col - numIn];
    }

    void paint (juce::Graphics& g) override
    {
        auto show = ctx.host.getShow();
        auto racks = show::getRacks (show);

        g.setFont (theme::font (11.0f, true));
        g.setColour (theme::dim);
        if (numIn > 0)
            g.drawText ("INPUTS", cellBounds (0, 0).withY (4).withHeight (16).withWidth (numIn * cell), juce::Justification::centredLeft);
        if (numOut > 0)
            g.drawText ("OUTPUTS", cellBounds (0, numIn).withY (4).withHeight (16).withWidth (numOut * cell), juce::Justification::centredLeft);

        g.setFont (theme::font (10.5f));
        for (int col = 0; col < numIn + numOut; ++col)
        {
            const int n = (col < numIn ? col : col - numIn) + 1;
            g.drawText (juce::String (n), cellBounds (0, col).withY (headerHeight - 22).withHeight (20), juce::Justification::centred);
        }

        for (int row = 0; row < numRacks; ++row)
        {
            auto rack = racks.getChild (row);
            const auto ins = show::getChannels (rack, ids::inputs);
            const auto outs = show::getChannels (rack, ids::outputs);
            const bool isBus = rack[ids::sources].toString().isNotEmpty();

            auto label = juce::Rectangle<int> (0, headerHeight + row * cell, nameWidth - 10, cell);
            g.setColour (row == hover.first ? theme::text : theme::dim);
            g.setFont (theme::font (12.5f, row == hover.first));
            g.drawText (rack[ids::name].toString() + (isBus ? "  (bus)" : ""), label, juce::Justification::centredRight, true);

            for (int col = 0; col < numIn + numOut; ++col)
            {
                const bool isInput = col < numIn;
                const int ch = isInput ? col : col - numIn;
                const auto& list = isInput ? ins : outs;
                const bool on = std::find (list.begin(), list.end(), ch) != list.end();
                const bool hot = hover.first == row || hover.second == col;

                auto r = cellBounds (row, col).reduced (2).toFloat();
                g.setColour (hot ? theme::raised.brighter (0.08f) : theme::raised);
                g.fillRect (r);

                if (on)
                {
                    g.setColour (isInput ? theme::accent : theme::text);
                    g.fillRect (r.reduced (3.0f));
                }
            }
        }

        if (numIn + numOut == 0)
        {
            g.setColour (theme::warn);
            g.setFont (theme::font (12.0f, true));
            g.drawText ("NO AUDIO DEVICE  -  choose one in SETTINGS", 0, headerHeight, 500, 24, juce::Justification::centredLeft);
            return;
        }

        if (numRacks == 0)
        {
            g.setColour (theme::dim);
            g.setFont (theme::font (13.0f));
            g.drawText ("Add racks to route channels.", 0, headerHeight, 400, 24, juce::Justification::centredLeft);
        }
    }

private:
    AppContext& ctx;
    int numIn = 0, numOut = 0, numRacks = 0;
    std::pair<int, int> hover { -1, -1 };
};

RoutingView::RoutingView (AppContext& c) : grid (std::make_unique<Grid> (c))
{
    viewport.setViewedComponent (grid.get(), false);
    addAndMakeVisible (viewport);
}

RoutingView::~RoutingView() = default;

void RoutingView::refresh()
{
    grid->layout();
}

void RoutingView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    g.setColour (theme::dim);
    g.setFont (theme::font (12.5f));
    g.drawText ("Click to patch. Several racks on one output are summed. Buses (sum of racks) are set from a rack's Input menu.",
                getLocalBounds().reduced (24, 0).removeFromTop (40), juce::Justification::centredLeft, true);
}

void RoutingView::resized()
{
    viewport.setBounds (getLocalBounds().reduced (24, 0).withTrimmedTop (40).withTrimmedBottom (16));
    grid->layout();
}
} // namespace lr
