#include "SettingsWindow.h"

namespace lr
{
// ---------------------------------------------------------------------------
// AUDIO: shared format + every interface on the Mac and its state.
class SettingsComponent::AudioPage final : public juce::Component
{
public:
    explicit AudioPage (AppContext& c) : ctx (c)
    {
        for (int rate : { 44100, 48000, 88200, 96000 })
            rateBox.addItem (juce::String (rate), rate);
        for (int b : { 16, 32, 48, 64, 96, 128, 256, 512, 1024 })
            blockBox.addItem (juce::String (b), b);

        rateBox.setSelectedId ((int) ctx.devices.getSampleRate(), juce::dontSendNotification);
        blockBox.setSelectedId (ctx.devices.getBlockSize(), juce::dontSendNotification);
        rateBox.onChange = blockBox.onChange = [this] { apply(); };
        addAndMakeVisible (rateBox);
        addAndMakeVisible (blockBox);
    }

    void apply()
    {
        const double rate = rateBox.getSelectedId();
        const int block = blockBox.getSelectedId();
        ctx.settings.setValue ("sampleRate", rate);
        ctx.settings.setValue ("bufferSize", block);
        ctx.devices.setFormat (rate, block);
        if (ctx.devices.onChanged)
            ctx.devices.onChanged();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::bg);
        auto r = getLocalBounds().reduced (16, 12);

        theme::caption (g, "Sample rate (all interfaces)", { 16, 12, 200, 12 });
        theme::caption (g, "Buffer size (samples)", { 196, 12, 200, 12 });

        g.setColour (theme::dim);
        g.setFont (theme::font (11.5f));
        g.drawText ("RTL = round trip of the interface itself. Racks crossing two interfaces add about one buffer.",
                    r.withTrimmedTop (50).removeFromTop (16), juce::Justification::centredLeft, true);

        // Interface table.
        auto table = r.withTrimmedTop (76);
        auto header = table.removeFromTop (22);
        g.setColour (theme::raised);
        g.fillRect (header);
        g.setColour (theme::line);
        g.drawRect (header);

        const int widths[] = { 300, 50, 50, 90, 60, 70, 60 };
        const char* titles[] = { "Interface", "In", "Out", "Status", "Buf", "RTL ms", "Xrun" };
        auto cols = [&] (juce::Rectangle<int> row)
        {
            std::vector<juce::Rectangle<int>> out;
            row = row.reduced (8, 0);
            for (int w : widths)
                out.push_back (row.removeFromLeft (w));
            out.push_back (row); // detail
            return out;
        };

        auto hc = cols (header);
        for (int i = 0; i < 7; ++i)
            theme::caption (g, titles[i], hc[(size_t) i]);
        theme::caption (g, "Detail", hc[7]);

        int row = 0;
        for (auto& info : ctx.devices.getAvailable())
        {
            auto rr = table.removeFromTop (24);
            g.setColour (row++ % 2 == 0 ? theme::panel : theme::stripe);
            g.fillRect (rr);
            auto c = cols (rr);

            auto* dev = ctx.devices.deviceFor (info.name);
            const auto err = ctx.devices.getError (info.name);
            const bool wanted = ctx.devices.getWanted().count (info.name) > 0;

            g.setColour (theme::text);
            g.setFont (theme::font (12.0f, dev != nullptr));
            g.drawText (info.name, c[0], juce::Justification::centredLeft, true);

            g.setFont (theme::mono (11.5f));
            g.setColour (theme::dim);
            g.drawText (juce::String (info.inputs.size()), c[1], juce::Justification::centredLeft);
            g.drawText (juce::String (info.outputs.size()), c[2], juce::Justification::centredLeft);

            juce::String status = "IDLE", detail = "not used by any rack";
            juce::Colour colour = theme::dim;
            if (dev != nullptr)
            {
                status = "OPEN";
                colour = theme::accent;
                const double sr = dev->getCurrentSampleRate();
                const int rtl = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples();
                g.setColour (theme::text);
                g.drawText (juce::String (dev->getCurrentBufferSizeSamples()), c[4], juce::Justification::centredLeft);
                g.drawText (juce::String (sr > 0 ? 1000.0 * rtl / sr : 0.0, 2), c[5], juce::Justification::centredLeft);
                g.setColour (dev->getXRunCount() > 0 ? theme::warn : theme::text);
                g.drawText (juce::String (juce::jmax (0, dev->getXRunCount())), c[6], juce::Justification::centredLeft);
                detail = "in use";
                if (const int n = ctx.devices.getRestarts (info.name); n > 0)
                    detail << "   (reopened by watchdog x" << n << ")";
                if (dev->getCurrentBufferSizeSamples() != ctx.devices.getBlockSize())
                    detail = "runs at " + juce::String (dev->getCurrentBufferSizeSamples()) + " (its nearest supported size)";
            }
            else if (wanted)
            {
                status = "ERROR";
                colour = theme::fault;
                detail = err;
            }

            theme::led (g, c[3].removeFromLeft (8).withSizeKeepingCentre (8, 8), colour);
            g.setColour (colour);
            g.setFont (theme::font (11.0f, true));
            g.drawText (status, c[3].withTrimmedLeft (6), juce::Justification::centredLeft);

            g.setColour (status == "ERROR" ? theme::fault : theme::dim);
            g.setFont (theme::font (11.5f));
            g.drawText (detail, c[7], juce::Justification::centredLeft, true);
        }

