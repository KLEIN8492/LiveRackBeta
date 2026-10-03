#include "MainComponent.h"

namespace lr
{
MainComponent::MainComponent (AppContext& c)
    : ctx (c), topBar (c), rackList (c), rackView (c), routingView (c)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (topBar);
    addAndMakeVisible (rackList);
    addAndMakeVisible (rackView);
    addChildComponent (routingView);

    topBar.onViewChanged = [this] (TopBar::View v) { setView (v); };
    rackList.onSelectionChanged = [this] (const juce::String& id) { rackView.setRack (id); };

    bannerClose.onClick = [this] { showBanner ({}, {}); };
    addChildComponent (bannerClose);

    ctx.host.onStateChanged = [this]
    {
        rackList.refresh();
        rackView.refresh();
        if (routingView.isVisible())
            routingView.refresh();
    };

    rackList.refresh();
    setSize (1180, 760);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    ctx.host.onStateChanged = nullptr;
    setLookAndFeel (nullptr);
}

void MainComponent::showChanged()
{
    rackList.refresh();
    rackView.setRack (rackList.getSelectedRackId());
    routingView.refresh();
}

void MainComponent::showBanner (const juce::String& text, juce::Colour colour)
{
    bannerText = text;
    bannerColour = colour;
    bannerClose.setVisible (text.isNotEmpty());
    resized();
    repaint();
}

void MainComponent::setView (TopBar::View v)
{
    view = v;

    if (v == TopBar::settingsView && settingsView == nullptr)
    {
        settingsView = std::make_unique<SettingsView> (ctx);
        addChildComponent (*settingsView);
    }

    rackList.setVisible (v != TopBar::settingsView);
    rackView.setVisible (v == TopBar::racksView);
    routingView.setVisible (v == TopBar::routingView);
    if (settingsView != nullptr)
        settingsView->setVisible (v == TopBar::settingsView);

    if (v == TopBar::routingView)
        routingView.refresh();

    resized();
}

void MainComponent::timerCallback()
{
    topBar.tick();
    rackList.tick();
    if (rackView.isVisible())
        rackView.tick();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    if (bannerText.isNotEmpty())
    {
        auto r = getLocalBounds().withTop (topBar.getBottom()).withHeight (36);
        g.setColour (bannerColour.withAlpha (0.18f));
        g.fillRect (r);
        g.setColour (bannerColour);
        g.setFont (theme::font (13.0f, true));
        g.drawText (bannerText, r.reduced (16, 0).withTrimmedRight (90), juce::Justification::centredLeft, true);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (52));

    if (bannerText.isNotEmpty())
    {
        auto banner = r.removeFromTop (36);
        bannerClose.setBounds (banner.removeFromRight (90).reduced (8, 6));
    }

    if (settingsView != nullptr && settingsView->isVisible())
    {
        settingsView->setBounds (r);
        return;
    }

    rackList.setBounds (r.removeFromLeft (300));
    rackView.setBounds (r);
    routingView.setBounds (r);
}
} // namespace lr
