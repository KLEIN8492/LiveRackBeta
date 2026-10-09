#include "MainComponent.h"

namespace lr
{
MainComponent::MainComponent (AppContext& c)
    : ctx (c), topBar (c), rackList (c), rackView (c)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (topBar);
    addAndMakeVisible (rackList);
    addAndMakeVisible (rackView);

    rackList.onSelectionChanged = [this] (const juce::String& id) { rackView.setRack (id); };

    bannerClose.onClick = [this] { showBanner ({}, {}); };
    addChildComponent (bannerClose);

    ctx.host.onStateChanged = [this]
    {
        rackList.refresh();
        rackView.refresh();
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
}

void MainComponent::saveSnapshot (const juce::File& png)
{
    timerCallback();
    auto image = createComponentSnapshot (getLocalBounds(), true, 2.0f);
    png.deleteFile();
    juce::FileOutputStream out (png);
    if (out.openedOk())
        juce::PNGImageFormat().writeImageToStream (image, out);
}

void MainComponent::showBanner (const juce::String& text, juce::Colour colour)
{
    bannerText = text;
    bannerColour = colour;
    bannerClose.setVisible (text.isNotEmpty());
    resized();
    repaint();
}

void MainComponent::timerCallback()
{
    topBar.tick();
    rackList.tick();
    rackView.tick();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    if (bannerText.isNotEmpty())
    {
        auto r = getLocalBounds().withTop (topBar.getBottom()).withHeight (30);
        g.setColour (bannerColour);
        g.fillRect (r);
        g.setColour (juce::Colours::black);
        g.setFont (theme::font (12.0f, true));
        g.drawText (bannerText.toUpperCase(), r.reduced (12, 0).withTrimmedRight (90), juce::Justification::centredLeft, true);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (46));

    if (bannerText.isNotEmpty())
    {
        auto banner = r.removeFromTop (30);
        bannerClose.setBounds (banner.removeFromRight (90).reduced (6, 5));
    }

    rackList.setBounds (r.removeFromLeft (330));
    rackView.setBounds (r);
}
} // namespace lr