        // Interfaces racks need that aren't connected.
        for (auto& name : ctx.devices.getWanted())
        {
            if (ctx.devices.findAvailable (name) != nullptr)
                continue;
            auto rr = table.removeFromTop (24);
            g.setColour (theme::stripe);
            g.fillRect (rr);
            auto c = cols (rr);
            g.setColour (theme::fault);
            g.setFont (theme::font (12.0f, true));
            g.drawText (name, c[0], juce::Justification::centredLeft, true);
            g.drawText ("MISSING", c[3], juce::Justification::centredLeft);
            g.setFont (theme::font (11.5f));
            g.drawText ("used by a rack but not connected", c[7], juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        rateBox.setBounds (16, 26, 160, 24);
        blockBox.setBounds (196, 26, 120, 24);
    }

private:
    AppContext& ctx;
    juce::ComboBox rateBox, blockBox;
};

// ---------------------------------------------------------------------------
class SettingsComponent::PluginsPage final : public juce::Component
{
public:
    explicit PluginsPage (AppContext& c)
        : ctx (c),
          list (c.host.getFormatManager(), c.host.getKnownPlugins(), c.session.getScanPedalFile(), &c.settings, true)
    {
        list.setNumberOfThreadsForScanning (4);
        addAndMakeVisible (list);

        for (auto* b : { &scanVst3, &scanOther })
        {
            b->setTooltip ("Finds new or updated plugins. Each one is tested in a separate process, so a bad plugin can't crash LiveRack.");
            addAndMakeVisible (*b);
        }
        scanVst3.onClick = [this] { scan ("VST3"); };

        // Audio Units on macOS, LV2 on Linux, nothing else on Windows.
        const auto other = platform::secondaryPluginFormat();
        scanOther.setButtonText (other == "AudioUnit" ? "SCAN AUDIO UNITS" : "SCAN " + other);
        scanOther.setVisible (other.isNotEmpty());
        scanOther.onClick = [this, other] { scan (other); };
    }

    void scan (const juce::String& formatName)
    {
        for (auto* format : ctx.host.getFormatManager().getFormats())
            if (format->getName() == formatName && format->canScanForPlugins())
                list.scanFor (*format);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::bg); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 12);
        auto buttons = r.removeFromTop (28);
        scanVst3.setBounds (buttons.removeFromLeft (120));
        buttons.removeFromLeft (6);
        scanOther.setBounds (buttons.removeFromLeft (160));
        r.removeFromTop (10);
        list.setBounds (r);
    }

private:
    AppContext& ctx;
    juce::PluginListComponent list;
    juce::TextButton scanVst3 { "SCAN VST3" }, scanOther;
};

// ---------------------------------------------------------------------------
class SettingsComponent::PerformancePage final : public juce::Component
{
public:
    explicit PerformancePage (AppContext& c) : ctx (c)
    {
        boost.setToggleState (ctx.devices.isBoostEnabled(), juce::dontSendNotification);
        boost.onClick = [this]
        {
            ctx.settings.setValue ("lowLatencyBoost", boost.getToggleState());
            ctx.devices.setBoostEnabled (boost.getToggleState());
        };
        addAndMakeVisible (boost);

        align.setToggleState (ctx.host.isAligningRacks(), juce::dontSendNotification);
        align.onClick = [this]
        {
            ctx.settings.setValue ("alignRacks", align.getToggleState());
            ctx.host.setAlignRacks (align.getToggleState());
        };
        addAndMakeVisible (align);

        reveal.onClick = [] { SessionGuard::getSupportDirectory().getChildFile ("diagnostics.log").revealToUser(); };
        addAndMakeVisible (reveal);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::bg);
        auto r = getLocalBounds().reduced (16, 12);

        g.setColour (theme::dim);
        g.setFont (theme::font (11.5f));
        g.drawFittedText ("Keeps one CPU core busy whenever an interface runs at 128 samples or less, so the Mac never "
                          "slows the processor down between tiny audio blocks. Without it, 32-64 sample buffers click. "
                          "Uses more power: best on mains.",
                          r.withTrimmedTop (30).removeFromTop (48), juce::Justification::topLeft, 3, 1.0f);

