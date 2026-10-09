#include "RoutingView.h"

namespace lr
{
class RoutingView::Grid final : public juce::Component, public juce::TooltipClient
{
public:
    explicit Grid (AppContext& c) : ctx (c) {}

    static constexpr int cell = 20, nameWidth = 170, headerHeight = 62, gap = 14, sectionGap = 30;

    struct Column
    {
        PortRef port;
        bool isInput;
        juce::String channelName;
        int x;
    };

    void layout()
    {
        columns.clear();
        groups.clear();
        sections.clear();
        int x = nameWidth;

        for (bool isInput : { true, false })
        {
            const int sectionStart = x;
            for (auto& o : ctx.devices.getOpen())
            {
                const auto* info = ctx.devices.findAvailable (o->name);
                if (info == nullptr)
                    continue;
                const auto& names = isInput ? info->inputs : info->outputs;
                if (names.isEmpty())
                    continue;

                const int groupStart = x;
                for (int ch = 0; ch < names.size(); ++ch, x += cell)
                    columns.push_back ({ { o->name, ch }, isInput, names[ch], x });
                groups.push_back ({ o->name, isInput, groupStart, x });
                x += gap;
            }
            sections.push_back ({ isInput ? "INPUTS" : "OUTPUTS", sectionStart });
            x += sectionGap;
        }

        numRacks = show::getRacks (ctx.host.getShow()).getNumChildren();
        setSize (juce::jmax (x + 20, 400), headerHeight + juce::jmax (1, numRacks) * cell + 20);
        repaint();
    }

    juce::Rectangle<int> cellBounds (int row, const Column& c) const
    {
        return { c.x, headerHeight + row * cell, cell, cell };
    }

    std::pair<int, int> hit (juce::Point<int> p) const
    {
        const int row = (p.y - headerHeight) / cell;
        if (p.y < headerHeight || row >= numRacks)
            return { -1, -1 };
        for (size_t i = 0; i < columns.size(); ++i)
            if (p.x >= columns[i].x && p.x < columns[i].x + cell)
                return { row, (int) i };
        return { -1, -1 };
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto [row, col] = hit (e.getPosition());
        if (row < 0)
            return;

        auto rack = show::getRacks (ctx.host.getShow()).getChild (row);
        const auto& c = columns[(size_t) col];
        const auto& prop = c.isInput ? ids::inputs : ids::outputs;

        auto ports = show::getPorts (rack, prop);
        togglePort (ports, c.port);
        show::setPorts (rack, prop, ports);
        if (c.isInput)
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
        const auto& c = columns[(size_t) col];
        const auto port = c.port.device + " " + juce::String (c.port.channel + 1) + " (" + c.channelName + ")";
        const auto arrow = juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "));
        return c.isInput ? port + arrow + rack[ids::name].toString() : rack[ids::name].toString() + arrow + port;
    }

    void paint (juce::Graphics& g) override
    {
        if (columns.empty())
        {
            g.setColour (theme::warn);
            g.setFont (theme::font (12.0f, true));
            g.drawText ("NO INTERFACE OPEN  -  pick a channel from a rack's INPUT or OUTPUT menu", 0, 0, 600, 24,
                        juce::Justification::centredLeft);
            return;
        }

        auto racks = show::getRacks (ctx.host.getShow());

        for (auto& s : sections)
            theme::caption (g, s.label, { s.x, 0, 200, 14 });

        for (auto& gr : groups)
        {
            auto r = juce::Rectangle<int> (gr.start, 16, gr.end - gr.start, 16);
            g.setColour (theme::raised);
            g.fillRect (r);
            g.setColour (theme::text);
            g.setFont (theme::font (10.5f, true));
            g.drawText (gr.name, r.reduced (4, 0), juce::Justification::centredLeft, true);
        }

        g.setFont (theme::mono (9.5f));
        for (auto& c : columns)
        {
            g.setColour (theme::dim);
            g.drawText (juce::String (c.port.channel + 1), c.x, headerHeight - 22, cell, 18, juce::Justification::centred);
        }

        for (int row = 0; row < numRacks; ++row)
        {
            auto rack = racks.getChild (row);
            const auto ins = show::getPorts (rack, ids::inputs);
            const auto outs = show::getPorts (rack, ids::outputs);
            const bool isBus = rack[ids::sources].toString().isNotEmpty();

            auto label = juce::Rectangle<int> (0, headerHeight + row * cell, nameWidth - 10, cell);
            g.setColour (row == hover.first ? theme::text : theme::dim);
            g.setFont (theme::font (12.0f, row == hover.first));
            g.drawText (rack[ids::name].toString() + (isBus ? "  (bus)" : ""), label, juce::Justification::centredRight, true);

            for (size_t i = 0; i < columns.size(); ++i)
            {
                const auto& c = columns[i];
                const auto& list = c.isInput ? ins : outs;
                const bool on = std::find (list.begin(), list.end(), c.port) != list.end();
                const bool hot = hover.first == row || hover.second == (int) i;

                auto r = cellBounds (row, c).reduced (1);
                g.setColour (hot ? theme::select : theme::raised);
                g.fillRect (r);
                if (on)
                {
                    g.setColour (c.isInput ? theme::accent : theme::text);
                    g.fillRect (r.reduced (3));
                }
            }
        }
    }

private:
    struct Group { juce::String name; bool isInput; int start, end; };
    struct Section { juce::String label; int x; };

    AppContext& ctx;
    std::vector<Column> columns;
    std::vector<Group> groups;
    std::vector<Section> sections;
    int numRacks = 0;
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
    theme::caption (g, "Click a cell to patch.  Several racks on one output are summed.  "
                       "Only interfaces in use are shown; add one from a rack's INPUT / OUTPUT menu.",
                    getLocalBounds().reduced (16, 0).removeFromTop (34));
}

void RoutingView::resized()
{
    viewport.setBounds (getLocalBounds().reduced (16, 0).withTrimmedTop (34).withTrimmedBottom (12));
    grid->layout();
}
} // namespace lr