        g.setColour (theme::dim);
        g.setFont (theme::font (11.5f));
        g.drawFittedText ("Delays every rack to the latency of the slowest one, so inserts summed on the console "
                          "(drum mics, stereo pairs, parallel buses) stay in phase. Off: each rack only has its own plugins' latency. "
                          "Either way a rack's latency never changes when plugins are bypassed or fault.",
                          r.withTrimmedTop (114).removeFromTop (48), juce::Justification::topLeft, 3, 1.0f);

        theme::caption (g, "Diagnostics", r.withTrimmedTop (184).removeFromTop (14));
        g.setColour (theme::text);
        g.setFont (theme::mono (11.0f));
        g.drawText (SessionGuard::getSupportDirectory().getChildFile ("diagnostics.log").getFullPathName(),
                    r.withTrimmedTop (202).removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (theme::dim);
        g.setFont (theme::font (11.5f));
        g.drawText ("One line per second per interface: buffer, worst callback, gaps, dropouts.",
                    r.withTrimmedTop (222).removeFromTop (18), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 12);
        boost.setBounds (r.removeFromTop (24).withWidth (420));
        align.setBounds (r.getX(), r.getY() + 64, 420, 24);
        reveal.setBounds (r.getX(), r.getY() + 224, 150, 26);
    }

private:
    AppContext& ctx;
    juce::ToggleButton boost { "LOW-LATENCY BOOST" }, align { "ALIGN ALL RACKS TO THE SAME LATENCY" };
    juce::TextButton reveal { "SHOW IN FINDER" };
};

// ---------------------------------------------------------------------------
SettingsComponent::SettingsComponent (AppContext& c) : ctx (c)
{
    setLookAndFeel (&lookAndFeel);

    const char* names[] = { "AUDIO", "ROUTING", "PLUGINS", "PERFORMANCE" };
    for (int i = 0; i < numPages; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (7);
        b->setColour (juce::TextButton::buttonOnColourId, theme::select.brighter (0.2f));
        b->setColour (juce::TextButton::textColourOnId, theme::text);
        b->onClick = [this, i] { showPage ((Page) i); };
        addAndMakeVisible (b);
    }

    audio = std::make_unique<AudioPage> (ctx);
    routing = std::make_unique<RoutingView> (ctx);
    plugins = std::make_unique<PluginsPage> (ctx);
    performance = std::make_unique<PerformancePage> (ctx);
    for (juce::Component* p : { (juce::Component*) audio.get(), (juce::Component*) routing.get(),
                                (juce::Component*) plugins.get(), (juce::Component*) performance.get() })
        addChildComponent (p);

    showPage (audioPage);
    setSize (980, 640);
    startTimerHz (4);
}

SettingsComponent::~SettingsComponent()
{
    setLookAndFeel (nullptr);
}

void SettingsComponent::showPage (Page p)
{
    page = p;
    tabs[p]->setToggleState (true, juce::dontSendNotification);
    audio->setVisible (p == audioPage);
    routing->setVisible (p == routingPage);
    plugins->setVisible (p == pluginsPage);
    performance->setVisible (p == performancePage);
    if (p == routingPage)
        routing->refresh();
}

void SettingsComponent::timerCallback()
{
    if (audio->isVisible())
        audio->repaint();
    if (routing->isVisible())
        routing->refresh();
}

void SettingsComponent::saveSnapshots (const juce::File& dir)
{
    const char* names[] = { "settings-audio", "settings-routing", "settings-plugins", "settings-performance" };
    for (int i = 0; i < numPages; ++i)
    {
        showPage ((Page) i);
        auto image = createComponentSnapshot (getLocalBounds(), true, 2.0f);
        auto file = dir.getChildFile (juce::String (names[i]) + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (out.openedOk())
            juce::PNGImageFormat().writeImageToStream (image, out);
    }
    showPage (audioPage);
}

void SettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    auto side = getLocalBounds().removeFromLeft (150);
    g.setColour (theme::panel);
    g.fillRect (side);
    g.setColour (theme::line);
    g.drawVerticalLine (side.getRight() - 1, 0.0f, (float) getHeight());
    theme::caption (g, "Settings", side.reduced (12, 0).removeFromTop (34));
}

void SettingsComponent::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromLeft (150).reduced (8, 0).withTrimmedTop (34);
    for (auto* b : tabs)
    {
        b->setBounds (side.removeFromTop (30));
        side.removeFromTop (4);
    }
    for (juce::Component* p : { (juce::Component*) audio.get(), (juce::Component*) routing.get(),
                                (juce::Component*) plugins.get(), (juce::Component*) performance.get() })
        p->setBounds (r);
}

// ---------------------------------------------------------------------------
SettingsWindow::SettingsWindow (AppContext& ctx)
    : juce::DocumentWindow ("LiveRack Settings", theme::bg, juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    content = new SettingsComponent (ctx);
    setContentOwned (content, true);
    setResizable (true, false);
    setResizeLimits (760, 480, 4000, 3000);
    centreWithSize (getWidth(), getHeight());
}
} // namespace lr
